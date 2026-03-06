#include <assert.h>
#include <errno.h>

#include <ctype.h>

#include <stdint.h>

#include <string.h>

#include <unistd.h>

/* Only for ntohs, perhaps write our own? */
#include <arpa/inet.h>

#include <stdio.h>
long filesize(FILE *fp) {
    long tell = ftell(fp);
    if (fseek(fp, 0, SEEK_END) < 0) return -1;
    long length = ftell(fp);
    if (fseek(fp, tell, SEEK_SET) < 0) return -1;
    return length;
}

#define BOOL_S(b) ((b) ? "true" : "false")

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
    if (room->data.switches) {
        for (size_t i = 0; i < room->data.num_switches; i ++) {
            ARRAY_FREE(room->data.switches[i].chunks);
        }
        free(room->data.switches);
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
    printf("Header:\n");
    _Static_assert(sizeof(Header) == 130, "readHeader expected a different header size");
    printf("  Room offsets: [");
    for (int i = 0; i < 64; i ++) {
        if (i != 0) printf(", ");
        printf("0x%04X", head->definitions[i]);
    }
    printf("]\n");
    printf("  filesize = %u\n", head->filesize);
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

void dumpRoom(Room *room, RoomFile *file, bool color) {
    printf("Room %u: \"%s\"\n", room->index, room->data.name);

    /* printf("  Compressed room (length=%zu): [", room->compressed.length); */
    /* for (size_t i = 0; i < room->compressed.length; i ++) { */
    /*     if (i % 32 == 0) printf("\n    "); */
    /*     if ((i % 32) != 0) printf(" "); */
    /*     printf("%02x", room->compressed.data[i]); */
    /* } */
    /* printf("]\n"); */

    /* printf("  Decompressed room (length=%zu): [", room->decompressed.length); */
    /* for (size_t i = 0; i < room->decompressed.length; i ++) { */
    /*     if (i % 32 == 0) printf("\n    "); */
    /*     if ((i % 32) != 0) printf(" "); */
    /*     printf("%02x", room->decompressed.data[i]); */
    /* } */
    /* printf("]\n"); */

    printf("Tiles:");
    for (size_t i = 0; i < C_ARRAY_LEN(room->data.tiles); i ++) {
        uint8_t x = i % WIDTH_TILES;
        uint8_t y = i / WIDTH_TILES;
        if (x == 0) printf("\n");
        bool colored = false;
        bool obj = false;
        uint8_t tile = room->data.tiles[i];
        /* if (x != 0) printf(" "); */
        for (size_t o = 0; o < room->data.num_objects; o ++) {
            struct RoomObject *object = room->data.objects + o;
            switch (object->type) {
                case BLOCK:
                    if (x >= object->x && x < object->x + object->block.width &&
                        y >= object->y && y < object->y + object->block.height) {

                        if (color) printf("\033[3%ldm", (o % 7) + 1);
                        colored = true;
                        assert(object->tiles != NULL);
                        int o_x = x - object->x;
                        int o_y = y - object->y;
                        tile = object->tiles[o_y * object->block.width + o_x];
                        obj = true;
                    }
                    break;

                case SPRITE:
                    if (x == object->x && y == object->y) {
                        if (color) printf("\033[4%ld;30m", (o % 7) + 1);
                        colored = true;
                        tile = object->sprite.type << 4 | object->sprite.damage;
                        obj = true;
                    }
            }
            if (colored) break;
        }
        for (size_t s = 0; s < room->data.num_switches; s ++) {
            struct SwitchObject *sw = room->data.switches + s;
            assert(sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE);
            if (x == sw->chunks.data[0].x && y == sw->chunks.data[0].y) {
                if (color) printf("\033[4%ld;30m", (s % 3) + 4);
                colored = true;
                break;
            }
            for (size_t c = 1; !colored && c < sw->chunks.length; c ++) {
                struct SwitchChunk *chunk = sw->chunks.data + c;
                if (chunk->type == TOGGLE_BLOCK) {
                    if (chunk->dir == HORIZONTAL) {
                        if (y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size) {
                            if (color) printf("\033[m\033[3%ld;40m", (s % 3) + 4);
                            colored = true;
                            tile = chunk->on;
                        }
                    } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                        if (color) printf("\033[m\033[3%ld;40m", (s % 3) + 4);
                        colored = true;
                        tile = chunk->on;
                    }
                }
            }
        }

        if (tile != BLANK_TILE || (colored && !obj)) printf("%02X", tile);
        else printf("  ");
        if (colored && color) printf("\033[m");
    }
    printf("\n");
    printf("\n");

    printf("bkgrnd: %u, tiles: %u, dmg: %d, gravity (|): %u, gravity (-): %u\n",
            room->data.background, room->data.tile_offset, room->data.room_damage,
            room->data.gravity_vertical, room->data.gravity_horizontal);

#define READ_NEIGHBOUR(id) do { \
neighbour_room = file->rooms[(id)].data; \
assert(C_ARRAY_LEN(neighbour_name) >= C_ARRAY_LEN(neighbour_room.name)); \
strncpy(neighbour_name, neighbour_room.name, C_ARRAY_LEN(neighbour_room.name)); \
for (int i = C_ARRAY_LEN(neighbour_name) - 1; i >= 0; i --) { \
    if (neighbour_name[i] == '\0' || isspace(neighbour_name[i])) { \
        neighbour_name[i] = '\0'; \
    } else { \
        break; \
    } \
} \
} while (0)
    struct DecompressedRoom neighbour_room = {0};
    char neighbour_name[25] = {0};
    printf("left: %u", room->data.room_west);
    READ_NEIGHBOUR(room->data.room_west);
    printf(" - \"%s\"\n", neighbour_name);
    printf("down: %u", room->data.room_south);
    READ_NEIGHBOUR(room->data.room_south);
    printf(" - \"%s\"\n", neighbour_name);
    printf("up: %u", room->data.room_north);
    READ_NEIGHBOUR(room->data.room_north);
    printf(" - \"%s\"\n", neighbour_name);
    printf("right: %u", room->data.room_east);
    READ_NEIGHBOUR(room->data.room_east);
    printf(" - \"%s\"\n", neighbour_name);

    printf("UNKNOWN_b: %d, UNKNOWN_c: %d, UNKNOWN_e: %d, UNKNOWN_f: %d\n",
            room->data.UNKNOWN_b, room->data.UNKNOWN_c,
            room->data._num_switches & 0x3, room->data.UNKNOWN_f);

    printf("  objects (%u):\n", room->data.num_objects);
    for (size_t i = 0; i < room->data.num_objects; i ++) {
        switch (room->data.objects[i].type) {
            case BLOCK:
                printf("    ");
                if (color) printf("\033[3%ld;1m", (i % 3) + 1);
                printf("block %ld: (x,y)=%d,%d (w,h)=%d,%d", i,
                        room->data.objects[i].x, room->data.objects[i].y,
                        room->data.objects[i].block.width, room->data.objects[i].block.height);
                for (size_t y = 0; y < room->data.objects[i].block.height; y ++) {
                    if (color) printf("\033[m");
                    printf("\n        ");
                    if (color) printf("\033[3%ld;1m", (i % 3) + 1);
                    for (size_t x = 0; x < room->data.objects[i].block.width; x ++) {
                        uint8_t tile = room->data.objects[i].tiles[y * room->data.objects[i].block.width + x];
                        if (tile == BLANK_TILE) {
                            if (color) printf("\033[m");
                            printf("  ");
                            if (color) printf("\033[3%ld;1m", (i % 3) + 1);
                        } else {
                            printf("%02x", tile);
                        }
                    }
                }
                break;

            case SPRITE:
                printf("    ");
                if (color) printf("\033[4%ld;30;1m", (i % 3) + 1);
                printf("sprite %ld: (x,y)=%d,%d ", i,
                        room->data.objects[i].x, room->data.objects[i].y);
                _Static_assert(NUM_SPRITE_TYPES == 8, "Unexpected number of sprite types");
                switch (room->data.objects[i].sprite.type) {
                    case SHARK: printf("SHARK"); break;
                    case MUMMY: printf("MUMMY"); break;
                    case BLUE_MAN: printf("BLUE_MAN"); break;
                    case WOLF: printf("WOLF"); break;
                    case R2D2: printf("R2D2"); break;
                    case DINOSAUR: printf("DINOSAUR"); break;
                    case RAT: printf("RAT"); break;
                    case SHOTGUN_LADY: printf("SHOTGUN_LADY"); break;

                    default:
                        fprintf(stderr, "%s:%d: UNREACHABLE: Unexpected sprite type %d\n", __FILE__, __LINE__, room->data.objects[i].sprite.type);
                        exit(1);
                        break;
                }
                printf(" dmg:%d", room->data.objects[i].sprite.damage);
                break;
        }
        if (color) printf("\033[m");
        printf("\n");
    }

    printf("  switches (%u):\n", room->data.num_switches);
    for (size_t i = 0; i < room->data.num_switches; i ++) {
        printf("    ");
        if (color) printf("\033[4%ld;30;1m", (i % 3) + 4);
        assert(room->data.switches[i].chunks.length > 0 && room->data.switches[i].chunks.data[0].type == PREAMBLE);
        struct SwitchChunk *preamble = room->data.switches[i].chunks.data;
        printf("switch %ld: (x,y)=%d,%d (entry)=%s (once)=%s (side)=%s",
                i, preamble->x, preamble->y,
                BOOL_S(preamble->room_entry), BOOL_S(preamble->one_time_use),
                SWITCH_SIDE(preamble->side));
        if (color) printf("\033[m");
        printf("\n");
        if (room->data.switches[i].chunks.length > 1) {
            printf("      chunks:\n");
            for (size_t c = 1; c < room->data.switches[i].chunks.length; c ++) {
                struct SwitchChunk *chunk = room->data.switches[i].chunks.data + c;
                printf("        (%ld) type ", c);
                _Static_assert(NUM_CHUNK_TYPES == 4, "Unexpected number of chunk types");
                switch (chunk->type) {
                    case PREAMBLE:
                        fprintf(stderr, "%s:%d: UNREACHABLE\n", __FILE__, __LINE__);
                        exit(1);
                        break;

                    case TOGGLE_BLOCK:
                        printf("block: (x,y)=%d,%d (%s)=%d (on/off)=%02x/%02x",
                                chunk->x, chunk->y,
                                chunk->dir == VERTICAL ? "height" : "width",
                                chunk->size, chunk->on, chunk->off);
                        break;

                    case TOGGLE_BIT: {
                        char target_name[25] = {0};
                        assert(chunk->room_idx < C_ARRAY_LEN(file->rooms));
                        strncpy(target_name, file->rooms[chunk->room_idx].data.name, C_ARRAY_LEN(target_name) - 1);
                        for (int j = (int)C_ARRAY_LEN(target_name) - 1; j >= 0; j--)
                            if (target_name[j] == '\0' || isspace((unsigned char)target_name[j])) target_name[j] = '\0';
                            else break;
                        printf("toggle: (room) %s (switch) %u (on/off)=%d/%d",
                                target_name, chunk->switch_idx, chunk->on, chunk->off);
                        break;
                    }

                    case TOGGLE_OBJECT:
                        printf("object: idx=%d value=0x%02x test=%d", chunk->index, chunk->value, chunk->test >> 4);
                        break;

                    default:
                        fprintf(stderr, "%s:%d: UNREACHABLE: Unexpected chunk type %d\n", __FILE__, __LINE__, chunk->type);
                        exit(1);
                        break;
                }
                printf("\n");
            }
        }
    }

    printf("  Left over data (length=%zu): [", room->rest.length);
    for (size_t i = 0; i < room->rest.length; i ++) {
        if (i != 0) printf(", ");
        printf("0x%02x", room->rest.data[i]);
    }
    printf("]\n");
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

