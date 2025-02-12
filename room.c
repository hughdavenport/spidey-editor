#include <assert.h>
#include <errno.h>

#include <ctype.h>

#include <stdint.h>

#include <string.h>

#include <arpa/inet.h>

#define IO_IMPLEMENTATION
#include "io.h"

#include "room.h"

// header of ROOMS.SPL (and BACKS.SPL also)
typedef struct {
    uint16_t definitions[64];
    uint16_t filesize;
} Header;

void freeRoom(Room *room) {
    if (room == NULL) return;
    if (room->data.objects) {
        for (size_t i = 0; i < room->data.num_objects; i ++) {
            if (room->data.objects[i].tiles) free(room->data.objects[i].tiles);
        }
        free(room->data.objects);
    }
    ARRAY_FREE(room->rest);
    ARRAY_FREE(room->compressed);
    ARRAY_FREE(room->decompressed);
}

void freeRoomFile(RoomFile *file) {
    if (file == NULL) return;
    for (size_t i = 0; i < C_ARRAY_LEN(file->rooms); i ++) {
        freeRoom(&file->rooms[i]);
    }
}

void dumpHeader(Header *head) {
    fprintf(stderr, "Header:\n");
    _Static_assert(sizeof(Header) == 130, "readHeader expected a different header size");
    fprintf(stderr, "  Room offsets: [");
    for (int i = 0; i < 64; i ++) {
        if (i != 0) fprintf(stderr, ", ");
        fprintf(stderr, "0x%04X", head->definitions[i]);
    }
    fprintf(stderr, "]\n");
    fprintf(stderr, "  filesize = %u\n", head->filesize);
}

bool readHeader(Header *head, FILE *fp) {
    if (head == NULL) return false;
    if (fp == NULL) return false;
    _Static_assert(sizeof(Header) == 130, "readHeader expected a different header size");
    if (fread(head, sizeof(Header), 1, fp) != 1) return false;
    for (size_t i = 0; i < C_ARRAY_LEN(head->definitions); i ++) {
        head->definitions[i] = ntohs(head->definitions[i]);
    }
    head->filesize = ntohs(head->filesize);
    return true;
}

void dumpRoom(Room *room) {
    fprintf(stderr, "Room %u @0x%04X: \"%s\"\n", room->index, room->address, room->data.name);

    /* fprintf(stderr, "  Compressed room (length=%zu): [", room->compressed.length); */
    /* for (size_t i = 0; i < room->compressed.length; i ++) { */
    /*     if (i % 32 == 0) fprintf(stderr, "\n    "); */
    /*     if ((i % 32) != 0) fprintf(stderr, " "); */
    /*     fprintf(stderr, "%02x", room->compressed.data[i]); */
    /* } */
    /* fprintf(stderr, "]\n"); */

    /* fprintf(stderr, "  Decompressed room (length=%zu): [", room->decompressed.length); */
    /* for (size_t i = 0; i < room->decompressed.length; i ++) { */
    /*     if (i % 32 == 0) fprintf(stderr, "\n    "); */
    /*     if ((i % 32) != 0) fprintf(stderr, " "); */
    /*     fprintf(stderr, "%02x", room->decompressed.data[i]); */
    /* } */
    /* fprintf(stderr, "]\n"); */

    fprintf(stderr, "  Tiles:");
    for (size_t i = 0; i < C_ARRAY_LEN(room->data.tiles); i ++) {
        uint8_t x = i % WIDTH_TILES;
        uint8_t y = i / WIDTH_TILES;
        if (x == 0) fprintf(stderr, "\n    ");
        bool colored = false;
        for (size_t o = 0; o < room->data.num_objects; o ++) {
            if (x >= room->data.objects[o].x && x < room->data.objects[o].x + room->data.objects[o].width &&
                    y >= room->data.objects[o].y && y < room->data.objects[o].y + room->data.objects[o].height) {

                fprintf(stderr, "\033[%d%ld;1m", room->data.objects[o].type == STATIC ? 3 : 4, (o % 8) + 1);
                colored = true;
                break;
            }
        }
        if (x != 0) fprintf(stderr, " ");
        if (room->data.tiles[i] == BLANK_TILE) {
            fprintf(stderr, "  ");
        } else {
            fprintf(stderr, "%02x", room->data.tiles[i]);
        }
        if (colored) fprintf(stderr, "\033[m");
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "  Tile offset: %u\n", room->data.tile_offset);
    fprintf(stderr, "  Background: %u\n", room->data.background);
    fprintf(stderr, "  Room North: %u\n", room->data.room_north);
    fprintf(stderr, "  Room East: %u\n", room->data.room_east);
    fprintf(stderr, "  Room South: %u\n", room->data.room_south);
    fprintf(stderr, "  Room West: %u\n", room->data.room_west);

    fprintf(stderr, "  UNKNOWN (a): %02x\n", room->data.UNKNOWN_a);

    fprintf(stderr, "  Gravity vertical: %u\n", room->data.gravity_vertical);
    fprintf(stderr, "  Gravity horizontal: %u\n", room->data.gravity_horizontal);

    fprintf(stderr, "  UNKNOWN (b): %02x\n", room->data.UNKNOWN_b);
    fprintf(stderr, "  UNKNOWN (c): %02x\n", room->data.UNKNOWN_c);
    fprintf(stderr, "  number of moving objects: %02x\n", room->data.num_objects);
    fprintf(stderr, "  UNKNOWN (e): %02x\n", room->data.UNKNOWN_e);
    fprintf(stderr, "  UNKNOWN (f): %02x\n", room->data.UNKNOWN_f);

    fprintf(stderr, "  Moving objects (length=%u):\n", room->data.num_objects);
    for (size_t i = 0; i < room->data.num_objects; i ++) {
        fprintf(stderr, "    {.type = %s, .x = %d, .y = %d, .width = %d, .height = %d}\n",
                room->data.objects[i].type == STATIC ? "STATIC" : "ENEMY",
                room->data.objects[i].x, room->data.objects[i].y,
                room->data.objects[i].width, room->data.objects[i].height);
        // FIXME show tiles?
    }

    fprintf(stderr, "  Left over data (length=%zu): [", room->rest.length);
    for (size_t i = 0; i < room->rest.length; i ++) {
        if (i != 0) fprintf(stderr, ", ");
        fprintf(stderr, "0x%02x", room->rest.data[i]);
    }
    fprintf(stderr, "]\n");
}

bool readRoom(Room *room, Header *head, size_t idx, FILE *fp) {
    if (head == NULL) return false;
    if (idx >= C_ARRAY_LEN(head->definitions)) return false;
    if (fp == NULL) return false;

    long seek = head->definitions[idx];
    if (fseek(fp, seek, SEEK_SET) < 0) {
        perror("Could not seek to start of room");
        return false;
    }
    long size = head->definitions[idx + 1] - head->definitions[idx];
    if (size <= 0) return false;

    Room tmp = {
        .index = idx,
        .address = seek,
        .decompressed = {0},
        .compressed = {0},
    };

/* #define log(...) fprintf(__VA_ARGS__) */
#define log(...) do {} while (false)
#define read_next(dst, fp) { \
        int next_val = fgetc((fp)); \
        if (next_val == EOF) { \
            log(stderr, "%s:%d: Unexpected end of file @ 0x%lx4x\n", \
                    __FILE__, __LINE__, ftell(fp)); \
            freeRoom(&tmp); \
            return false; \
        } \
        size --; \
        if (isprint(next_val)) log(stderr, "%s:%d: READ 0x%02x (%hhd, '%c') @ 0x%lx, %zu left, decompressed %zu so far\n", __FILE__, __LINE__, next_val, next_val, next_val, ftell(fp), size, tmp.decompressed.length); \
        else log(stderr, "%s:%d: READ 0x%02x (%hhd) @ 0x%lx, %zu left, decompressed %zu so far\n", __FILE__, __LINE__, next_val, next_val, ftell(fp), size, tmp.decompressed.length); \
        ARRAY_ADD(tmp.compressed, next_val); \
        dst = next_val; \
}

// The source reads the first two words into 0x1d1d-0x1d20 (including marker and these tile markers)
    uint8_t markers[4];
    log(stderr, "%s:%d: Starting read at 0x%lx\n", __FILE__, __LINE__, ftell(fp));
    read_next(markers[0], fp);
    if (markers[0] != 0x8F) {
        log(stderr, "Expected room to start with 0x8F, found %02X\n", markers[0]);
        freeRoom(&tmp);
        return false;
    }
    for (size_t i = 1; i < C_ARRAY_LEN(markers); i ++) {
        read_next(markers[i], fp);
    }

    while (size) {
        uint8_t val;
        read_next(val, fp);
        if (val == markers[1]) {
            // RLE
            uint8_t next;
            read_next(next, fp);
            if (next < 0x80) {
                // get last byte, store that byte (next + 2) times)
                if (tmp.decompressed.length < 1) {
                    log(stderr, "%s:%d: Corrupt RLE state @ 0x%lx4x\n",
                            __FILE__, __LINE__, ftell(fp));
                    freeRoom(&tmp);
                    return false;
                }
                uint8_t copy = tmp.decompressed.data[tmp.decompressed.length - 1];
                log(stderr, "%s:%d: Copying 0x%02x %d times\n",
                        __FILE__, __LINE__, copy, next + 2);
                for (int i = 0; i < next + 2; i ++) {
                    ARRAY_ADD(tmp.decompressed, copy);
                }
            } else if (next != 0x80) {
                // copy abs(next) bytes from dst - 0x20 to dst
                if (tmp.decompressed.length < 0x20) {
                    log(stderr, "%s:%d: Corrupt RLE state @ 0x%lx4x\n",
                            __FILE__, __LINE__, ftell(fp));
                    freeRoom(&tmp);
                    return false;
                }
                log(stderr, "%s:%d: Copying from behind 0x%02x, %d times\n",
                        __FILE__, __LINE__, 0x20, -(int8_t)next);
                for (int i = 0; i < -(int8_t)next; i ++) {
                    uint8_t copy = tmp.decompressed.data[tmp.decompressed.length - 0x20];
                    ARRAY_ADD(tmp.decompressed, copy);
                }
            } else {
                log(stderr, "%s:%d: Output 0x%02x as is @ %lu\n", __FILE__, __LINE__, val, tmp.decompressed.length);
                ARRAY_ADD(tmp.decompressed, val);
            }
        } else if (val == markers[2]) {
            // LZ
            uint8_t next;
            read_next(next, fp);
            if (next < 0x80) {
                // Store start of room marker next + 2 times
                log(stderr, "%s:%d: Copying 0x%02x %d times\n",
                        __FILE__, __LINE__, markers[0], next + 2);
                for (int i = 0; i < next + 2; i ++) {
                    ARRAY_ADD(tmp.decompressed, markers[0]);
                }
            } else if (next != 0x80) {
                // copy abs(next) + 1 bytes from dst - read() to dst
                uint8_t back;
                read_next(back, fp);
                if (tmp.decompressed.length < back) {
                    log(stderr, "%s:%d: Corrupt RLE state @ 0x%lx4x\n",
                            __FILE__, __LINE__, ftell(fp));
                    freeRoom(&tmp);
                    return false;
                }
                log(stderr, "%s:%d: Copying from behind 0x%02x, %d times\n",
                        __FILE__, __LINE__, back, -(int8_t)next + 1);
                for (int i = 0; i < -(int8_t)next + 1; i ++) {
                    uint8_t copy = tmp.decompressed.data[tmp.decompressed.length - back];
                    ARRAY_ADD(tmp.decompressed, copy);
                }
            } else {
                log(stderr, "%s:%d: Output 0x%02x as is @ %lu\n", __FILE__, __LINE__, val, tmp.decompressed.length);
                ARRAY_ADD(tmp.decompressed, val);
            }
        } else if (val == markers[3]) {
            // Larger backindex LZ's
            uint8_t next;
            read_next(next, fp);
            if (next < 0x80) {
                uint16_t back;
                read_next(back, fp);
                back |= 0x200;
                if (tmp.decompressed.length < back) {
                    log(stderr, "%s:%d: Corrupt RLE state @ 0x%lx4x\n",
                            __FILE__, __LINE__, ftell(fp));
                    freeRoom(&tmp);
                    return false;
                }
                log(stderr, "%s:%d: Copying from behind 0x%02x, %d times\n",
                        __FILE__, __LINE__, back, next + 2);
                for (int i = 0; i < next + 2; i ++) {
                    uint8_t copy = tmp.decompressed.data[tmp.decompressed.length - back];
                    ARRAY_ADD(tmp.decompressed, copy);
                }
            } else if (next != 0x80) {
                uint16_t back;
                read_next(back, fp);
                back |= 0x100;
                if (tmp.decompressed.length < back) {
                    log(stderr, "%s:%d: Corrupt RLE state @ 0x%lx4x\n",
                            __FILE__, __LINE__, ftell(fp));
                    freeRoom(&tmp);
                    return false;
                }
                log(stderr, "%s:%d: Copying from behind 0x%02x, %d times\n",
                        __FILE__, __LINE__, back, -(int8_t)next + 1);
                for (int i = 0; i < -(int8_t)next + 1; i ++) {
                    uint8_t copy = tmp.decompressed.data[tmp.decompressed.length - back];
                    ARRAY_ADD(tmp.decompressed, copy);
                }
            } else {
                log(stderr, "%s:%d: Output 0x%02x as is @ %lu\n", __FILE__, __LINE__, val, tmp.decompressed.length);
                ARRAY_ADD(tmp.decompressed, val);
            }
        } else {
            // Just add it as is then get next
            log(stderr, "%s:%d: Output 0x%02x as is @ %lu\n", __FILE__, __LINE__, val, tmp.decompressed.length);
            ARRAY_ADD(tmp.decompressed, val);
        }
    }
    log(stderr, "%s:%d: Finishing read at 0x%lx\n", __FILE__, __LINE__, ftell(fp));