/* #define log(...) printf(__VA_ARGS__) */
#define log(...) do {} while (false)
#define read_next(dst, fp) { \
        int next_val = fgetc((fp)); \
        if (next_val == EOF) { \
            log("%s:%d: Unexpected end of file @ 0x%lx4x\n", \
                    __FILE__, __LINE__, ftell(fp)); \
            freeRoom(&tmp); \
            return false; \
        } \
        size --; \
        if (isprint(next_val)) log("%s:%d: READ 0x%02x (%hhd, '%c') @ 0x%lx, %zu left, decompressed %zu so far\n", __FILE__, __LINE__, next_val, next_val, next_val, ftell(fp), size, tmp.decompressed.length); \
        else log("%s:%d: READ 0x%02x (%hhd) @ 0x%lx, %zu left, decompressed %zu so far\n", __FILE__, __LINE__, next_val, next_val, ftell(fp), size, tmp.decompressed.length); \
        ARRAY_ADD(tmp.compressed, next_val); \
        dst = next_val; \
}

// The source reads the first two words into 0x1d1d-0x1d20 (including marker and these tile markers)
    uint8_t markers[4];
    log("%s:%d: Starting read at 0x%lx\n", __FILE__, __LINE__, ftell(fp));
    read_next(markers[0], fp);
    if (markers[0] != 0x8F) {
        log("Expected room to start with 0x8F, found %02X\n", markers[0]);
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
                    log("%s:%d: Corrupt RLE state @ 0x%lx4x\n",
                            __FILE__, __LINE__, ftell(fp));
                    freeRoom(&tmp);
                    return false;
                }
                uint8_t copy = tmp.decompressed.data[tmp.decompressed.length - 1];
                log("%s:%d: Copying 0x%02x %d times\n",
                        __FILE__, __LINE__, copy, next + 2);
                for (int i = 0; i < next + 2; i ++) {
                    ARRAY_ADD(tmp.decompressed, copy);
                }
            } else if (next != 0x80) {
                // copy abs(next) bytes from dst - 0x20 to dst
                if (tmp.decompressed.length < 0x20) {
                    log("%s:%d: Corrupt RLE state @ 0x%lx4x\n",
                            __FILE__, __LINE__, ftell(fp));
                    freeRoom(&tmp);
                    return false;
                }
                log("%s:%d: Copying from behind 0x%02x, %d times\n",
                        __FILE__, __LINE__, 0x20, -(int8_t)next);
                for (int i = 0; i < -(int8_t)next; i ++) {
                    uint8_t copy = tmp.decompressed.data[tmp.decompressed.length - 0x20];
                    ARRAY_ADD(tmp.decompressed, copy);
                }
            } else {
                log("%s:%d: Output 0x%02x as is @ %lu\n", __FILE__, __LINE__, val, tmp.decompressed.length);
                ARRAY_ADD(tmp.decompressed, val);
            }
        } else if (val == markers[2]) {
            // LZ
            uint8_t next;
            read_next(next, fp);
            if (next < 0x80) {
                // Store start of room marker next + 2 times
                log("%s:%d: Copying 0x%02x %d times\n",
                        __FILE__, __LINE__, markers[0], next + 2);
                for (int i = 0; i < next + 2; i ++) {
                    ARRAY_ADD(tmp.decompressed, markers[0]);
                }
            } else if (next != 0x80) {
                // copy abs(next) + 1 bytes from dst - read() to dst
                uint8_t back;
                read_next(back, fp);
                if (tmp.decompressed.length < back) {
                    log("%s:%d: Corrupt RLE state @ 0x%lx4x\n",
                            __FILE__, __LINE__, ftell(fp));
                    freeRoom(&tmp);
                    return false;
                }
                log("%s:%d: Copying from behind 0x%02x, %d times\n",
                        __FILE__, __LINE__, back, -(int8_t)next + 1);
                for (int i = 0; i < -(int8_t)next + 1; i ++) {
                    uint8_t copy = tmp.decompressed.data[tmp.decompressed.length - back];
                    ARRAY_ADD(tmp.decompressed, copy);
                }
            } else {
                log("%s:%d: Output 0x%02x as is @ %lu\n", __FILE__, __LINE__, val, tmp.decompressed.length);
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
                    log("%s:%d: Corrupt RLE state @ 0x%lx4x\n",
                            __FILE__, __LINE__, ftell(fp));
                    freeRoom(&tmp);
                    return false;
                }
                log("%s:%d: Copying from behind 0x%02x, %d times\n",
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
                    log("%s:%d: Corrupt RLE state @ 0x%lx4x\n",
                            __FILE__, __LINE__, ftell(fp));
                    freeRoom(&tmp);
                    return false;
                }
                log("%s:%d: Copying from behind 0x%02x, %d times\n",
                        __FILE__, __LINE__, back, -(int8_t)next + 1);
                for (int i = 0; i < -(int8_t)next + 1; i ++) {
                    uint8_t copy = tmp.decompressed.data[tmp.decompressed.length - back];
                    ARRAY_ADD(tmp.decompressed, copy);
                }
            } else {
                log("%s:%d: Output 0x%02x as is @ %lu\n", __FILE__, __LINE__, val, tmp.decompressed.length);
                ARRAY_ADD(tmp.decompressed, val);
            }
        } else {
            // Just add it as is then get next
            log("%s:%d: Output 0x%02x as is @ %lu\n", __FILE__, __LINE__, val, tmp.decompressed.length);
            ARRAY_ADD(tmp.decompressed, val);
        }
    }
    log("%s:%d: Finishing read at 0x%lx\n", __FILE__, __LINE__, ftell(fp));