#undef read_next

    size_t data_idx = 0;
#define read_next(dst, a) { \
        if (data_idx == (a).length) { \
            log(stderr, "%s:%d: Not enough compressed data at %lu bytes long\n", \
                    __FILE__, __LINE__, (a).length); \
            freeRoom(&tmp); \
            return false; \
        } \
        int next_val = (a).data[data_idx++]; \
        if (isprint(next_val)) log(stderr, "%s:%d: READ 0x%02x (%hhd, '%c') @ 0x%lx, %zu left\n", __FILE__, __LINE__, next_val, next_val, next_val, data_idx - 1, (a).length - data_idx); \
        else log(stderr, "%s:%d: READ 0x%02x (%hhd) @ 0x%lx, %zu left\n", __FILE__, __LINE__, next_val, next_val, data_idx - 1, (a).length - data_idx); \
        dst = next_val; \
}

    for (size_t i = 0; i < C_ARRAY_LEN(tmp.data.tiles); i ++) {
        read_next(tmp.data.tiles[i], tmp.decompressed);
    }
    read_next(tmp.data.tile_offset, tmp.decompressed);
    read_next(tmp.data.background, tmp.decompressed);
    read_next(tmp.data.room_north, tmp.decompressed);
    read_next(tmp.data.room_east, tmp.decompressed);
    read_next(tmp.data.room_south, tmp.decompressed);
    read_next(tmp.data.room_west, tmp.decompressed);

    read_next(tmp.data.UNKNOWN_a, tmp.decompressed);

    read_next(tmp.data.gravity_vertical, tmp.decompressed);
    read_next(tmp.data.gravity_horizontal, tmp.decompressed);

    read_next(tmp.data.UNKNOWN_b, tmp.decompressed);
    read_next(tmp.data.UNKNOWN_c, tmp.decompressed);
    read_next(tmp.data.num_objects, tmp.decompressed);
    read_next(tmp.data.UNKNOWN_e, tmp.decompressed);
    read_next(tmp.data.UNKNOWN_f, tmp.decompressed);

    log(stderr, "%s:%d: Reading name at %04lu\n", __FILE__, __LINE__, data_idx);
    for (size_t i = 0; i < C_ARRAY_LEN(tmp.data.name); i ++) {
        read_next(tmp.data.name[i], tmp.decompressed);
    }

    log(stderr, "%s:%d: Reading %u moving objects at %04lu\n", __FILE__, __LINE__, tmp.data.UNKNOWN_d, data_idx);
    tmp.data.objects = calloc(tmp.data.num_objects, sizeof(struct RoomObject));
    assert(tmp.data.objects != NULL);
    struct RoomObject *objects = tmp.data.objects;
    for (size_t i = 0; i < tmp.data.num_objects; i ++) {
        uint8_t msb, lsb;
        read_next(msb, tmp.decompressed);
        read_next(lsb, tmp.decompressed);
        if ((msb & 0x80) == 0) {
            objects[i].type = STATIC;
            objects[i].x = lsb & 0x1f;
            objects[i].y = msb & 0x1f;
            objects[i].width = ((lsb & 0xe0) >> 5) + 1;
            objects[i].height = ((msb & 0x60) >> 5) + 1;
            assert(objects[i].x < WIDTH_TILES);
            assert(objects[i].width < WIDTH_TILES);
            assert(objects[i].x + objects[i].width < WIDTH_TILES);
            if (!(objects[i].y < HEIGHT_TILES)) {
                fprintf(stderr, "WARNING: Room %ld (%s) object %zu y is out of bounds (%u >= %u)\n",
                        idx, tmp.data.name, i, objects[i].y, HEIGHT_TILES);
                if (!(objects[i].y < WIDTH_TILES)) {
                    fprintf(stderr, "WARNING: Room %ld (%s) object %zu y is really out of bounds (%u >= %u)\n",
                            idx, tmp.data.name, i, objects[i].y, WIDTH_TILES);
                }
                continue;
            }
            assert(objects[i].y < HEIGHT_TILES);
            assert(objects[i].height < HEIGHT_TILES);
            if (!(objects[i].y + objects[i].height < HEIGHT_TILES)) {
                fprintf(stderr, "WARNING: Room %ld (%s) object %zu y+height is out of bounds (%u >= %u)\n",
                        idx, tmp.data.name, i, objects[i].y + objects[i].height, HEIGHT_TILES);
                if (!(objects[i].y + objects[i].height < WIDTH_TILES)) {
                    fprintf(stderr, "WARNING: Room %ld (%s) object %zu y+height is really out of bounds (%u >= %u)\n",
                            idx, tmp.data.name, i, objects[i].y + objects[i].height, WIDTH_TILES);
                }
                continue;
            }
            objects[i].tiles = malloc(objects[i].width * objects[i].height);
            assert(objects[i].tiles != NULL);
            for (size_t y = objects[i].y; y < objects[i].y + objects[i].height; y ++) {
                memcpy(
                        objects[i].tiles + (y - objects[i].y) * objects[i].width,
                        tmp.data.tiles + y * WIDTH_TILES + objects[i].x,
                        objects[i].width
                      );
            }
        } else {
            objects[i].type = ENEMY;
            objects[i].x = lsb & 0x1f;
            objects[i].y = msb & 0x1f;

            // FIXME work out what this is
            objects[i].width = ((lsb & 0xe0) >> 5) + 1;
            objects[i].height = ((msb & 0x60) >> 5) + 1;
        }
    }
    // then stuff that controls enemy placement, switch actions, etc
    while (data_idx != tmp.decompressed.length) {
        uint8_t val;
        read_next(val, tmp.decompressed);
        ARRAY_ADD(tmp.rest, val);
    }

    /* log(stderr, "%s:%d: UNIMPLEMENTED\n", __FILE__, __LINE__); return false; */
#undef read_next
#undef log
    *room = tmp;
    return true;
}

bool readFile(RoomFile *file, FILE *fp) {
    if (file == NULL) return false;
    if (fp == NULL) return false;
    if (fseek(fp, 0L, SEEK_SET) < 0) {
        perror("Could not seek file to header");
        return false;
    }
    Header head = {0};
    if (!readHeader(&head, fp)) {
        perror("Could not read header");
        return false;
    }
    /* dumpHeader(&head); */
    long length = filesize(fp);
    if (length < 0) {
        perror("Could not get filesize");
        return false;
    }
    if (head.filesize != length) {
        fprintf(stderr, "Unexpected filesize %u, actual = %zu\n", head.filesize, length);
        return false;
    }

    for (size_t idx = 0; idx < C_ARRAY_LEN(head.definitions); idx ++) {
        if (!readRoom(&file->rooms[idx], &head, idx, fp)) {
            file->rooms[idx].valid = false;
            /* fprintf(stderr, "Could not read room %lu\n", idx); */
            continue;
        }
        file->rooms[idx].valid = true;
        /* dumpRoom(&file->rooms[idx]); */
    }

    return true;
}

#define MAX_ROOM_FILE_SIZE 0x3000

bool writeRoom(Room *room, FILE *fp) {
    if (fp == NULL || room == NULL || !room->valid) return false;

    if (room->compressed.length > 0) {
        size_t written = 0;
        do {
            size_t write_ret = fwrite(room->compressed.data + written, sizeof(uint8_t), room->compressed.length - written, fp);
            if (write_ret == 0) return false;
            written += write_ret;
        } while (written < room->compressed.length);
        return true;
    }

    fprintf(stderr, "Writing Room %d \"%s\" at %ld.\n", room->index, room->data.name, ftell(fp));

/* #define log(...) fprintf(__VA_ARGS__) */
#define log(...) do {} while (false)
    uint8_t decompressed[960] = {0};
    uint8_t compressed[960] = {0};
    size_t d_idx = 0;
    size_t d_len = 0;
    size_t c_len = 0;
    for (size_t i = 0; i < C_ARRAY_LEN(room->data.tiles); i ++) {
        decompressed[d_len++] = room->data.tiles[i];
    }
    decompressed[d_len++] = room->data.tile_offset;
    decompressed[d_len++] = room->data.background;
    decompressed[d_len++] = room->data.room_north;
    decompressed[d_len++] = room->data.room_east;
    decompressed[d_len++] = room->data.room_south;
    decompressed[d_len++] = room->data.room_west;

    decompressed[d_len++] = room->data.UNKNOWN_a;

    decompressed[d_len++] = room->data.gravity_vertical;
    decompressed[d_len++] = room->data.gravity_horizontal;

    decompressed[d_len++] = room->data.UNKNOWN_b;
    decompressed[d_len++] = room->data.UNKNOWN_c;
    decompressed[d_len++] = room->data.num_objects;
    decompressed[d_len++] = room->data.UNKNOWN_e;
    decompressed[d_len++] = room->data.UNKNOWN_f;

    for (size_t i = 0; i < C_ARRAY_LEN(room->data.name); i ++) {
        decompressed[d_len++] = room->data.name[i];
    }

    assert(d_len + 2 * room->data.num_objects < C_ARRAY_LEN(decompressed));
    for (size_t i = 0; i < room->data.num_objects; i ++) {
        // FIXME depending on type, first byte should have 0x80
        switch (room->data.objects[i].type) {
            case STATIC:
                decompressed[d_len++] = room->data.objects[i].x | ((room->data.objects[i].width - 1) << 5);
                decompressed[d_len++] = room->data.objects[i].y | ((room->data.objects[i].height - 1) << 5);
                break;

            case ENEMY:
                // FIXME write this properly
                decompressed[d_len++] = room->data.objects[i].x;
                decompressed[d_len++] = room->data.objects[i].y;
                break;

        }
    }
    // then stuff that controls enemy placement, switch actions, etc
    assert(d_len + room->rest.length < C_ARRAY_LEN(decompressed));
    for (size_t i = 0; i < room->rest.length; i ++) {
        decompressed[d_len++] = room->rest.data[i];
    }

    compressed[c_len++] = '\x8f';
    compressed[c_len++] = '\0';
    compressed[c_len++] = '\0';
    compressed[c_len++] = '\0';

    // Really any compressed can be chosen. But this chooses 3 that are hopefully not used
    // It will only be the same as a used byte if it overwraps
    for (size_t i = 0; i < d_len; ++ i) {
        if (decompressed[i] == compressed[1]) {
            if (decompressed[i] == 254) {
                compressed[1] = 1;
                break;
            }
            compressed[1] ++;
            i = 0;
            continue;
        }
    }
    compressed[2] = compressed[1] + 1;
    for (size_t i = 0; i < d_len; ++ i) {
        if (decompressed[i] == compressed[2]) {
            if (decompressed[i] == 254) {
                compressed[2] = 2;
                break;
            }
            compressed[2] ++;
            i = 0;
            continue;
        }
    }
    compressed[3] = compressed[2] + 1;
    for (size_t i = 0; i < d_len; ++ i) {
        if (decompressed[i] == compressed[3]) {
            if (decompressed[i] == 254) {
                compressed[3] = 3;
                break;
            }
            compressed[3] ++;
            i = 0;
            continue;
        }
    }
    compressed[4] = compressed[3] + 1;
    for (size_t i = 0; i < d_len; ++ i) {
        if (decompressed[i] == compressed[4]) {
            if (decompressed[i] == 254) {
                compressed[4] = 4;
                break;
            }
            compressed[4] ++;
            i = 0;
            continue;
        }
    }

    // Options for outputting
    // 1- Output as is, if byte not one of the markers
    // 2- Output RLE encoded byte, send marker[1] and length < 0x80. Copies last decompressed byte length + 2 times
    // 3- Copy fixed size block, send marker[1] and (int8_t)length < 0 != 0x80. Copy last 0x20 bytes -(int8_t)length times
    // 4- Output marker[1], send marker[1] and 0x80
    // 5- Output marker[0], send marker[2] and length < 0x80. Copies room marker (0x8f, but could be different?) length + 2 times
    // 6- LZ Output, send marker[2] and (int8_t)length < 0 != 0x80 and backindex. Copy last backindex bytes -(int8_t)length + 1 times
    // 7- Output marker[2], send marker[2] and 0x80
    // 8- LZ Far output, send marker[3] and length < 0x80 and backindex. Copy 0x200 | backindex bytes length + 2 times
    // 9- LZ Mid output, send marker[3] and (int8_t)length < 0 != 0x80 and backindex. Copy 0x100 | backindex bytes -(int8_t)length + 1 times
    // 10- Output marker[3], send marker[3] and 0x80
    while (d_idx < d_len) {
        uint8_t byte = decompressed[d_idx++];
        uint8_t length = 1;
        log(stderr, "%s:%d: Compressing byte %x @ %lu. Compressed so far %lu.\n",
                __FILE__, __LINE__, byte, d_idx - 1, c_len);
        uint8_t best = 0;
        uint16_t best_back = 0;
        for (size_t back = 1; back < d_idx - 1 && back < 0x300; back ++) {
            if (decompressed[d_idx - back - 1] == byte) {
                length = 1;
                while (d_idx + length - 1 < d_len && length - 1 < 0x7e) {
                    if (back == 0) break;
                    if (decompressed[d_idx + length - 1] != decompressed[d_idx - back + (length % back) - 1]) {
                        break;
                    }
                    length ++;
                }
                if (length > best) {
                    if (back < 0x100 && length - 2 > 0x7e) length = 0x80;
                    best = length;
                    best_back = back;
                    if (length - 1 >= 0x7e) break;
                }
            }
        }
        length = 1;
        if (d_idx >= 0x20 + 1 && decompressed[d_idx - 0x20 - 1] == byte) {
            while (d_idx + length - 1 < d_len && length < 0x7f) {
                if (decompressed[d_idx + length - 1] != decompressed[d_idx - 0x20 + (length % 0x20) - 1]) {
                    break;
                }
                length ++;
            }
            // 3- Copy fixed size block, send marker[1] and (int8_t)length < 0 != 0x80. Copy last 0x20 bytes -(int8_t)length times
            log(stderr, "%s:%d: Fixed size 0x20 block, length %u (%x).\n",
                    __FILE__, __LINE__, length, -(int8_t)length);
            if (length > 2 && (best < 3 || (best - 3) < (length - 2))) {
                compressed[c_len++] = compressed[1];
                compressed[c_len++] = -(int8_t)length;
                d_idx += length - 1; // We had already read 1 at start of loop
                continue;
            }
        }
        if (d_idx >= 2 && byte == decompressed[d_idx - 2]) {
            while (d_idx + length - 1 < d_len && length - 2 < 0x7e) {
                if (decompressed[d_idx + length - 1] != byte) break;
                length ++;
            }
            if (length > 2 && (best < 3 || (best - 3) < (length - 2))) {
                // 2- Output RLE encoded byte, send marker[1] and length < 0x80. Copies last decompressed byte length + 2 times
                log(stderr, "%s:%d: RLE length %u (%x).\n",
                        __FILE__, __LINE__, length, length - 2);
                compressed[c_len++] = compressed[1];
                compressed[c_len++] = length - 2;
                d_idx += length - 1; // We had already read 1 at start of loop
                continue;
            }
        }
        if (byte == compressed[0]) {
            while (d_idx + length - 1 < d_len && length - 2 < 0x7e) {
                if (decompressed[d_idx + length - 1] != byte) break;
                length ++;
            }
            if (length > 2 && (best < 3 || (best - 3) < (length - 2))) {
                // 5- Output marker[0], send marker[2] and length < 0x80. Copies room marker (0x8f, but could be different?) length + 2 times
                log(stderr, "%s:%d: RLE marker[0] length %u (%x).\n",
                        __FILE__, __LINE__, length, length - 2);
                compressed[c_len++] = compressed[0];
                compressed[c_len++] = length - 2;
                d_idx += length - 1; // We had already read 1 at start of loop
                continue;
            }
        } 
        if (best > 3) { // Is it worth it to compress?
            if (best_back < 0x100) {
                // 6- LZ Output, send marker[2] and (int8_t)length < 0 != 0x80 and backindex. Copy last backindex bytes -(int8_t)length + 1 times
                log(stderr, "%s:%d: LZ length %u (%x), back %u (%x).\n",
                        __FILE__, __LINE__, best, -(int8_t)(best - 1), best_back, best_back);
                compressed[c_len++] = compressed[2];
                compressed[c_len++] = -(int8_t)(best - 1);
                compressed[c_len++] = (uint8_t)(best_back & 0xFF);
                d_idx += best - 1; // We already read 1 at start of loop
                continue;
            } else if (best_back < 0x200) {
                // 9- LZ Mid output, send marker[3] and (int8_t)length < 0 != 0x80 and backindex. Copy 0x100 | backindex bytes -(int8_t)length + 1 times
                log(stderr, "%s:%d: LZ Mid length %u (%x), back %u (%x).\n",
                        __FILE__, __LINE__, best, -(int8_t)(best - 1), best_back, best_back);
                compressed[c_len++] = compressed[3];
                compressed[c_len++] = -(int8_t)(best - 1);
                compressed[c_len++] = (uint8_t)(best_back & 0xFF);
                d_idx += best - 1; // We already read 1 at start of loop
                continue;
            } else if (best_back < 0x300) {
                // 8- LZ Far output, send marker[3] and length < 0x80 and backindex. Copy 0x200 | backindex bytes length + 2 times
                log(stderr, "%s:%d: LZ Far length %u (%x), back %u (%x).\n",
                        __FILE__, __LINE__, best, best - 2, best_back, best_back);
                compressed[c_len++] = compressed[3];
                compressed[c_len++] = best - 2;
                compressed[c_len++] = (uint8_t)(best_back & 0xFF);
                d_idx += best - 1; // We already read 1 at start of loop
                continue;
            }
        }

        log(stderr, "%s:%d: Output as is (or escaped if one of markers).\n", __FILE__, __LINE__);
        if (byte == compressed[1]) {
            // 4- Output marker[1], send marker[1] and 0x80
            compressed[c_len++] = compressed[1];
            compressed[c_len++] = 0x80;
            continue;
        } else if (byte == compressed[2]) {
            // 7- Output marker[2], send marker[2] and 0x80
            compressed[c_len++] = compressed[2];
            compressed[c_len++] = 0x80;
            continue;
        } else if (byte == compressed[3]) {
            // 10- Output marker[3], send marker[3] and 0x80
            compressed[c_len++] = compressed[3];
            compressed[c_len++] = 0x80;
            continue;
        }
        // 1- Output as is, if byte not one of the compressed
        compressed[c_len++] = byte;
    }


    fprintf(stderr, "  Data to compress (length=%zu): [", d_len);
    for (size_t i = 0; i < d_len; i ++) {
        if (i % 32 == 0) fprintf(stderr, "\n    ");
        if ((i % 32) != 0) fprintf(stderr, " ");
        fprintf(stderr, "%02x", decompressed[i]);
    }
    fprintf(stderr, "]\n");

    fprintf(stderr, "  Newly compressed room (length=%zu): [", c_len);
    for (size_t i = 0; i < c_len; i ++) {
        if (i % 32 == 0) fprintf(stderr, "\n    ");
        if ((i % 32) != 0) fprintf(stderr, " ");
        fprintf(stderr, "%02x", compressed[i]);
    }
    fprintf(stderr, "]\n");

    size_t written = 0;
    do {
        size_t write_ret = fwrite(compressed + written, sizeof(uint8_t), c_len - written, fp);
        if (write_ret == 0) return false;
        written += write_ret;
    } while (written < c_len);

#undef log
    return true;
}