#undef read_next

    size_t data_idx = 0;
#define read_decompressed(dst, a) { \
        if (data_idx == (a).length) { \
            log("%s:%d: Not enough compressed data at %lu bytes long\n", \
                    __FILE__, __LINE__, (a).length); \
            freeRoom(&tmp); \
            return false; \
        } \
        int next_val = (a).data[data_idx++]; \
        if (isprint(next_val)) log("%s:%d: READ 0x%02x (%hhd, '%c') @ 0x%lx, %zu left\n", __FILE__, __LINE__, next_val, next_val, next_val, data_idx - 1, (a).length - data_idx); \
        else log("%s:%d: READ 0x%02x (%hhd) @ 0x%lx, %zu left\n", __FILE__, __LINE__, next_val, next_val, data_idx - 1, (a).length - data_idx); \
        dst = next_val; \
}

    for (size_t i = 0; i < C_ARRAY_LEN(tmp.data.tiles); i ++) {
        read_decompressed(tmp.data.tiles[i], tmp.decompressed);
    }
    read_decompressed(tmp.data.tile_offset, tmp.decompressed);
    read_decompressed(tmp.data.background, tmp.decompressed);
    read_decompressed(tmp.data.room_north, tmp.decompressed);
    read_decompressed(tmp.data.room_east, tmp.decompressed);
    read_decompressed(tmp.data.room_south, tmp.decompressed);
    read_decompressed(tmp.data.room_west, tmp.decompressed);

    read_decompressed(tmp.data.room_damage, tmp.decompressed);

    read_decompressed(tmp.data.gravity_vertical, tmp.decompressed);
    read_decompressed(tmp.data.gravity_horizontal, tmp.decompressed);

    read_decompressed(tmp.data.UNKNOWN_b, tmp.decompressed);
    read_decompressed(tmp.data.UNKNOWN_c, tmp.decompressed);
    read_decompressed(tmp.data.num_objects, tmp.decompressed);
    read_decompressed(tmp.data._num_switches, tmp.decompressed);
    tmp.data.num_switches = tmp.data._num_switches >> 2;
    read_decompressed(tmp.data.UNKNOWN_f, tmp.decompressed);

    log("%s:%d: Reading name at %04lu\n", __FILE__, __LINE__, data_idx);
    for (size_t i = 0; i < C_ARRAY_LEN(tmp.data.name); i ++) {
        read_decompressed(tmp.data.name[i], tmp.decompressed);
    }

    log("%s:%d: Reading %u moving objects at %04lu\n", __FILE__, __LINE__, tmp.data.UNKNOWN_d, data_idx);
    tmp.data.objects = calloc(tmp.data.num_objects, sizeof(struct RoomObject));
    assert(tmp.data.objects != NULL);
    struct RoomObject *objects = tmp.data.objects;
    for (size_t i = 0; i < tmp.data.num_objects; i ++) {
        uint8_t msb, lsb;
        read_decompressed(msb, tmp.decompressed);
        read_decompressed(lsb, tmp.decompressed);
        if ((msb & 0x80) == 0) {
            objects[i].type = BLOCK;
            objects[i].x = lsb & 0x1f;
            objects[i].y = msb & 0x1f;
            objects[i].block.width = ((lsb & 0xe0) >> 5) + 1;
            objects[i].block.height = ((msb & 0x60) >> 5) + 1;
            assert(objects[i].x < WIDTH_TILES);
            assert(objects[i].block.width < WIDTH_TILES);
            assert(objects[i].x + objects[i].block.width <= WIDTH_TILES);
            if (!(objects[i].y < HEIGHT_TILES)) {
                fprintf(stderr, "WARNING: Room %ld (%s) object %zu y is out of bounds (%u >= %u)\n",
                        idx, tmp.data.name, i, objects[i].y, HEIGHT_TILES);
                continue;
            }
            assert(objects[i].y < HEIGHT_TILES);
            assert(objects[i].block.height < HEIGHT_TILES);
            if (!(objects[i].y + objects[i].block.height <= HEIGHT_TILES)) {
                fprintf(stderr, "WARNING: Room %ld (%s) object %zu y+height is out of bounds (%u >= %u)\n",
                        idx, tmp.data.name, i, objects[i].y + objects[i].block.height, HEIGHT_TILES);
                continue;
            }
            objects[i].tiles = malloc(objects[i].block.width * objects[i].block.height);
            assert(objects[i].tiles != NULL);
            for (size_t y = objects[i].y; y < objects[i].y + objects[i].block.height; y ++) {
                memcpy(
                        objects[i].tiles + (y - objects[i].y) * objects[i].block.width,
                        tmp.data.tiles + TILE_IDX(objects[i].x, y),
                        objects[i].block.width
                      );
                memset(
                        tmp.data.tiles + TILE_IDX(objects[i].x, y),
                        '\0',
                        objects[i].block.width
                      );

            }
        } else {
            objects[i].type = SPRITE;
            objects[i].x = lsb & 0x1f;
            objects[i].y = msb & 0x1f;

            objects[i].sprite.type = (((lsb & 0xe0) >> 5) + 1) % NUM_SPRITE_TYPES;
            objects[i].sprite.damage = ((msb & 0x60) >> 5) + 1;
        }
    }

    log("%s:%d: Reading %u switches at %04lu\n", __FILE__, __LINE__, tmp.data.UNKNOWN_d, data_idx);
    tmp.data.switches = calloc(tmp.data.num_switches, sizeof(struct SwitchObject));
    assert(tmp.data.switches != NULL);
    struct SwitchObject *switches = tmp.data.switches;
    for (size_t i = 0; i < tmp.data.num_switches; i ++) {
        uint8_t msb, lsb;
        read_decompressed(msb, tmp.decompressed);
        read_decompressed(lsb, tmp.decompressed);
        uint8_t y = msb & 0x1f;
        uint8_t x = lsb & 0x1f;
        _Static_assert(NUM_CHUNK_TYPES == 4, "Unexpected number of chunk types");
        ARRAY_ADD(switches[i].chunks, ((struct SwitchChunk){ .type = PREAMBLE, .x = x, .y = y, .room_entry = (lsb & 0x80) == 0x00, .one_time_use = (msb & 0x20) != 0x00, .side = (lsb & 0x60) >> 5, .msb = msb, .lsb = lsb }));

        while (data_idx < tmp.decompressed.length && (tmp.decompressed.data[data_idx] & 0xc0) != 0x00) {
            read_decompressed(msb, tmp.decompressed);
            read_decompressed(lsb, tmp.decompressed);
            switch (msb & 0xc0) {
                case 0x80: {
                    x = lsb & 0x1f;
                    y = msb & 0x1f;
                    uint8_t size = ((lsb >> 5) & 0x7) + 1;
                    uint8_t off, on;
                    enum SwitchChunkDirection dir = ((msb & 0x20) == 0) ? HORIZONTAL : VERTICAL;
                    read_decompressed(off, tmp.decompressed);
                    read_decompressed(on, tmp.decompressed);
                    // All bits accounted for
                    ARRAY_ADD(switches[i].chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK, .x = x, .y = y, .size = size, .dir = dir, .off = off, .on = on }));
                }; break;

                case 0x40: {
                    // Only does this if switch_structs[unknown(f) + i / 3] has {0x2, 0x8, 0x20, 0x80}[i] set
                    uint8_t on = (msb >> 4) & 0x3;
                    uint8_t off = (msb >> 2) & 0x3;
                    uint8_t bitmask = (uint8_t[]){0x01, 0x04, 0x10, 0x40}[msb & 0x3];
                    // removes bit, sets bit << 1
                    // Note that this static array index is based purely on byte, not on switch index
                    uint8_t index = lsb;
                    // This seems to be the index that is set, not the index that is checked
                    ARRAY_ADD(switches[i].chunks, ((struct SwitchChunk){ .type = TOGGLE_BIT, .on = on, .off = off, .bitmask = bitmask, .index = index }));
                }; break;

                case 0xc0: {
                    // Only does this if switch_structs[unknown(f) + i / 3] has {0x2, 0x8, 0x20, 0x80}[i] set
                    // 0b00110000 is tested against *0x1d13
                    // msb & 0xf is index into objects
                    // if object is block, set movertab[2] = lsb
                    // if sprite, set movertab[2] = lsb & 0xf8, maybe | 0x80 if movertab[2] & 0x2 != 0
                    uint8_t test = msb & 0x30;
                    uint8_t index = msb & 0xf;
                    ARRAY_ADD(switches[i].chunks, ((struct SwitchChunk){ .type = TOGGLE_OBJECT, .index = index, .test = test, .value = lsb }));

                }; break;

                default:
                    fprintf(stderr, "%s:%d: UNREACHABLE: Unexpected chunk type 0x%02x\n", __FILE__, __LINE__, msb);
                    exit(1);
            }
        }
    }

    // then stuff that controls enemy placement, switch actions, etc
    while (data_idx != tmp.decompressed.length) {
        uint8_t val;
        read_decompressed(val, tmp.decompressed);
        ARRAY_ADD(tmp.rest, val);
    }

    /* fprintf(stderr, "%s:%d: UNIMPLEMENTED\n", __FILE__, __LINE__); return false; */