bool writeFile(RoomFile *file, FILE *fp) {
    // structure:
    //   128 byte header, 2 byte indices to where that room is, ascending
    //   room marker 0x8f
    //   3 bytes which control compression
    //   compressed room
    // must fit within 0x3000 bytes

    Header head = {0};
    long offset = sizeof(Header);
    if (fseek(fp, offset, SEEK_SET) == -1) return false;
    for (size_t i = 0; i < C_ARRAY_LEN(file->rooms); i ++) {
        head.definitions[i] = htons(offset);
        if (!writeRoom(&file->rooms[i], fp)) {
            /* fprintf(stderr, "%s:%d: Not writing room %ld\n", __FILE__, __LINE__, i); */
            continue;
        }
        if (ftell(fp) > MAX_ROOM_FILE_SIZE) {
            fprintf(stderr, "%s:%d: Can't write room %ld @ 0x%2lx within size limit. size now %lx\n",
                    __FILE__, __LINE__, i, offset, ftell(fp));
            break;
        }
        /* fprintf(stderr, "%s:%d: Wrote room %ld \"%s\" @ 0x%2lx length %ld\n", */
        /*         __FILE__, __LINE__, i, file->rooms[i].data.name, offset, ftell(fp) - offset); */

        offset = ftell(fp);
    }
    head.filesize = htons(ftell(fp));

    if (fseek(fp, 0, SEEK_SET) == -1) return false;
    if (fwrite(&head, sizeof(Header), 1, fp) != 1) return false;
    return true;
}