#undef read_decompressed
#undef log
    *room = tmp;
    return true;
}

bool readRoomFromFile(Room *room, FILE *fp, const char *filename) {

    assert(fseek(fp, 0L, SEEK_END) == 0);
    long ftold = ftell(fp);
    assert(ftold != -1);
    size_t filesize = ftold;
    assert(fseek(fp, 0L, SEEK_SET) == 0);

    uint8_t *data = malloc(filesize);
    assert(data != NULL);

    if (fread(data, sizeof(uint8_t), filesize, fp) != (size_t)filesize) {
        free(data);
        fprintf(stderr, "Could read file fully: %s: %s", filename, strerror(errno));
        return false;
    }

    size_t tile_idx = 0;
    size_t data_idx = 0;
    uint16_t fullbyte = 0;
    while (tile_idx < sizeof(room->data.tiles)) {
        if (data_idx >= (size_t)filesize) {
            free(data);
            fprintf(stderr, "Could read full tileset from file: %s: Read %ld tiles\n", filename, tile_idx);
            return false;
        }

        char c = data[data_idx++];
        if (c == '\033') {
            if (data_idx >= filesize) {
                free(data);
                fprintf(stderr, "Incomplete escape sequence at EOF: %s\n", filename);
                return false;
            }

            if (data[data_idx] != '[') {
                free(data);
                fprintf(stderr, "Invalid escape sequence at %ld: %s\n", data_idx, filename);
                return false;
            }
            while (data_idx < filesize && !isalpha(data[data_idx])) data_idx ++;
            if (data_idx >= filesize) {
                free(data);
                fprintf(stderr, "Incomplete escape sequence at EOF: %s\n", filename);
                return false;
            }
            data_idx++;
        } else if (c == '\n') {
            if ((fullbyte & 0xFF00) != 0) {
                free(data);
                fprintf(stderr, "Unexpected newline at %ld: %s\n", data_idx - 1, filename);
                return false;
            }
            if (tile_idx == 0 || (data_idx >= 2 && data[data_idx - 2] == '\n')) {
                room->data.tiles[tile_idx++] = 0;
            }
            while (tile_idx < WIDTH_TILES * HEIGHT_TILES && (tile_idx % WIDTH_TILES) != 0) {
                room->data.tiles[tile_idx++] = 0;
            }
            assert(tile_idx % WIDTH_TILES == 0);
        } else {
            if (c != ' ' && !isxdigit(c)) {
                free(data);
                fprintf(stderr, "Invalid hex digit at %ld: %s\n", data_idx - 1, filename);
                return false;
            }

            uint8_t b = 0;
            if (c == ' ') b = 0;
            else if (isdigit(c)) b = c - '0';
            else b = 10 + tolower(c) - 'a';

            if ((fullbyte & 0xFF00) == 0) {
                fullbyte = 0xFF00 | (b << 4);
            } else {
                fullbyte = (fullbyte & 0xF0) | b;
                room->data.tiles[tile_idx++] = fullbyte;
            }
        }
    }

    if (data_idx < filesize) {
        fprintf(stderr, "WARNING: Still more file to read at %ld: %s\n", data_idx, filename);
    }

    free(data);
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

    uint16_t index = 0;
    uint8_t mask = 0;
    uint8_t bitmasks[] = {0x01, 0x04, 0x10, 0x40};

    for (size_t idx = 0; idx < C_ARRAY_LEN(head.definitions); idx ++) {
        Room *r = file->rooms + idx;
        if (!r->valid) continue;
        for (size_t sw = 0; sw < r->data.num_switches; sw ++) {
            for (size_t _idx = 0; _idx < C_ARRAY_LEN(head.definitions); _idx ++) {
                Room *_r = file->rooms + _idx;
                if (!_r->valid) continue;
                for (size_t _sw = 0; _sw < _r->data.num_switches; _sw ++) {
                    struct SwitchObject *switcch = _r->data.switches + _sw;
                    for (size_t c = 1; c < switcch->chunks.length; c ++) {
                        struct SwitchChunk *chunk = switcch->chunks.data + c;
                        if (chunk->type != TOGGLE_BIT) continue;
                        if (chunk->index == index && chunk->bitmask == bitmasks[mask]) {
                            chunk->room_idx = idx;
                            chunk->switch_idx = sw;
                        }
                    }
                }
            }
            mask ++;
            if (mask == C_ARRAY_LEN(bitmasks)) {
                mask = 0;
                index ++;
            }
        }
    }

    return true;
}

#define MAX_ROOM_FILE_SIZE 0x3100
// there is also a MAX_ROOM_SIZE, unknown yet, add a few switches to midnight and it will corrupt

bool writeRoom(Room *room, FILE *fp) {
    if (fp == NULL || room == NULL || !room->valid) return false;

    room->compressed.length = 0;

    /* fprintf(stderr, "Compressing and writing Room %d \"%s\" at %ld.\n", room->index, room->data.name, ftell(fp)); */

/* #define log(...) printf(__VA_ARGS__) */
#define log(...) do {} while (false)
    uint8_t decompressed[960] = {0};
    uint8_t compressed[960] = {0};
    size_t d_idx = 0;
    size_t d_len = 0;
    size_t c_len = 0;

    /* Build a serialisation tile array without modifying room->data.tiles.
     * Overlay objects in reverse order so lower-indexed objects take priority
     * on any overlapping regions — matching readRoom's forward-read-then-zero
     * behaviour, which prevents tile corruption when two block objects overlap. */
    uint8_t ser_tiles[WIDTH_TILES * HEIGHT_TILES];
    memcpy(ser_tiles, room->data.tiles, sizeof(ser_tiles));
    if (room->data.num_objects > 0) {
        for (size_t ii = room->data.num_objects; ii-- > 0; ) {
            struct RoomObject *obj = room->data.objects + ii;
            if (obj->type == SPRITE || obj->tiles == NULL) continue;
            assert(obj->y + obj->block.height <= HEIGHT_TILES);
            for (size_t y = obj->y; y < obj->y + obj->block.height; y++) {
                memcpy(
                        ser_tiles + TILE_IDX(obj->x, y),
                        obj->tiles + (y - obj->y) * obj->block.width,
                        obj->block.width
                      );
            }
        }
    }

    room->data._num_switches = (room->data.num_switches << 2) | (room->data._num_switches & 0x3);

    for (size_t i = 0; i < C_ARRAY_LEN(room->data.tiles); i ++) {
        decompressed[d_len++] = ser_tiles[i];
    }
    decompressed[d_len++] = room->data.tile_offset;
    decompressed[d_len++] = room->data.background;
    decompressed[d_len++] = room->data.room_north;
    decompressed[d_len++] = room->data.room_east;
    decompressed[d_len++] = room->data.room_south;
    decompressed[d_len++] = room->data.room_west;

    decompressed[d_len++] = room->data.room_damage;

    decompressed[d_len++] = room->data.gravity_vertical;
    decompressed[d_len++] = room->data.gravity_horizontal;

    decompressed[d_len++] = room->data.UNKNOWN_b;
    decompressed[d_len++] = room->data.UNKNOWN_c;
    decompressed[d_len++] = room->data.num_objects;
    decompressed[d_len++] = room->data._num_switches;
    decompressed[d_len++] = room->data.UNKNOWN_f;

    for (size_t i = 0; i < C_ARRAY_LEN(room->data.name); i ++) {
        decompressed[d_len++] = room->data.name[i];
    }

    assert(d_len + 2 * room->data.num_objects < C_ARRAY_LEN(decompressed));
    for (size_t i = 0; i < room->data.num_objects; i ++) {
        // FIXME depending on type, first byte should have 0x80
        switch (room->data.objects[i].type) {
            case BLOCK:
                decompressed[d_len++] = room->data.objects[i].y | ((room->data.objects[i].block.height - 1) << 5);
                decompressed[d_len++] = room->data.objects[i].x | ((room->data.objects[i].block.width - 1) << 5);
                break;

            case SPRITE:
                decompressed[d_len++] = 0x80 | room->data.objects[i].y | ((room->data.objects[i].sprite.damage - 1) << 5);
                decompressed[d_len++] = room->data.objects[i].x | ((room->data.objects[i].sprite.type - 1) << 5);
                break;

        }
    }
    for (size_t i = 0; i < room->data.num_switches; i ++) {
        struct SwitchObject *sw = room->data.switches + i;
        assert(sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE);
        for (size_t c = 0; c < sw->chunks.length; c ++) {
            struct SwitchChunk *chunk = sw->chunks.data + c;
            _Static_assert(NUM_CHUNK_TYPES == 4, "Unexpected number of chunk types");
            switch (chunk->type) {
                case PREAMBLE:
                    assert(c == 0);
                    decompressed[d_len++] = (chunk->y & 0x1f) | (chunk->one_time_use ? 0x20 : 0x00);
                    decompressed[d_len++] = (chunk->x & 0x1f) | (chunk->room_entry ? 0x00 : 0x80) | (chunk->side << 5);
                    break;

                case TOGGLE_BLOCK:
                    decompressed[d_len++] = 0x80 | (chunk->y & 0x1f) | (chunk->dir == VERTICAL ? 0x20 : 0);
                    decompressed[d_len++] = (chunk->x & 0x1f) | ((chunk->size - 1) << 5);
                    decompressed[d_len++] = chunk->off;
                    decompressed[d_len++] = chunk->on;
                    break;

                case TOGGLE_BIT: {
                    uint8_t mask_idx = 4;
                    switch(chunk->bitmask) {
                        case 0x01: mask_idx = 0; break;
                        case 0x04: mask_idx = 1; break;
                        case 0x10: mask_idx = 2; break;
                        case 0x40: mask_idx = 3; break;
                    }
                    assert(mask_idx < 4);
                    decompressed[d_len++] = 0x40 | ((chunk->on & 0x3) << 4) | ((chunk->off & 0x3) << 2) | (mask_idx & 0x3);
                    decompressed[d_len++] = chunk->index;
                }; break;

                case TOGGLE_OBJECT:
                    decompressed[d_len++] = 0xc0 | (chunk->test & 0x30) | (chunk->index & 0xf);
                    decompressed[d_len++] = chunk->value;
                    break;

                default:
                    fprintf(stderr, "%s:%d: UNREACHABLE: Unexpected chunk type %d\n", __FILE__, __LINE__, chunk->type);
                    exit(1);
                    break;
            }
        }
    }
    // then stuff that controls enemy placement, switch actions, etc
    assert(d_len + room->rest.length < C_ARRAY_LEN(decompressed));
    for (size_t i = 0; i < room->rest.length; i ++) {
        decompressed[d_len++] = room->rest.data[i];
    }

    assert(d_len < C_ARRAY_LEN(decompressed));
    log("Compressing %ld bytes of data\n", d_len);

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
        log("%s:%d: Compressing byte %x @ %lu. Compressed so far %lu.\n",
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
            log("%s:%d: Fixed size 0x20 block, length %u (%x).\n",
                    __FILE__, __LINE__, length, -(int8_t)length);
            if (length > 2 && (best < 3 || (best - 3) < (length - 2))) {
                compressed[c_len++] = compressed[1];
                compressed[c_len++] = -(int8_t)length;
                d_idx += length - 1; // We had already read 1 at start of loop
                continue;
            }
        }
        length = 1;
        if (d_idx >= 2 && byte == decompressed[d_idx - 2]) {
            while (d_idx + length - 1 < d_len && length - 2 < 0x7e) {
                if (decompressed[d_idx + length - 1] != byte) break;
                length ++;
            }
            if (length > 2 && (best < 3 || (best - 3) < (length - 2))) {
                // 2- Output RLE encoded byte, send marker[1] and length < 0x80. Copies last decompressed byte length + 2 times
                log("%s:%d: RLE length %u (%x).\n",
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
                log("%s:%d: RLE marker[0] length %u (%x).\n",
                        __FILE__, __LINE__, length, length - 2);
                compressed[c_len++] = compressed[2];
                compressed[c_len++] = length - 2;
                d_idx += length - 1; // We had already read 1 at start of loop
                continue;
            }
        } 
        if (best > 3) { // Is it worth it to compress?
            if (best_back < 0x100) {
                // 6- LZ Output, send marker[2] and (int8_t)length < 0 != 0x80 and backindex. Copy last backindex bytes -(int8_t)length + 1 times
                log("%s:%d: LZ length %u (%x), back %u (%x).\n",
                        __FILE__, __LINE__, best, -(int8_t)(best - 1), best_back, best_back);
                compressed[c_len++] = compressed[2];
                compressed[c_len++] = -(int8_t)(best - 1);
                compressed[c_len++] = (uint8_t)(best_back & 0xFF);
                d_idx += best - 1; // We already read 1 at start of loop
                continue;
            } else if (best_back < 0x200) {
                // 9- LZ Mid output, send marker[3] and (int8_t)length < 0 != 0x80 and backindex. Copy 0x100 | backindex bytes -(int8_t)length + 1 times
                log("%s:%d: LZ Mid length %u (%x), back %u (%x).\n",
                        __FILE__, __LINE__, best, -(int8_t)(best - 1), best_back, best_back);
                compressed[c_len++] = compressed[3];
                compressed[c_len++] = -(int8_t)(best - 1);
                compressed[c_len++] = (uint8_t)(best_back & 0xFF);
                d_idx += best - 1; // We already read 1 at start of loop
                continue;
            } else if (best_back < 0x300) {
                // 8- LZ Far output, send marker[3] and length < 0x80 and backindex. Copy 0x200 | backindex bytes length + 2 times
                log("%s:%d: LZ Far length %u (%x), back %u (%x).\n",
                        __FILE__, __LINE__, best, best - 2, best_back, best_back);
                compressed[c_len++] = compressed[3];
                compressed[c_len++] = best - 2;
                compressed[c_len++] = (uint8_t)(best_back & 0xFF);
                d_idx += best - 1; // We already read 1 at start of loop
                continue;
            }
        }

        log("%s:%d: Output as is (or escaped if one of markers).\n", __FILE__, __LINE__);
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

    log("Compressed %ld bytes of data into %ld bytes\n", d_len, c_len);
    assert(c_len < C_ARRAY_LEN(compressed));

    /* printf("  Data to compress (length=%zu): [", d_len); */
    /* for (size_t i = 0; i < d_len; i ++) { */
    /*     if (i % 32 == 0) printf("\n    "); */
    /*     if ((i % 32) != 0) printf(" "); */
    /*     printf("%02x", decompressed[i]); */
    /* } */
    /* printf("]\n"); */

    /* printf("  Newly compressed room (length=%zu): [", c_len); */
    /* for (size_t i = 0; i < c_len; i ++) { */
    /*     if (i % 32 == 0) printf("\n    "); */
    /*     if ((i % 32) != 0) printf(" "); */
    /*     printf("%02x", compressed[i]); */
    /* } */
    /* printf("]\n"); */

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
        /* printf("%s:%d: Wrote room %ld \"%s\" @ 0x%2lx length %ld\n", */
        /*        __FILE__, __LINE__, i, file->rooms[i].data.name, offset, ftell(fp) - offset); */

        offset = ftell(fp);
    }
    head.filesize = htons(ftell(fp));

    if (fseek(fp, 0, SEEK_SET) == -1) return false;
    if (fwrite(&head, sizeof(Header), 1, fp) != 1) return false;
    return true;
}