bool readRooms(RoomFile *file) {
    char *fileName = "ROOMS.SPL";
    FILE *fp = fopen(fileName, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open %s for reading.\n", fileName);
        return false;
    }
    bool ret = readFile(file, fp);
    fclose(fp);
    return ret;
}

typedef struct {
    uint8_t room_id;
    size_t address;
    uint8_t value;
} PatchInstruction;

#define ROOMS_FILE "ROOMS.SPL"
int editor_main();
int main(int argc, char **argv) {
    char *fileName = ROOMS_FILE;
    RoomFile file = {0};
    ARRAY(PatchInstruction) patches = {0};
    char *end = NULL;
    bool recompress = false;
    int recompress_room = -1;
    bool display = false;
    bool list = false;
    int display_room = -1;
    int find_tile = -1;
    int find_tile_offset = 0;
    char *program = argv[0];
    argc --;
    argv ++;
    while (argc > 0) {
        if (strcmp(argv[0], "patch") == 0) {
            if (argc <= 3) {
                fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                return 1;
            }
            long room_id = strtol(argv[1], &end, 0);
            if (errno == EINVAL || end == NULL || *end != '\0') {
                fprintf(stderr, "Invalid number: %s\n", argv[1]);
                fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                return 1;
            }
            if (room_id < 0 || (unsigned)room_id >= C_ARRAY_LEN(file.rooms)) {
                fprintf(stderr, "Room ID out of range 0..%lu\n", C_ARRAY_LEN(file.rooms) - 1);
                fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                return 1;
            }
            argv += 2;
            argc -= 2;
            while (argc >= 2) {
                long addr = strtol(argv[0], &end, 0);
                if (errno == EINVAL || end == NULL || *end != '\0') {
                    addr = -1;
                    if (strncmp(argv[0], "tile[", 5) == 0 && isdigit(argv[0][5])) {
                        // Read [x][y] or [idx]
                        long idx = strtol(argv[0] + 5, &end, 0);
                        if (errno == EINVAL || *end != ']') {
                            fprintf(stderr, "Invalid tile address: %s\n", argv[0]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            return 1;
                        }
                        if (end[1] == '[') {
                            long y = strtol(end + 2, &end, 0);
                            if (errno == EINVAL || strcmp(end, "]") != 0) {
                                fprintf(stderr, "Invalid tile address for y: %s\n", argv[0]);
                                fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                                return 1;
                            }
                            idx = y * WIDTH_TILES + idx;
                        }
                        if (idx > WIDTH_TILES * HEIGHT_TILES) {
                            fprintf(stderr, "Invalid tile address, too large: %s\n", argv[0]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            return 1;
                        }
                        addr = idx;
                    } else if (strcmp(argv[0], "tile_offset") == 0) {
                        addr = offsetof(struct DecompresssedRoom, tile_offset);
                    } else if (strcmp(argv[0], "background") == 0) {
                        addr = offsetof(struct DecompresssedRoom, background);
                    } else if (strcmp(argv[0], "room_north") == 0) {
                        addr = offsetof(struct DecompresssedRoom, room_north);
                    } else if (strcmp(argv[0], "room_east") == 0) {
                        addr = offsetof(struct DecompresssedRoom, room_east);
                    } else if (strcmp(argv[0], "room_south") == 0) {
                        addr = offsetof(struct DecompresssedRoom, room_south);
                    } else if (strcmp(argv[0], "room_west") == 0) {
                        addr = offsetof(struct DecompresssedRoom, room_west);
                    } else if (strcasecmp(argv[0], "UNKNOWN") == 0 || strcasecmp(argv[0], "UNKNOWN_a") == 0) {
                        addr = offsetof(struct DecompresssedRoom, UNKNOWN_a);
                    } else if (strcmp(argv[0], "gravity_vertical") == 0) {
                        addr = offsetof(struct DecompresssedRoom, gravity_vertical);
                    } else if (strcmp(argv[0], "gravity_horizontal") == 0) {
                        addr = offsetof(struct DecompresssedRoom, gravity_horizontal);
                    } else if (strncasecmp(argv[0], "UNKNOWN2[", 9) == 0 && isdigit(argv[0][9])) {
                        long idx = strtol(argv[0] + 9, &end, 0);
                        if (errno == EINVAL || *end != ']') {
                            fprintf(stderr, "Invalid UNKNOWN2 address: %s\n", argv[0]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            return 1;
                        }
                        addr = offsetof(struct DecompresssedRoom, UNKNOWN_b) + idx;
                    } else if (strcasecmp(argv[0], "UNKNOWN_b") == 0) {
                        addr = offsetof(struct DecompresssedRoom, UNKNOWN_b);
                    } else if (strcasecmp(argv[0], "UNKNOWN_c") == 0) {
                        addr = offsetof(struct DecompresssedRoom, UNKNOWN_c);
                    } else if (strcasecmp(argv[0], "UNKNOWN_d") == 0) {
                        fprintf(stderr, "WARNING: This will change how much data is looped over after main loop. You must ensure the correct data\n");
                        // FIXME It'll probably blow the assert when writing
                        addr = offsetof(struct DecompresssedRoom, num_objects);
                    } else if (strcasecmp(argv[0], "UNKNOWN_e") == 0) {
                        addr = offsetof(struct DecompresssedRoom, UNKNOWN_e);
                    } else if (strcasecmp(argv[0], "UNKNOWN_f") == 0) {
                        addr = offsetof(struct DecompresssedRoom, UNKNOWN_f);
                    } else if (strcmp(argv[0], "name") == 0) {
                        size_t len = strlen(argv[1]);
                        if (len > 20) {
                            fprintf(stderr, "Invalid name. Max 20 characters: %s\n", argv[1]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            return 1;
                        }
                        addr = offsetof(struct DecompresssedRoom, name);
                        for (size_t idx = 0; idx < 20; ++ idx) {
                            if (idx < len) {
                                ARRAY_ADD(patches, ((PatchInstruction){ .room_id = room_id, .address = addr + idx, .value = argv[1][idx], }));
                            } else {
                                ARRAY_ADD(patches, ((PatchInstruction){ .room_id = room_id, .address = addr + idx, .value = ' ', }));
                            }
                        }
                        argv += 2;
                        argc -= 2;
                        break;
                    } else if (strncmp(argv[0], "rest[", 5) == 0 && isdigit(argv[0][5])) {
                        long idx = strtol(argv[0] + 9, &end, 0);
                        if (errno == EINVAL || *end != ']') {
                            fprintf(stderr, "Invalid rest address: %s\n", argv[0]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            return 1;
                        }
                        addr = sizeof(struct DecompresssedRoom) + idx;
                    }
                    if (addr == -1) {
                        fprintf(stderr, "Invalid address: %s\n", argv[0]);
                        fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                        return 1;
                    }
                }
                if (addr < 0) {
                    fprintf(stderr, "Address must be positive\n");
                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                    return 1;
                }

                long value = strtol(argv[1], &end, 0);
                if (errno == EINVAL || end == NULL || *end != '\0') {
                    fprintf(stderr, "Invalid number: %s\n", argv[1]);
                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                    return 1;
                }
                if (value < 0 || value > 0xFF) {
                    fprintf(stderr, "Value must be in the range 0..255\n");
                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                    return 1;
                }

                ARRAY_ADD(patches, ((PatchInstruction){ .room_id = room_id, .address = addr, .value = value, }));
                argv += 2;
                argc -= 2;
            }
        } else if (strcmp(argv[0], "recompress") == 0) {
            recompress = true;
            argv ++;
            argc --;
            if (argc > 0 && isdigit(*argv[0])) {
                long room_id = strtol(argv[0], &end, 0);
                if (errno == EINVAL || end == NULL || *end != '\0') {
                    fprintf(stderr, "Invalid number: %s\n", argv[0]);
                    fprintf(stderr, "Usage: %s recompress [ROOM_ID] [FILENAME]\n", program);
                    return 1;
                }
                if (room_id < 0 || (unsigned)room_id >= C_ARRAY_LEN(file.rooms)) {
                    fprintf(stderr, "Value must be in the range 0..%lu\n", C_ARRAY_LEN(file.rooms) - 1);
                    fprintf(stderr, "Usage: %s recompress [ROOM_ID] [FILENAME]\n", program);
                    return 1;
                }
                recompress_room = room_id;
                argv ++;
                argc --;
            }
        } else if (strcmp(argv[0], "display") == 0) {
            display = true;
            argv ++;
            argc --;
            if (argc > 0 && isdigit(*argv[0])) {
                long room_id = strtol(argv[0], &end, 0);
                if (errno == EINVAL || end == NULL || *end != '\0') {
                    fprintf(stderr, "Invalid number: %s\n", argv[0]);
                    fprintf(stderr, "Usage: %s display [ROOM_ID] [FILENAME]\n", program);
                    return 1;
                }
                if (room_id < 0 || (unsigned)room_id >= C_ARRAY_LEN(file.rooms)) {
                    fprintf(stderr, "Value must be in the range 0..%lu\n", C_ARRAY_LEN(file.rooms) - 1);
                    fprintf(stderr, "Usage: %s display [ROOM_ID] [FILENAME]\n", program);
                    return 1;
                }
                display_room = room_id;
                argv ++;
                argc --;
            }
        } else if (strcmp(argv[0], "find_tile") == 0) {
            find_tile = strtol(argv[1], &end, 0);
            if (errno == EINVAL || end == NULL || *end != '\0') {
                fprintf(stderr, "Invalid number: %s\n", argv[1]);
                fprintf(stderr, "Usage: %s find_tile tile_id [tile_offset]\n", program);
                return 1;
            }
            if (find_tile < 0 || find_tile >= 64) {
                fprintf(stderr, "Invalid tile number: %s\n", argv[1]);
                fprintf(stderr, "Usage: %s find_tile tile_id [tile_offset]\n", program);
                return 1;
            }
            argv += 2;
            argc -= 2;
            if (argc > 0 && isdigit(argv[0][0])) {
                find_tile_offset = strtol(argv[0], &end, 0);
                if (errno == EINVAL || end == NULL || *end != '\0') {
                    fprintf(stderr, "Invalid tile number: %s\n", argv[0]);
                    fprintf(stderr, "Usage: %s find_tile tile_id [tile_offset]\n", program);
                    return 1;
                }
                if (find_tile_offset < 0 || find_tile_offset % 4 != 0 || find_tile_offset > 28) {
                    fprintf(stderr, "Invalid offset: %s\n", argv[0]);
                    fprintf(stderr, "Usage: %s find_tile tile_id [tile_offset]\n", program);
                    return 1;
                }
                argv ++;
                argc --;
            }
        } else if (strcmp(argv[0], "editor") == 0) {
            return editor_main();
        } else if (strcmp(argv[0], "rooms") == 0) {
            list = true;
            argv ++;
            argc --;
        } else if (strcmp(argv[0], "help") == 0) {
            fprintf(stderr, "Usage: %s subcommand [subcommand]... [FILENAME]\n", program);
            fprintf(stderr, "Subcommands:\n");
            fprintf(stderr, "    rooms                                - List rooms\n");
            fprintf(stderr, "    display [ROOMID]                     - Defaults to all rooms\n");
            fprintf(stderr, "    recompress                           - No changes to underlying data, just recompress\n");
            fprintf(stderr, "    patch ROOMID ADDR VAL [ADDR VAL]...  - Patch room by changing the bytes requested. For multiple rooms provide patch command again\n");
            fprintf(stderr, "    find_tile TILE [OFFSET]              - Find a tile/offset pair\n");
            fprintf(stderr, "    help                                 - Display this message\n");
            return 1;
        } else {
            fileName = argv[0];
            // Should we allow args after this?
            argv ++;
            argc --;
        }
    }
    if (find_tile == -1 && !list && !display && !recompress && patches.length == 0) {
        fprintf(stderr, "Usage: %s subcommand [subcommand]... [FILENAME]\n", program);
        fprintf(stderr, "Subcommands:\n");
        fprintf(stderr, "    rooms                                - List rooms\n");
        fprintf(stderr, "    display [ROOMID]                     - Defaults to all rooms\n");
        fprintf(stderr, "    recompress                           - No changes to underlying data, just recompress\n");
        fprintf(stderr, "    patch ROOMID ADDR VAL [ADDR VAL]...  - Patch room by changing the bytes requested. For multiple rooms provide patch command again\n");
        fprintf(stderr, "    find_tile TILE [OFFSET]              - Find a tile/offset pair\n");
        fprintf(stderr, "    editor                               - Start an editor\n");
        fprintf(stderr, "    help                                 - Display this message\n");
        return 1;
    }
    FILE *fp = fopen(fileName, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open %s for reading.\n", fileName);
        return 1;
    }
    int ret = 0;
#define defer_return(code) { ret = code; goto defer; }
    fprintf(stderr, "Reading file %s.\n", fileName);
    if (!readFile(&file, fp)) defer_return(1);
    fclose(fp);
    fp = NULL;
    if (find_tile != -1) {
        bool found = false;
        for (size_t i = 0; i < C_ARRAY_LEN(file.rooms); i ++) {
            if (file.rooms[i].valid && file.rooms[i].data.tile_offset == find_tile_offset) {
                for (size_t idx = 0; idx < C_ARRAY_LEN(file.rooms[i].data.tiles); idx ++) {
                    if (file.rooms[i].data.tiles[idx] == find_tile) {
                        printf("Found tile (0x%02x) in room %ld (%s)\n", find_tile, i, file.rooms[i].data.name);
                        found = true;
                        break;
                    }
                    if (file.rooms[i].data.tiles[idx] == find_tile + 64) {
                        printf("Found tile+64 (0x%02x) in room %ld (%s)\n", find_tile + 64, i, file.rooms[i].data.name);
                        found = true;
                        break;
                    }
                    if (file.rooms[i].data.tiles[idx] == find_tile + 128) {
                        printf("Found tile+128 (0x%02x) in room %ld (%s)\n", find_tile + 128,  i, file.rooms[i].data.name);
                        found = true;
                        break;
                    }
                    if (file.rooms[i].data.tiles[idx] == find_tile + 192) {
                        printf("Found tile+192 (0x%02x) in room %ld (%s)\n", find_tile + 192,  i, file.rooms[i].data.name);
                        found = true;
                        break;
                    }
                }
            }
        }
        if (!found) {
            printf("Tile 0x%02x (offset 0x%02x) not found\n", find_tile, find_tile_offset);
        }
    }

    if (list) {
        for (size_t i = 0; i < C_ARRAY_LEN(file.rooms); i ++) {
            if (file.rooms[i].valid) {
                printf("%2ld: %s\n", i, file.rooms[i].data.name);
            }
        }
    }

    if (display) {
        if (display_room == -1) {
            for (size_t i = 0; i < C_ARRAY_LEN(file.rooms); i ++) {
                if (file.rooms[i].valid) {
                    dumpRoom(&file.rooms[i]);
                }
            }
        } else {
            if (!file.rooms[display_room].valid) {
                fprintf(stderr, "Room %d is invalid\n", display_room);
                defer_return(1);
            }
            dumpRoom(&file.rooms[display_room]);
        }
    }
    if (patches.length > 0) {
        for (size_t i = 0; i < patches.length; i ++) {
            PatchInstruction patch = patches.data[i];
            if (!file.rooms[patch.room_id].valid) {
                fprintf(stderr, "Room ID %d invalid\n", patch.room_id);
                fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                defer_return(1);
            }
            ARRAY_FREE(file.rooms[patch.room_id].compressed);
            if (patch.address >= sizeof(file.rooms[patch.room_id].data)) {
                if (patch.address - sizeof(file.rooms[patch.room_id].data) >= file.rooms[patch.room_id].rest.length) {
                    fprintf(stderr, "Address %lu invalid\n", patch.address);
                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                    defer_return(1);
                }
                file.rooms[patch.room_id].rest.data[patch.address - sizeof(file.rooms[patch.room_id].data)] = patch.value;
            } else {
                ((uint8_t *)&file.rooms[patch.room_id].data)[patch.address] = patch.value;
            }
        }
    }
    if (patches.length > 0 || recompress) {
        if (recompress) {
            if (recompress_room == -1) {
                for (size_t i = 0; i < C_ARRAY_LEN(file.rooms); i ++) {
                    if (file.rooms[i].valid) ARRAY_FREE(file.rooms[i].compressed);
                }
            } else {
                if (!file.rooms[recompress_room].valid) {
                    fprintf(stderr, "Room %d is invalid\n", display_room);
                    defer_return(1);
                }
                ARRAY_FREE(file.rooms[recompress_room].compressed);
            }
        }
        fp = fopen(fileName, "w");
        if (fp == NULL) {
            fprintf(stderr, "Could not open %s for writing. Check that you have permission.\n",
                    fileName);
            defer_return(1);
        }
        if (!writeFile(&file, fp)) defer_return(1);
        fclose(fp);
        fp = NULL;
        fprintf(stderr, "Written %s\n", fileName);
    }

defer:
#undef defer_return
    ARRAY_FREE(patches);
    freeRoomFile(&file);
    if (fp) { fclose(fp); fp = NULL; }
    return ret;
}