static void strip_ansi(char *line) {
    char *r = line, *w = line;
    while (*r) {
        if (*r == '\033' && *(r+1) == '[') {
            r += 2;
            while (*r && !isalpha((unsigned char)*r)) r++;
            if (*r) r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static uint8_t parse_hex_pair(char a, char b) {
    uint8_t hi = isdigit((unsigned char)a) ? (uint8_t)(a - '0') : (uint8_t)(10 + tolower((unsigned char)a) - 'a');
    uint8_t lo = isdigit((unsigned char)b) ? (uint8_t)(b - '0') : (uint8_t)(10 + tolower((unsigned char)b) - 'a');
    return (hi << 4) | lo;
}

// Recompute chunk->index and chunk->bitmask for all TOGGLE_BIT chunks from
// chunk->room_idx / chunk->switch_idx, using the current global switch ordering.
// Called after importRooms (since import may not have the original file for lookup).
bool recomputeToggleBits(RoomFile *file) {
    uint8_t bitmasks[] = {0x01, 0x04, 0x10, 0x40};
    for (size_t idx = 0; idx < C_ARRAY_LEN(file->rooms); idx++) {
        Room *r = file->rooms + idx;
        if (!r->valid) continue;
        for (size_t sw = 0; sw < r->data.num_switches; sw++) {
            struct SwitchObject *switcch = r->data.switches + sw;
            for (size_t c = 1; c < switcch->chunks.length; c++) {
                struct SwitchChunk *chunk = switcch->chunks.data + c;
                if (chunk->type != TOGGLE_BIT) continue;
                uint16_t index = 0; uint8_t mask = 0;
                for (size_t _idx = 0; _idx < C_ARRAY_LEN(file->rooms); _idx++) {
                    Room *_r = file->rooms + _idx;
                    if (!_r->valid) continue;
                    for (size_t _sw = 0; _sw < _r->data.num_switches; _sw++) {
                        if (chunk->room_idx == _idx && chunk->switch_idx == _sw) {
                            chunk->index   = index;
                            chunk->bitmask = bitmasks[mask];
                            goto tb_found;
                        }
                        if (++mask == C_ARRAY_LEN(bitmasks)) { mask = 0; index++; }
                    }
                }
                fprintf(stderr, "recomputeToggleBits: room %u switch %u not found\n",
                        chunk->room_idx, chunk->switch_idx);
                return false;
tb_found:       ;
            }
        }
    }
    return true;
}

// force_room_id >= 0: import the first room found into that slot (ignore file's Room N id).
// force_room_id < 0: use the room id from the "Room N @..." header.
bool importRooms(FILE *fp, RoomFile *file, int force_room_id) {
    char line[1024];
    int room_id = -1;
    Room *room = NULL;
    int tile_row = -1;
    int obj_idx = -1;
    int obj_tile_row = -1;
    int sw_idx = -1;
    bool in_chunks = false;

    // Deferred TOGGLE_BIT entries: chunk pointers with the room name and switch_idx to resolve
    // after all rooms have been imported (handles forward references and blank-first imports).
    typedef struct { struct SwitchChunk *chunk; char room_name[64]; unsigned switch_idx; } DeferredTB;
    DeferredTB deferred[1024];
    int n_deferred = 0;

    while (fgets(line, sizeof(line), fp)) {
        strip_ansi(line);
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';

        // Room header: "Room N @0xADDR: "name""
        unsigned uid;
        if (sscanf(line, "Room %u:", &uid) == 1 && uid < 64) {
            // If force_room_id is set and we already imported one room, stop.
            if (force_room_id >= 0 && room != NULL) break;
            room_id = (force_room_id >= 0) ? force_room_id : (int)uid;
            room = &file->rooms[room_id];
            room->valid = true;
            room->index = (uint8_t)room_id;
            // Parse name from header: Room N @ADDR: "NAME"
            memset(room->data.name, ' ', sizeof(room->data.name));
            char *q = strchr(line, '"');
            if (q) {
                q++; // skip opening quote
                char *eq = strchr(q, '"');
                size_t nlen = eq ? (size_t)(eq - q) : strlen(q);
                if (nlen > sizeof(room->data.name)) nlen = sizeof(room->data.name);
                memcpy(room->data.name, q, nlen);
            }
            // Free dynamic data; fixed fields (tiles, metadata, name) will be overwritten below
            if (room->data.objects) {
                for (size_t i = 0; i < room->data.num_objects; i++)
                    if (room->data.objects[i].tiles) free(room->data.objects[i].tiles);
                free(room->data.objects);
                room->data.objects = NULL;
            }
            room->data.num_objects = 0;
            if (room->data.switches) {
                for (size_t i = 0; i < room->data.num_switches; i++)
                    ARRAY_FREE(room->data.switches[i].chunks);
                free(room->data.switches);
                room->data.switches = NULL;
            }
            room->data.num_switches = 0;
            room->data._num_switches = 0;
            ARRAY_FREE(room->rest);
            ARRAY_FREE(room->compressed);
            tile_row = -1; obj_idx = -1; obj_tile_row = -1; sw_idx = -1; in_chunks = false;
            continue;
        }

        if (room == NULL) continue;

        // Tiles label
        if (strcmp(line, "Tiles:") == 0) {
            tile_row = 0;
            memset(room->data.tiles, 0, sizeof(room->data.tiles));
            continue;
        }

        // Reading tile rows (HEIGHT_TILES rows, 2 chars per tile)
        if (tile_row >= 0 && tile_row < HEIGHT_TILES) {
            for (int x = 0; x < WIDTH_TILES; x++) {
                char a = (size_t)(x*2)   < len ? line[x*2]   : ' ';
                char b = (size_t)(x*2+1) < len ? line[x*2+1] : ' ';
                room->data.tiles[tile_row * WIDTH_TILES + x] =
                    (a == ' ' && b == ' ') ? 0 : parse_hex_pair(a, b);
            }
            tile_row++;
            if (tile_row == HEIGHT_TILES) tile_row = -1;
            continue;
        }

        // Empty line
        if (len == 0) continue;

        // Metadata: "bkgrnd: B, tiles: T, dmg: D, gravity (|): V, gravity (-): H"
        unsigned bkgrnd, tiles_v, grav_v, grav_h; int dmg;
        if (sscanf(line, "bkgrnd: %u, tiles: %u, dmg: %d, gravity (|): %u, gravity (-): %u",
                   &bkgrnd, &tiles_v, &dmg, &grav_v, &grav_h) == 5) {
            room->data.background = bkgrnd;
            room->data.tile_offset = tiles_v;
            room->data.room_damage = dmg;
            room->data.gravity_vertical = grav_v;
            room->data.gravity_horizontal = grav_h;
            continue;
        }

        // Neighbor links
        unsigned nb;
        if (sscanf(line, "left: %u",  &nb) == 1) { room->data.room_west  = nb; continue; }
        if (sscanf(line, "down: %u",  &nb) == 1) { room->data.room_south = nb; continue; }
        if (sscanf(line, "up: %u",    &nb) == 1) { room->data.room_north = nb; continue; }
        if (sscanf(line, "right: %u", &nb) == 1) { room->data.room_east  = nb; continue; }

        // UNKNOWN fields
        int ub, uc, ue, uf;
        if (sscanf(line, "UNKNOWN_b: %d, UNKNOWN_c: %d, UNKNOWN_e: %d, UNKNOWN_f: %d",
                   &ub, &uc, &ue, &uf) == 4) {
            room->data.UNKNOWN_b = ub;
            room->data.UNKNOWN_c = uc;
            room->data._num_switches = (room->data._num_switches & ~0x3) | (ue & 0x3);
            room->data.UNKNOWN_f = uf;
            continue;
        }

        // Objects header: "  objects (N):"
        unsigned num_obj;
        if (sscanf(line, "  objects (%u):", &num_obj) == 1) {
            room->data.num_objects = num_obj;
            room->data.objects = num_obj ? calloc(num_obj, sizeof(struct RoomObject)) : NULL;
            assert(room->data.objects != NULL || num_obj == 0);
            obj_idx = -1;
            continue;
        }

        // Block object tile rows (8-space indent, only while obj_tile_row >= 0)
        if (obj_tile_row >= 0 && len >= 8 && memcmp(line, "        ", 8) == 0) {
            struct RoomObject *obj = &room->data.objects[obj_idx];
            char *p = line + 8;
            size_t plen = len - 8;
            for (int x = 0; x < obj->block.width; x++) {
                char a = (size_t)(x*2)   < plen ? p[x*2]   : ' ';
                char b = (size_t)(x*2+1) < plen ? p[x*2+1] : ' ';
                obj->tiles[obj_tile_row * obj->block.width + x] =
                    (a == ' ' && b == ' ') ? 0 : parse_hex_pair(a, b);
            }
            obj_tile_row++;
            if (obj_tile_row >= obj->block.height) obj_tile_row = -1;
            continue;
        }
        obj_tile_row = -1;

        // Block object header: "    block I: (x,y)=X,Y (w,h)=W,H"
        long bi; int bx, by, bw, bh;
        if (sscanf(line, "    block %ld: (x,y)=%d,%d (w,h)=%d,%d", &bi, &bx, &by, &bw, &bh) == 5) {
            obj_idx = (int)bi;
            if (obj_idx < 0 || obj_idx >= (int)room->data.num_objects) {
                fprintf(stderr, "import: block %d out of range (num_objects=%u)\n", obj_idx, room->data.num_objects);
                return false;
            }
            struct RoomObject *obj = &room->data.objects[obj_idx];
            obj->type = BLOCK;
            obj->x = bx; obj->y = by;
            obj->block.width = bw; obj->block.height = bh;
            obj->tiles = calloc((size_t)(bw * bh), 1);
            assert(obj->tiles != NULL);
            obj_tile_row = 0;
            continue;
        }

        // Sprite object: "    sprite I: (x,y)=X,Y TYPENAME dmg:D"
        long si; int sx, sy, sdmg; char stype[32];
        if (sscanf(line, "    sprite %ld: (x,y)=%d,%d %31s dmg:%d", &si, &sx, &sy, stype, &sdmg) == 5) {
            obj_idx = (int)si;
            if (obj_idx < 0 || obj_idx >= (int)room->data.num_objects) {
                fprintf(stderr, "import: sprite %d out of range (num_objects=%u)\n", obj_idx, room->data.num_objects);
                return false;
            }
            struct RoomObject *obj = &room->data.objects[obj_idx];
            obj->type = SPRITE;
            obj->x = sx; obj->y = sy;
            obj->sprite.damage = sdmg;
            _Static_assert(NUM_SPRITE_TYPES == 8, "Unexpected number of sprite types");
            if      (strcmp(stype, "SHARK")        == 0) obj->sprite.type = SHARK;
            else if (strcmp(stype, "MUMMY")        == 0) obj->sprite.type = MUMMY;
            else if (strcmp(stype, "BLUE_MAN")     == 0) obj->sprite.type = BLUE_MAN;
            else if (strcmp(stype, "WOLF")         == 0) obj->sprite.type = WOLF;
            else if (strcmp(stype, "R2D2")         == 0) obj->sprite.type = R2D2;
            else if (strcmp(stype, "DINOSAUR")     == 0) obj->sprite.type = DINOSAUR;
            else if (strcmp(stype, "RAT")          == 0) obj->sprite.type = RAT;
            else if (strcmp(stype, "SHOTGUN_LADY") == 0) obj->sprite.type = SHOTGUN_LADY;
            else {
                fprintf(stderr, "import: unknown sprite type '%s'\n", stype);
                return false;
            }
            continue;
        }

        // Switches header: "  switches (N):"
        unsigned num_sw;
        if (sscanf(line, "  switches (%u):", &num_sw) == 1) {
            room->data.num_switches = num_sw;
            room->data._num_switches = (num_sw << 2) | (room->data._num_switches & 0x3);
            room->data.switches = num_sw ? calloc(num_sw, sizeof(struct SwitchObject)) : NULL;
            assert(room->data.switches != NULL || num_sw == 0);
            sw_idx = -1; in_chunks = false;
            continue;
        }

        // Switch header: "    switch I: (x,y)=X,Y (entry)=E (once)=O (side)=S"
        long swi; int swx, swy; char entry_s[8], once_s[8], side_s[8];
        if (sscanf(line, "    switch %ld: (x,y)=%d,%d (entry)=%7s (once)=%7s (side)=%7s",
                   &swi, &swx, &swy, entry_s, once_s, side_s) == 6) {
            sw_idx = (int)swi;
            if (sw_idx < 0 || sw_idx >= (int)room->data.num_switches) {
                fprintf(stderr, "import: switch %d out of range (num_switches=%u)\n", sw_idx, room->data.num_switches);
                return false;
            }
            bool room_entry    = strncmp(entry_s, "true", 4) == 0;
            bool one_time_use  = strncmp(once_s,  "true", 4) == 0;
            _Static_assert(NUM_SIDES == 4, "Unexpected number of switch sides");
            enum SwitchSide side = TOP;
            if      (strncmp(side_s, "RIGHT",  5) == 0) side = RIGHT;
            else if (strncmp(side_s, "BOTTOM", 6) == 0) side = BOTTOM;
            else if (strncmp(side_s, "LEFT",   4) == 0) side = LEFT;
            struct SwitchObject *sw = &room->data.switches[sw_idx];
            ARRAY_FREE(sw->chunks);
            ARRAY_ADD(sw->chunks, ((struct SwitchChunk){
                .type = PREAMBLE, .x = swx, .y = swy,
                .room_entry = room_entry, .one_time_use = one_time_use, .side = side
            }));
            in_chunks = false;
            continue;
        }

        // Chunks label
        if (strcmp(line, "      chunks:") == 0) { in_chunks = true; continue; }

        // Chunk line: "        (N) type ..."
        if (in_chunks && sw_idx >= 0 && len > 9 && memcmp(line, "        (", 9) == 0) {
            struct SwitchObject *sw = &room->data.switches[sw_idx];
            char *type_p = strstr(line, ") type ");
            if (!type_p) continue;
            type_p += 7; // skip ") type "

            _Static_assert(NUM_CHUNK_TYPES == 4, "Unexpected number of chunk types");
            if (strncmp(type_p, "block: ", 7) == 0) {
                // TOGGLE_BLOCK: "(x,y)=X,Y (width|height)=S (on/off)=HH/HH"
                int cx, cy, csize; unsigned con, coff; char dir_s[8];
                if (sscanf(type_p, "block: (x,y)=%d,%d (%7[^)])=%d (on/off)=%x/%x",
                           &cx, &cy, dir_s, &csize, &con, &coff) == 6) {
                    enum SwitchChunkDirection dir = strncmp(dir_s, "height", 6) == 0 ? VERTICAL : HORIZONTAL;
                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){
                        .type = TOGGLE_BLOCK, .x = cx, .y = cy, .size = csize,
                        .dir = dir, .on = (uint8_t)con, .off = (uint8_t)coff
                    }));
                }
            } else if (strncmp(type_p, "toggle: ", 8) == 0) {
                // TOGGLE_BIT: "(room) NAME (switch) IDX (on/off)=D/D"
                // Parse room name (may contain spaces) by finding " (switch) "
                const char *p = type_p + 8;
                if (strncmp(p, "(room) ", 7) != 0) continue;
                p += 7;
                const char *sw_marker = strstr(p, " (switch) ");
                if (!sw_marker) continue;
                size_t name_len = (size_t)(sw_marker - p);
                char room_name[64] = {0};
                if (name_len >= sizeof(room_name)) name_len = sizeof(room_name) - 1;
                memcpy(room_name, p, name_len);
                unsigned switch_idx; int ton, toff;
                if (sscanf(sw_marker + 10, "%u (on/off)=%d/%d", &switch_idx, &ton, &toff) != 3) continue;
                // Defer name→room_id resolution until all rooms are parsed
                ARRAY_ADD(sw->chunks, ((struct SwitchChunk){
                    .type = TOGGLE_BIT, .on = ton, .off = toff,
                }));
                if (n_deferred < (int)(sizeof(deferred)/sizeof(deferred[0]))) {
                    deferred[n_deferred].chunk = sw->chunks.data + sw->chunks.length - 1;
                    deferred[n_deferred].switch_idx = switch_idx;
                    if (name_len >= sizeof(deferred[n_deferred].room_name))
                        name_len = sizeof(deferred[n_deferred].room_name) - 1;
                    memcpy(deferred[n_deferred].room_name, room_name, name_len);
                    deferred[n_deferred].room_name[name_len] = '\0';
                    n_deferred++;
                }
            } else if (strncmp(type_p, "object: ", 8) == 0) {
                // TOGGLE_OBJECT: "idx=I value=0xHH test=N"
                int oidx, otest; unsigned oval;
                if (sscanf(type_p, "object: idx=%d value=0x%x test=%d", &oidx, &oval, &otest) == 3) {
                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){
                        .type = TOGGLE_OBJECT, .index = (uint8_t)oidx,
                        .value = (uint8_t)oval, .test = (uint8_t)(otest << 4)
                    }));
                }
            }
            continue;
        }

        // Rest data: "  Left over data (length=N): [0xHH, ...]"
        if (strncmp(line, "  Left over data", 16) == 0) {
            ARRAY_FREE(room->rest);
            char *p = strchr(line, '[');
            if (p) {
                p++;
                while (*p && *p != ']') {
                    unsigned val;
                    if (sscanf(p, "0x%x", &val) == 1)
                        ARRAY_ADD(room->rest, (uint8_t)val);
                    p = strchr(p, ',');
                    if (p) p++; else break;
                }
            }
            continue;
        }
    }

    // Resolve deferred TOGGLE_BIT entries now that all rooms have been imported
    for (int d = 0; d < n_deferred; d++) {
        DeferredTB *dt = &deferred[d];
        uint16_t target_room = 0xFFFF;
        for (size_t r = 0; r < C_ARRAY_LEN(file->rooms); r++) {
            if (!file->rooms[r].valid) continue;
            char trimmed[25] = {0};
            strncpy(trimmed, file->rooms[r].data.name, 24);
            for (int j = 23; j >= 0; j--)
                if (trimmed[j] == ' ' || trimmed[j] == '\0') trimmed[j] = '\0'; else break;
            if (strcmp(trimmed, dt->room_name) == 0) { target_room = (uint16_t)r; break; }
        }
        if (target_room == 0xFFFF) {
            fprintf(stderr, "import: TOGGLE_BIT target room '%s' not found\n", dt->room_name);
            return false;
        }
        dt->chunk->room_idx = target_room;
        dt->chunk->switch_idx = dt->switch_idx;
        // chunk->index and chunk->bitmask are set by recomputeToggleBits() before writing
    }

    return true;
}

bool readRooms(RoomFile *file) {
    FILE *fp = fopen(ROOMS_FILE, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open %s for reading.\n", ROOMS_FILE);
        return false;
    }
    bool ret = readFile(file, fp);
    fclose(fp);
    return ret;
}

bool writeRooms(RoomFile *file) {
    for (size_t idx = 0; idx < C_ARRAY_LEN(file->rooms); idx ++) {
        Room *r = file->rooms + idx;
        if (!r->valid) continue;
        for (size_t sw = 0; sw < r->data.num_switches; sw ++) {
            struct SwitchObject *switcch = r->data.switches + sw;
            for (size_t c = 1; c < switcch->chunks.length; c ++) {
                struct SwitchChunk *chunk = switcch->chunks.data + c;
                if (chunk->type != TOGGLE_BIT) continue;
                uint16_t index = 0;
                uint8_t mask = 0;
                uint8_t bitmasks[] = {0x01, 0x04, 0x10, 0x40};

                for (size_t _idx = 0; _idx < C_ARRAY_LEN(file->rooms); _idx ++) {
                    Room *_r = file->rooms + _idx;
                    if (!_r->valid) continue;
                    for (size_t _sw = 0; _sw < _r->data.num_switches; _sw ++) {
                        if (chunk->room_idx == _idx && chunk->switch_idx == _sw) {
                            chunk->index = index;
                            chunk->bitmask = bitmasks[mask];
                            goto next;
                        }
                        mask ++;
                        if (mask == C_ARRAY_LEN(bitmasks)) {
                            mask = 0;
                            index ++;
                        }
                    }
                }
                if (chunk->room_idx == 0 && chunk->switch_idx == 0) {
                    chunk->index = 0;
                    chunk->bitmask = 0x1;
                    goto next;
                }
                fprintf(stderr, "Could not find switch for room %lu switch %lu chunk %lu pointing to room %u switch %u\n", idx, sw, c, chunk->room_idx, chunk->switch_idx);
                return false;
next:
                {}
            }
        }
    }

    FILE *fp = fopen(ROOMS_FILE, "wb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open %s for writing.\n", ROOMS_FILE);
        return false;
    }
    bool ret = writeFile(file, fp);
    fclose(fp);
    return ret;
}
