#include "array.h"
#include "room.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#define DEPRECATED(str) do { fprintf(stderr, "%s:%d: DEPRECATED: %s", __FILE__, __LINE__, (str)); } while (0)
#define UNIMPLEMENTED(str) do { fprintf(stderr, "%s:%d: UNIMPLEMENTED: %s", __FILE__, __LINE__, (str)); } while (0)

typedef enum {
    NORMAL,
    OBJECT,
    SWITCH,
    TILESET,
    OBJECT_TILESET,

    NUM_PATCH_TYPES // _Static_asserts depend on this being the last entry
} PatchType;

typedef struct {
    PatchType type;
    uint8_t room_id;
    int address;
    uint16_t value;
    int object_id;
    bool delete;
    char *filename;
} PatchInstruction;

typedef ARRAY(PatchInstruction) PatchInstructionArray;

bool main_patch(int *argc, char ***argv, char *program, RoomFile *file, PatchInstructionArray *patches) {
    char *end = NULL;
    char *last = NULL;
    if (*argc <= 3) {
        fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
        return false;
    }
    long room_id = strtol((*argv)[1], &end, 0);
    if (errno == EINVAL || end == NULL || *end != '\0') {
        fprintf(stderr, "Invalid number: %s\n", (*argv)[1]);
        fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
        return false;
    }
    if (room_id < 0 || (unsigned)room_id >= C_ARRAY_LEN(file->rooms)) {
        fprintf(stderr, "Room ID out of range 0..%lu\n", C_ARRAY_LEN(file->rooms) - 1);
        fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
        return false;
    }
    *argv += 2;
    *argc -= 2;
    while (*argc >= 2) {
        long addr = strtol((*argv)[0], &end, 0);
        if (errno == EINVAL || end == NULL || *end != '\0') {
            addr = -1;
            if (strcasecmp((*argv)[0], "tile") == 0 || strcasecmp((*argv)[0], "tiles") == 0) {
                if (access((*argv)[1], R_OK) != 0) {
                    fprintf(stderr, "Could not open tile file: %s\n", (*argv)[1]);
                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                    return false;
                }
                ARRAY_ADD(*patches, ((PatchInstruction){ .type = TILESET, .room_id = room_id, .filename = (*argv)[1], }));
                *argc -= 2;
                *argv += 2;
                continue;
            } else if ((strncasecmp((*argv)[0], "tile[", 5) == 0 && isdigit((*argv)[0][5])) || (strncasecmp((*argv)[0], "tiles[", 6) == 0 && isdigit((*argv)[0][6]))) {
                // Read [x][y] or [idx]
                long idx = strtol((*argv)[0] + ((*argv)[0][4] == '[' ? 5 : 6), &end, 0);
                if (errno == EINVAL || *end != ']') {
                    fprintf(stderr, "Invalid tile address: %s\n", (*argv)[0]);
                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                    return false;
                }
                if (end[1] == '[') {
                    long y = strtol(end + 2, &end, 0);
                    if (errno == EINVAL || strcasecmp(end, "]") != 0) {
                        fprintf(stderr, "Invalid tile address for y: %s\n", (*argv)[0]);
                        fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                        return false;
                    }
                    idx = TILE_IDX(idx, y);
                }
                if (idx >= WIDTH_TILES * HEIGHT_TILES) {
                    fprintf(stderr, "Invalid tile address, too large: %s\n", (*argv)[0]);
                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                    return false;
                }
                addr = offsetof(struct DecompresssedRoom, tiles) + idx;
            } else if (strcasecmp((*argv)[0], "tile_offset") == 0) {
                DEPRECATED("tile_offset is now tileset");
                addr = offsetof(struct DecompresssedRoom, tile_offset);
            } else if (strcasecmp((*argv)[0], "tileset") == 0) {
                addr = offsetof(struct DecompresssedRoom, tile_offset);
            } else if (strcasecmp((*argv)[0], "background") == 0 || strcasecmp((*argv)[0], "back") == 0) {
                addr = offsetof(struct DecompresssedRoom, background);
            } else if (strcasecmp((*argv)[0], "room_north") == 0) {
                DEPRECATED("room_north is now room_up");
                addr = offsetof(struct DecompresssedRoom, room_north);
            } else if (strcasecmp((*argv)[0], "room_up") == 0) {
                addr = offsetof(struct DecompresssedRoom, room_north);
            } else if (strcasecmp((*argv)[0], "room_east") == 0) {
                DEPRECATED("room_east is now room_right");
                addr = offsetof(struct DecompresssedRoom, room_east);
            } else if (strcasecmp((*argv)[0], "room_right") == 0) {
                addr = offsetof(struct DecompresssedRoom, room_east);
            } else if (strcasecmp((*argv)[0], "room_south") == 0) {
                DEPRECATED("room_south is now room_down");
                addr = offsetof(struct DecompresssedRoom, room_south);
            } else if (strcasecmp((*argv)[0], "room_down") == 0) {
                addr = offsetof(struct DecompresssedRoom, room_south);
            } else if (strcasecmp((*argv)[0], "room_west") == 0) {
                DEPRECATED("room_west is now room_left");
                addr = offsetof(struct DecompresssedRoom, room_west);
            } else if (strcasecmp((*argv)[0], "room_left") == 0) {
                addr = offsetof(struct DecompresssedRoom, room_west);
            } else if (strcasecmp((*argv)[0], "UNKNOWN_a") == 0) {
                DEPRECATED("UNKNOWN_a is now room_damage");
                addr = offsetof(struct DecompresssedRoom, room_damage);
            } else if (strcasecmp((*argv)[0], "room_damage") == 0 ||
                    strcasecmp((*argv)[0], "damage") == 0 ||
                    strcasecmp((*argv)[0], "dmg") == 0) {
                addr = offsetof(struct DecompresssedRoom, room_damage);
            } else if (strcasecmp((*argv)[0], "gravity_vertical") == 0) {
                addr = offsetof(struct DecompresssedRoom, gravity_vertical);
            } else if (strcasecmp((*argv)[0], "gravity_horizontal") == 0) {
                addr = offsetof(struct DecompresssedRoom, gravity_horizontal);
            } else if (strncasecmp((*argv)[0], "UNKNOWN2[", 9) == 0 && isdigit((*argv)[0][9])) {
                long idx = strtol((*argv)[0] + 9, &end, 0);
                if (errno == EINVAL || *end != ']') {
                    fprintf(stderr, "Invalid UNKNOWN2 address: %s\n", (*argv)[0]);
                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                    return false;
                }
                addr = offsetof(struct DecompresssedRoom, UNKNOWN_b) + idx;
            } else if (strcasecmp((*argv)[0], "UNKNOWN_b") == 0 || strcasecmp((*argv)[0], "UNKNOWN[b]") == 0) {
                addr = offsetof(struct DecompresssedRoom, UNKNOWN_b);
            } else if (strcasecmp((*argv)[0], "UNKNOWN_c") == 0 || strcasecmp((*argv)[0], "UNKNOWN[c]") == 0) {
                addr = offsetof(struct DecompresssedRoom, UNKNOWN_c);
            } else if (strcasecmp((*argv)[0], "UNKNOWN_d") == 0 || strcasecmp((*argv)[0], "UNKNOWN[d]") == 0) {
                fprintf(stderr, "WARNING: This will change how much data is looped over after main loop. You must ensure the correct data\n");
                // FIXME It'll probably blow the assert when writing
                addr = offsetof(struct DecompresssedRoom, num_objects);
            } else if (strcasecmp((*argv)[0], "UNKNOWN_e") == 0 || strcasecmp((*argv)[0], "UNKNOWN[e]") == 0) {
                fprintf(stderr, "WARNING: This will change how much data is looped over after main loop. You must ensure the correct data\n");
                // FIXME It'll probably blow the assert when writing
                addr = offsetof(struct DecompresssedRoom, _num_switches);
            } else if (strcasecmp((*argv)[0], "UNKNOWN_f") == 0 || strcasecmp((*argv)[0], "UNKNOWN[f]") == 0) {
                addr = offsetof(struct DecompresssedRoom, UNKNOWN_f);
            } else if (strcasecmp((*argv)[0], "name") == 0) {
                size_t len = strlen((*argv)[1]);
                if (len > 20) {
                    fprintf(stderr, "Invalid name. Max 20 characters: %s\n", (*argv)[1]);
                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                    return false;
                }
                addr = offsetof(struct DecompresssedRoom, name);
                for (size_t idx = 0; idx < 20; ++ idx) {
                    if (idx < len) {
                        ARRAY_ADD(*patches, ((PatchInstruction){ .room_id = room_id, .address = addr + idx, .value = (*argv)[1][idx], }));
                    } else {
                        ARRAY_ADD(*patches, ((PatchInstruction){ .room_id = room_id, .address = addr + idx, .value = ' ', }));
                    }
                }
                *argv += 2;
                *argc -= 2;
                break;
            } else {
                char *arg = (*argv)[0];
                if (arg[0] == '.') {
                    if (last == NULL) {
                        fprintf(stderr, "Invalid use of shortcut %s. Requires longform before\n", arg);
                        fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                        return false;
                    }
                    if (arg[1] == '.') {
                        char *last_dot = strrchr(last, '.');
                        if (last_dot == NULL) {
                            fprintf(stderr, "Invalid use of double shortcut %s. Requires longform before\n", arg);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (last) free(last);
                            return false;
                        }
                        *last_dot = '\0';
                        arg ++;
                    }
                    assert(asprintf(&arg, "%s%s", last, arg) >= 0);
                }
                if ((strncasecmp(arg, "object[", 7) == 0 && (isdigit(arg[7]) || arg[7] == ']')) || (strncasecmp(arg, "objects[", 8) == 0 && (isdigit(arg[8]) || arg[8] == ']'))) {
                    char *str = arg + (arg[6] == '[' ? 7 : 8);
                    long idx = strtol(str, &end, 0);
                    if (errno == EINVAL || *end != ']') {
                        fprintf(stderr, "Invalid object id: %s\n", arg);
                        fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                        if (arg != (*argv)[0]) free(arg);
                        return false;
                    }
                    if (end == str) {
                        idx = file->rooms[room_id].data.num_objects;
                        fprintf(stderr, "Choosing new object[%ld] over object[]\n", idx);
                    }
                    addr = idx * sizeof(struct RoomObject);
                    long value = 0xFFFF;
                    if (strcasecmp(end, "].tiles") == 0) {
                        if (access((*argv)[1], R_OK) != 0) {
                            fprintf(stderr, "Could not open tile file: %s\n", (*argv)[1]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (arg != (*argv)[0]) free(arg);
                            return false;
                        }
                        ARRAY_ADD(*patches, ((PatchInstruction){ .type = OBJECT_TILESET, .room_id = room_id, .object_id = idx, .filename = (*argv)[1], }));
                        *argc -= 2;
                        *argv += 2;
                        continue;
                    }
                    if (strcasecmp(end, "].x") == 0) {
                        addr += offsetof(struct RoomObject, x);
                    } else if (strcasecmp(end, "].y") == 0) {
                        addr += offsetof(struct RoomObject, y);
                    } else if (strcasecmp(end, "].width") == 0) {
                        addr += offsetof(struct RoomObject, block.width);
                    } else if (strcasecmp(end, "].sprite") == 0) {
                        addr += offsetof(struct RoomObject, sprite.type);
                        _Static_assert(NUM_SPRITE_TYPES == 8, "Unexpected number of sprite types");

                        if (strcasecmp((*argv)[1], "SHARK") == 0) {
                            value = SHARK;
                        } else if (strcasecmp((*argv)[1], "MUMMY") == 0) {
                            value = MUMMY;
                        } else if (strcasecmp((*argv)[1], "BLUE_MAN") == 0) {
                            value = BLUE_MAN;
                        } else if (strcasecmp((*argv)[1], "WOLF") == 0) {
                            value = WOLF;
                        } else if (strcasecmp((*argv)[1], "R2D2") == 0) {
                            value = R2D2;
                        } else if (strcasecmp((*argv)[1], "DINOSAUR") == 0) {
                            value = DINOSAUR;
                        } else if (strcasecmp((*argv)[1], "RAT") == 0) {
                            value = RAT;
                        } else if (strcasecmp((*argv)[1], "SHOTGUN_LADY") == 0) {
                            value = SHOTGUN_LADY;
                        } else {
                            value = strtol((*argv)[1], &end, 0);
                            if (value < 0 || value >= NUM_SPRITE_TYPES || errno == EINVAL || end == NULL || *end != '\0') {
                                fprintf(stderr, "Invalid sprite type: %s\n", (*argv)[1]);
                                fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                                if (arg != (*argv)[0]) free(arg);
                                return false;
                            }
                        }
                    } else if (strcasecmp(end, "].height") == 0) {
                        addr += offsetof(struct RoomObject, block.height);
                    } else if (strcasecmp(end, "].damage") == 0) {
                        addr += offsetof(struct RoomObject, sprite.damage);
                    } else if (strcasecmp(end, "].type") == 0) {
                        addr += offsetof(struct RoomObject, type);
                        if (strcasecmp((*argv)[1], "static") == 0 || strcasecmp((*argv)[1], "block") == 0) {
                            value = BLOCK;
                        } else if (strcasecmp((*argv)[1], "enemy") == 0 || strcasecmp((*argv)[1], "sprite") == 0) {
                            value = SPRITE;
                        } else {
                            fprintf(stderr, "Invalid object type: %s\n", (*argv)[1]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (arg != (*argv)[0]) free(arg);
                            return false;
                        }
                    } else if (strncasecmp(end, "].tile[", 7) == 0 || strncasecmp(end, "].tiles[", 8) == 0) {
                        // Read [x][y] or [idx]
                        addr += offsetof(struct RoomObject, tiles);
                        idx = strtol(end + (end[6] == '[' ? 7 : 8), &end, 0);
                        if (errno == EINVAL || *end != ']') {
                            fprintf(stderr, "Invalid tile address: %s\n", (*argv)[0]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (arg != (*argv)[0]) free(arg);
                            return false;
                        }
                        if (end[1] == '[') {
                            long y = strtol(end + 2, &end, 0);
                            if (errno == EINVAL || strcasecmp(end, "]") != 0) {
                                fprintf(stderr, "Invalid tile address for y: %s\n", (*argv)[0]);
                                fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                                if (arg != (*argv)[0]) free(arg);
                                return false;
                            }
                            idx = TILE_IDX(idx, y);
                        }
                        if (idx >= WIDTH_TILES * HEIGHT_TILES) {
                            fprintf(stderr, "Invalid tile address, too large: %s\n", (*argv)[0]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (arg != (*argv)[0]) free(arg);
                            return false;
                        }
                        value = strtol((*argv)[1], &end, 0);
                        if (errno == EINVAL || end == NULL || *end != '\0') {
                            fprintf(stderr, "Invalid number: %s\n", (*argv)[1]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (arg != (*argv)[0]) free(arg);
                            return false;
                        }
                        if (value < 0 || value > 0xFF) {
                            fprintf(stderr, "Value must be in the range 0..255\n");
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (arg != (*argv)[0]) free(arg);
                            return false;
                        }
                        value = idx << 8 | value;
                    } else {
                        fprintf(stderr, "Invalid object field: %s\n", arg);
                        fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                        if (arg != (*argv)[0]) free(arg);
                        return false;
                    }

                    if (value == 0xFFFF) {
                        value = strtol((*argv)[1], &end, 0);
                        if (errno == EINVAL || end == NULL || *end != '\0') {
                            fprintf(stderr, "Invalid number: %s\n", (*argv)[1]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (arg != (*argv)[0]) free(arg);
                            return false;
                        }
                        if (value < 0 || value > 0xFF) {
                            fprintf(stderr, "Value must be in the range 0..255\n");
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (arg != (*argv)[0]) free(arg);
                            return false;
                        }
                    }
                    if (last != NULL) free(last);
                    if (idx < 0) {
                        assert(asprintf(&last, "object[]") > 0);
                    } else {
                        assert(asprintf(&last, "object[%ld]", idx) > 0);
                    }
                    if (arg != (*argv)[0]) free(arg);

                    ARRAY_ADD(*patches, ((PatchInstruction){ .type = OBJECT, .room_id = room_id, .address = addr, .value = value, }));
                    *argv += 2;
                    *argc -= 2;
                    continue;
                } else if ((strncasecmp(arg, "switch[", 7) == 0 && (isdigit(arg[7]) || arg[7] == ']')) || (strncasecmp(arg, "switchs[", 8) == 0 && (isdigit(arg[8]) || arg[8] == ']')) || (strncasecmp(arg, "switches[", 9) == 0 && (isdigit(arg[9]) || arg[9] == ']'))) {
                    char *str = arg + (arg[6] == '[' ? 7 : (arg[7] == '[' ? 8 : 9));
                    long idx = strtol(str, &end, 0);
                    if (errno == EINVAL || *end != ']') {
                        fprintf(stderr, "Invalid switch id: %s\n", arg);
                        fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                        if (arg != (*argv)[0]) free(arg);
                        return false;
                    }
                    if (end == str) {
                        idx = file->rooms[room_id].data.num_switches;
                        fprintf(stderr, "Choosing new switch[%ld] over switch[]\n", idx);
                    }
                    addr = (idx + 1) * sizeof(struct SwitchObject);
                    long value = 0xFFFF;
                    long chunk_idx = -1;
                    if (strcasecmp(end, "].x") == 0) {
                        // Do it on chunks[0].x
                        addr <<= 8;
                        addr += offsetof(struct SwitchChunk, x);
                    } else if (strcasecmp(end, "].y") == 0) {
                        // Do it on chunks[0].y
                        addr <<= 8;
                        addr += offsetof(struct SwitchChunk, y);
                    } else if (strcasecmp(end, "].room_entry") == 0) {
                        // Do it on chunks[0].room_entry
                        addr <<= 8;
                        addr += offsetof(struct SwitchChunk, room_entry);
                        if (strcasecmp((*argv)[1], "false") == 0) {
                            value = 0;
                        } else if (strcasecmp((*argv)[1], "true") == 0) {
                            value = 1;
                        }
                    } else if (strcasecmp(end, "].one_time_use") == 0) {
                        // Do it on chunks[0].one_time_use
                        addr <<= 8;
                        addr += offsetof(struct SwitchChunk, one_time_use);
                        if (strcasecmp((*argv)[1], "false") == 0) {
                            value = 0;
                        } else if (strcasecmp((*argv)[1], "true") == 0) {
                            value = 1;
                        }
                    } else if (strcasecmp(end, "].side") == 0) {
                        // Do it on chunks[0].side
                        addr <<= 8;
                        addr += offsetof(struct SwitchChunk, side);
                        if (strcasecmp((*argv)[1], "top") == 0 || strcasecmp((*argv)[1], "up") == 0) {
                            value = TOP;
                        } else if (strcasecmp((*argv)[1], "bottom") == 0 || strcasecmp((*argv)[1], "down") == 0) {
                            value = BOTTOM;
                        } else if (strcasecmp((*argv)[1], "left") == 0) {
                            value = LEFT;
                        } else if (strcasecmp((*argv)[1], "right") == 0) {
                            value = RIGHT;
                        } else {
                            fprintf(stderr, "Invalid side: %s\n", (*argv)[1]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (arg != (*argv)[0]) free(arg);
                            return false;
                        }
                    } else if ((strncasecmp(end, "].chunk[", 8) == 0 && (isdigit(end[8]) || end[8] == ']')) || (strncasecmp(end, "].chunks[", 9) == 0 && (isdigit(end[9]) || end[9] == ']'))) {
                        char *str = end + (end[7] == '[' ? 8 : 9);
                        chunk_idx = strtol(str, &end, 0);
                        if (errno == EINVAL || *end != ']') {
                            fprintf(stderr, "Invalid chunk index: %s\n", arg);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (arg != (*argv)[0]) free(arg);
                            return false;
                        }
                        if (end == str) {
                            if (idx >= file->rooms[room_id].data.num_switches) {
                                // This may have issues if we do .switches[].chunks[] ..chunks[]. Intention would likely be chunks[0] and chunks[1] on switches[last]
                                chunk_idx = 1;
                            } else {
                                struct SwitchObject *sw = file->rooms[room_id].data.switches + idx;
                                chunk_idx = sw->chunks.length;
                            }
                            fprintf(stderr, "Choosing new chunk[%ld] over chunk[]\n", chunk_idx);
                        }
                        addr <<= 8;
                        addr += chunk_idx * sizeof(struct SwitchChunk);
                        if (strcasecmp(end, "].x") == 0) {
                            addr += offsetof(struct SwitchChunk, x);
                        } else if (strcasecmp(end, "].y") == 0) {
                            addr += offsetof(struct SwitchChunk, y);
                        } else if (strcasecmp(end, "].size") == 0) {
                            addr += offsetof(struct SwitchChunk, size);
                        } else if (strcasecmp(end, "].off") == 0) {
                            addr += offsetof(struct SwitchChunk, off);
                        } else if (strcasecmp(end, "].on") == 0) {
                            addr += offsetof(struct SwitchChunk, on);
                        } else if (strcasecmp(end, "].dir") == 0) {
                            addr += offsetof(struct SwitchChunk, dir);
                            if (strcasecmp((*argv)[1], "VERTICAL") == 0) {
                                value = VERTICAL;
                            } else if (strcasecmp((*argv)[1], "HORIZONTAL") == 0) {
                                value = HORIZONTAL;
                            } else {
                                value = strtol((*argv)[1], &end, 0);
                                if (value == 0x20) value = VERTICAL;
                                if (value < 0 || value >= 2 || errno == EINVAL || end == NULL || *end != '\0') {
                                    fprintf(stderr, "Invalid direction type: %s\n", (*argv)[1]);
                                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                                    if (arg != (*argv)[0]) free(arg);
                                    return false;
                                }
                                if (arg != (*argv)[0]) free(arg);
                                return false;
                            }
                        } else if (strcasecmp(end, "].msb") == 0 || strcasecmp(end, "].msb_without_y") == 0 || strcasecmp(end, "].msb_without_y_and_one_time_use") == 0) {
                            addr += offsetof(struct SwitchChunk, msb);
                        } else if (strcasecmp(end, "].index") == 0) {
                            addr += offsetof(struct SwitchChunk, index);
                        } else if (strcasecmp(end, "].bitmask") == 0) {
                            addr += offsetof(struct SwitchChunk, bitmask);
                        } else if (strcasecmp(end, "].test") == 0) {
                            addr += offsetof(struct SwitchChunk, test);
                        } else if (strcasecmp(end, "].value") == 0) {
                            addr += offsetof(struct SwitchChunk, value);
                            if (strcasecmp((*argv)[1], "") == 0 || strcasecmp((*argv)[1], "stop") == 0 || strcasecmp((*argv)[1], "stopped") == 0 || strcasecmp((*argv)[1], "stationary") == 0 || strcasecmp((*argv)[1], "stationery") == 0) {
                                value = 0;
                            } else if (strcasecmp((*argv)[1], "up") == 0) {
                                value = MOVE_UP;
                            } else if (strcasecmp((*argv)[1], "down") == 0) {
                                value = MOVE_DOWN;
                            } else if (strcasecmp((*argv)[1], "left") == 0) {
                                value = MOVE_LEFT;
                            } else if (strcasecmp((*argv)[1], "right") == 0) {
                                value = MOVE_RIGHT;
                            } else if (strcasecmp((*argv)[1], "up+left") == 0 || strcasecmp((*argv)[1], "left+up") == 0) {
                                value = MOVE_UP | MOVE_LEFT;
                            } else if (strcasecmp((*argv)[1], "up+right") == 0 || strcasecmp((*argv)[1], "right+up") == 0) {
                                value = MOVE_UP | MOVE_RIGHT;
                            } else if (strcasecmp((*argv)[1], "down+left") == 0 || strcasecmp((*argv)[1], "left+down") == 0) {
                                value = MOVE_DOWN | MOVE_LEFT;
                            } else if (strcasecmp((*argv)[1], "down+right") == 0 || strcasecmp((*argv)[1], "right+down") == 0) {
                                value = MOVE_DOWN | MOVE_RIGHT;
                            }
                        } else if (strcasecmp(end, "].room_entry") == 0) {
                            addr += offsetof(struct SwitchChunk, room_entry);
                            if (strcasecmp((*argv)[1], "false") == 0) {
                                value = 0;
                            } else if (strcasecmp((*argv)[1], "true") == 0) {
                                value = 1;
                            }
                        } else if (strcasecmp(end, "].side") == 0) {
                            addr += offsetof(struct SwitchChunk, side);
                            if (strcasecmp((*argv)[1], "top") == 0 || strcasecmp((*argv)[1], "up") == 0) {
                                value = TOP;
                            } else if (strcasecmp((*argv)[1], "bottom") == 0 || strcasecmp((*argv)[1], "down") == 0) {
                                value = BOTTOM;
                            } else if (strcasecmp((*argv)[1], "left") == 0) {
                                value = LEFT;
                            } else if (strcasecmp((*argv)[1], "right") == 0) {
                                value = RIGHT;
                            } else {
                                fprintf(stderr, "Invalid side: %s\n", (*argv)[1]);
                                fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                                if (arg != (*argv)[0]) free(arg);
                                return false;
                            }
                        } else if (strcasecmp(end, "].type") == 0) {
                            addr += offsetof(struct SwitchChunk, type);
                            _Static_assert(NUM_CHUNK_TYPES == 4, "Unexpected number of chunk types");
                            if (strcasecmp((*argv)[1], "PREAMBLE") == 0) {
                                if (chunk_idx != 0) {
                                    fprintf(stderr, "PREAMBLE type is only valid for chunk 0\n");
                                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                                    if (arg != (*argv)[0]) free(arg);
                                    return false;
                                }
                                value = PREAMBLE;
                            } else if (strcasecmp((*argv)[1], "TOGGLE_BLOCK") == 0) {
                                value = TOGGLE_BLOCK;
                            } else if (strcasecmp((*argv)[1], "TOGGLE_BIT") == 0) {
                                value = TOGGLE_BIT;
                            } else if (strcasecmp((*argv)[1], "TOGGLE_OBJECT") == 0) {
                                value = TOGGLE_OBJECT;
                            } else {
                                value = strtol((*argv)[1], &end, 0);
                                if (value < 0 || value >= NUM_CHUNK_TYPES || errno == EINVAL || end == NULL || *end != '\0') {
                                    fprintf(stderr, "Invalid chunk type: %s\n", (*argv)[1]);
                                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                                    if (arg != (*argv)[0]) free(arg);
                                    return false;
                                }
                                if (value == PREAMBLE && idx != 0) {
                                    fprintf(stderr, "PREAMBLE type is only valid for chunk 0\n");
                                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                                    if (arg != (*argv)[0]) free(arg);
                                    return false;
                                }
                                if (arg != (*argv)[0]) free(arg);
                                return false;
                            }
                        } else {
                            fprintf(stderr, "Invalid chunk field: %s\n", arg);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (arg != (*argv)[0]) free(arg);
                            return false;
                        }
                    } else {
                        fprintf(stderr, "end: %s\n", end);
                        fprintf(stderr, "Invalid switch field: %s\n", arg);
                        fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                        if (arg != (*argv)[0]) free(arg);
                        return false;
                    }

                    if (value == 0xFFFF) {
                        value = strtol((*argv)[1], &end, 0);
                        if (errno == EINVAL || end == NULL || *end != '\0') {
                            fprintf(stderr, "Invalid number: %s\n", (*argv)[1]);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            if (arg != (*argv)[0]) free(arg);
                            return false;
                        }
                    }
                    if (value < 0 || value > 0xFF) {
                        fprintf(stderr, "Value must be in the range 0..255\n");
                        fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                        if (arg != (*argv)[0]) free(arg);
                        return false;
                    }
                    if (last != NULL) free(last);
                    if (chunk_idx != -1) {
                        assert(asprintf(&last, "switch[%ld].chunk[%ld]", idx, chunk_idx) > 0);
                    } else {
                        assert(asprintf(&last, "switch[%ld]", idx) > 0);
                    }
                    if (arg != (*argv)[0]) free(arg);

                    ARRAY_ADD(*patches, ((PatchInstruction){ .type = SWITCH, .room_id = room_id, .address = addr, .value = value, }));
                    *argv += 2;
                    *argc -= 2;
                    continue;
                }
                if (addr == -1) {
                    fprintf(stderr, "Invalid address: %s\n", arg);
                    fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                    if (arg != (*argv)[0]) free(arg);
                    return false;
                }
                if (arg != (*argv)[0]) free(arg);
            }
        }
        if (addr < 0) {
            fprintf(stderr, "Address must be positive\n");
            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
            return false;
        }

        long value = strtol((*argv)[1], &end, 0);
        if (errno == EINVAL || end == NULL || *end != '\0') {
            fprintf(stderr, "Invalid number: %s\n", (*argv)[1]);
            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
            return false;
        }
        if (value < 0 || value > 0xFF) {
            fprintf(stderr, "Value must be in the range 0..255\n");
            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
            return false;
        }

        ARRAY_ADD(*patches, ((PatchInstruction){ .room_id = room_id, .address = addr, .value = value, }));
        *argv += 2;
        *argc -= 2;
    }
    if (last != NULL) free(last);

    return true;
}

bool main_delete(int *argc, char ***argv, char *program, RoomFile *file, PatchInstructionArray *patches) {
    char *end = NULL;
    if (*argc <= 2) {
        fprintf(stderr, "Usage: %s delete ROOM_ID (switch[i]|switch[x].chunk[i]|object[i])... [FILENAME]\n", program);
        return false;
    }
    long room_id = strtol((*argv)[1], &end, 0);
    if (errno == EINVAL || end == NULL || *end != '\0') {
        fprintf(stderr, "Invalid number: %s\n", (*argv)[1]);
        fprintf(stderr, "Usage: %s delete ROOM_ID (switch[i]|switch[x].chunk[i]|object[i])... [FILENAME]\n", program);
        return false;
    }
    if (room_id < 0 || (unsigned)room_id >= C_ARRAY_LEN(file->rooms)) {
        fprintf(stderr, "Room ID out of range 0..%lu\n", C_ARRAY_LEN(file->rooms) - 1);
        fprintf(stderr, "Usage: %s delete ROOM_ID (switch[i]|switch[x].chunk[i]|object[i])... [FILENAME]\n", program);
        return false;
    }
    *argv += 2;
    *argc -= 2;
    while (*argc >= 1) {
        long addr = 0xFFFF;
        if ((strncasecmp((*argv)[0], "object[", 7) == 0 && (isdigit((*argv)[0][7]) || (*argv)[0][7] == ']')) || (strncasecmp((*argv)[0], "objects[", 8) == 0 && (isdigit((*argv)[0][8]) || (*argv)[0][8] == ']'))) {
            char *str = (*argv)[0] + ((*argv)[0][6] == '[' ? 7 : 8);
            long idx = strtol(str, &end, 0);
            if (errno == EINVAL || *end != ']') {
                fprintf(stderr, "Invalid object id: %s\n", (*argv)[0]);
                fprintf(stderr, "Usage: %s delete ROOM_ID (switch[i]|switch[x].chunk[i]|object[i])... [FILENAME]\n", program);
                return false;
            }
            if (end == str) {
                if (file->rooms[room_id].data.num_objects == 0) {
                    fprintf(stderr, "No objects left to match object[]\n");
                    fprintf(stderr, "Usage: %s delete ROOM_ID (switch[i]|switch[x].chunk[i]|object[i])... [FILENAME]\n", program);
                    return false;
                }
                idx = file->rooms[room_id].data.num_objects - 1;
                fprintf(stderr, "Choosing object[%ld] over object[]\n", idx);
            }
            addr = idx * sizeof(struct RoomObject);
            ARRAY_ADD(*patches, ((PatchInstruction){ .type = OBJECT, .room_id = room_id, .address = addr, .delete = true }));
        } else if ((strncasecmp((*argv)[0], "switch[", 7) == 0 && (isdigit((*argv)[0][7]) || (*argv)[0][7] == ']')) || (strncasecmp((*argv)[0], "switchs[", 8) == 0 && (isdigit((*argv)[0][8]) || (*argv)[0][8] == ']')) || (strncasecmp((*argv)[0], "switches[", 9) == 0 && (isdigit((*argv)[0][9]) || (*argv)[0][9] == ']'))) {
            char *str = (*argv)[0] + ((*argv)[0][6] == '[' ? 7 : ((*argv)[0][7] == '[' ? 8 : 9));
            long idx = strtol(str, &end, 0);
            if (errno == EINVAL || *end != ']') {
                fprintf(stderr, "Invalid switch id: %s\n", (*argv)[0]);
                fprintf(stderr, "Usage: %s delete ROOM_ID (switch[i]|switch[x].chunk[i]|object[i])... [FILENAME]\n", program);
                return false;
            }
            if (end == str) {
                if (file->rooms[room_id].data.num_switches == 0) {
                    fprintf(stderr, "No switches left to match switch[]\n");
                    fprintf(stderr, "Usage: %s delete ROOM_ID (switch[i]|switch[x].chunk[i]|object[i])... [FILENAME]\n", program);
                    return false;
                }
                idx = file->rooms[room_id].data.num_switches - 1;
                fprintf(stderr, "Choosing switch[%ld] over switch[]\n", idx);
            }
            addr = (idx + 1) * sizeof(struct SwitchObject);
            if ((strncasecmp(end, "].chunk[", 8) == 0 && (isdigit(end[8]) || end[8] == ']')) || (strncasecmp(end, "].chunks[", 9) == 0) || (isdigit(end[9]) || end[9] == ']')) {
                char *str = end + (end[7] == '[' ? 8 : 9);
                long chunk_idx = strtol(str, &end, 0);
                if (errno == EINVAL || *end != ']') {
                    fprintf(stderr, "Invalid chunk index: %s\n", (*argv)[0]);
                    fprintf(stderr, "Usage: %s delete ROOM_ID (switch[i]|switch[x].chunk[i]|object[i])... [FILENAME]\n", program);
                    return false;
                }
                if (end == str) {
                    if (file->rooms[room_id].data.switches[idx].chunks.length == 0) {
                        fprintf(stderr, "No chunks left to match chunk[]\n");
                        fprintf(stderr, "Usage: %s delete ROOM_ID (switch[i]|switch[x].chunk[i]|object[i])... [FILENAME]\n", program);
                        return false;
                    }
                    chunk_idx = file->rooms[room_id].data.switches[idx].chunks.length - 1;
                    fprintf(stderr, "Choosing chunk[%ld] over chunk[]\n", chunk_idx);
                }
                addr <<= 8;
                addr += chunk_idx * sizeof(struct SwitchChunk);
            }
            ARRAY_ADD(*patches, ((PatchInstruction){ .type = SWITCH, .room_id = room_id, .address = addr, .delete = true }));
        }
        *argv += 1;
        *argc -= 1;
    }

    return true;
}

bool main_recompress(int *argc, char ***argv, char *program, RoomFile *file, bool *recompress, int *recompress_room) {
    char *end;
    *recompress = true;
    *argv += 1;
    *argc -= 1;
    if (*argc > 0 && isdigit(*(*argv)[0])) {
        long room_id = strtol((*argv)[0], &end, 0);
        if (errno == EINVAL || end == NULL || *end != '\0') {
            fprintf(stderr, "Invalid number: %s\n", (*argv)[0]);
            fprintf(stderr, "Usage: %s recompress [ROOM_ID] [FILENAME]\n", program);
            return false;
        }
        if (room_id < 0 || (unsigned)room_id >= C_ARRAY_LEN(file->rooms)) {
            fprintf(stderr, "Value must be in the range 0..%lu\n", C_ARRAY_LEN(file->rooms) - 1);
            fprintf(stderr, "Usage: %s recompress [ROOM_ID] [FILENAME]\n", program);
            return false;
        }
        *recompress_room = room_id;
        *argv += 1;
        *argc -= 1;
    }

    return true;
}

bool main_display(int *argc, char ***argv, char *program, RoomFile *file, bool *display, int *display_room) {
    char *end;
    *display = true;
    *argv += 1;
    *argc -= 1;
    if (*argc > 0 && isdigit(*(*argv)[0])) {
        long room_id = strtol((*argv)[0], &end, 0);
        if (errno == EINVAL || end == NULL || *end != '\0') {
            fprintf(stderr, "Invalid number: %s\n", (*argv)[0]);
            fprintf(stderr, "Usage: %s display [ROOM_ID] [FILENAME]\n", program);
            return false;
        }
        if (room_id < 0 || (unsigned)room_id >= C_ARRAY_LEN(file->rooms)) {
            fprintf(stderr, "Value must be in the range 0..%lu\n", C_ARRAY_LEN(file->rooms) - 1);
            fprintf(stderr, "Usage: %s display [ROOM_ID] [FILENAME]\n", program);
            return false;
        }
        *display_room = room_id;
        *argv += 1;
        *argc -= 1;
    }

    return true;
}

bool main_find_tile(int *argc, char ***argv, char *program, int *find_tile, int *find_tile_offset) {
    char *end = NULL;
    if (*argc <= 1) {
        fprintf(stderr, "Usage: %s find_tile tile_id [tile_offset]\n", program);
        return false;
    }
    *find_tile = strtol((*argv)[1], &end, 0);
    if (errno == EINVAL || end == NULL || *end != '\0') {
        fprintf(stderr, "Invalid number: %s\n", (*argv)[1]);
        fprintf(stderr, "Usage: %s find_tile tile_id [tile_offset]\n", program);
        return false;
    }
    if (*find_tile < 0 || *find_tile >= 64) {
        fprintf(stderr, "Invalid tile number: %s\n", (*argv)[1]);
        fprintf(stderr, "Usage: %s find_tile tile_id [tile_offset]\n", program);
        return false;
    }
    (*argv) += 2;
    *argc -= 2;
    if (*argc > 0 && isdigit((*argv)[0][0])) {
        *find_tile_offset = strtol((*argv)[0], &end, 0);
        if (errno == EINVAL || end == NULL || *end != '\0') {
            fprintf(stderr, "Invalid tile number: %s\n", (*argv)[0]);
            fprintf(stderr, "Usage: %s find_tile tile_id [tile_offset]\n", program);
            return false;
        }
        if (*find_tile_offset < 0 || *find_tile_offset % 4 != 0 || *find_tile_offset > 28) {
            fprintf(stderr, "Invalid offset: %s\n", (*argv)[0]);
            fprintf(stderr, "Usage: %s find_tile tile_id [tile_offset]\n", program);
            return false;
        }
        *argv += 1;
        *argc -= 1;
    }

    return true;
}

bool main_find_sprite(int *argc, char ***argv, char *program, int *find_sprite) {
    char *end = NULL;
    if (*argc <= 1) {
        fprintf(stderr, "Usage: %s find_sprite SPRITENAME\n", program);
        fprintf(stderr, "Available types:\n");
        _Static_assert(NUM_SPRITE_TYPES == 8, "Unexpected number of sprite types");
        for (size_t i = 0; i < NUM_SPRITE_TYPES; i ++) {
            switch ((SpriteType)i) {
                case SHARK: fprintf(stderr, "    SHARK\n"); break;
                case MUMMY: fprintf(stderr, "    MUMMY\n"); break;
                case BLUE_MAN: fprintf(stderr, "    BLUE_MAN\n"); break;
                case WOLF: fprintf(stderr, "    WOLF\n"); break;
                case R2D2: fprintf(stderr, "    R2D2\n"); break;
                case DINOSAUR: fprintf(stderr, "    DINOSAUR\n"); break;
                case RAT: fprintf(stderr, "    RAT\n"); break;
                case SHOTGUN_LADY: fprintf(stderr, "    SHOTGUN_LADY\n"); break;

                default:
                    fprintf(stderr, "%s:%d: UNREACHABLE\n", __FILE__, __LINE__);
                    exit(1);
            }
        }
        return false;
    }
    _Static_assert(NUM_SPRITE_TYPES == 8, "Unexpected number of sprite types");
    if (strcasecmp((*argv)[1], "SHARK") == 0) {
        *find_sprite = SHARK;
    } else if (strcasecmp((*argv)[1], "MUMMY") == 0) {
        *find_sprite = MUMMY;
    } else if (strcasecmp((*argv)[1], "BLUE_MAN") == 0) {
        *find_sprite = BLUE_MAN;
    } else if (strcasecmp((*argv)[1], "WOLF") == 0) {
        *find_sprite = WOLF;
    } else if (strcasecmp((*argv)[1], "R2D2") == 0) {
        *find_sprite = R2D2;
    } else if (strcasecmp((*argv)[1], "DINOSAUR") == 0) {
        *find_sprite = DINOSAUR;
    } else if (strcasecmp((*argv)[1], "RAT") == 0) {
        *find_sprite = RAT;
    } else if (strcasecmp((*argv)[1], "SHOTGUN_LADY") == 0) {
        *find_sprite = SHOTGUN_LADY;
    } else {
        *find_sprite = strtol((*argv)[1], &end, 0);
        if (*find_sprite < 0 || *find_sprite >= NUM_SPRITE_TYPES || errno == EINVAL || end == NULL || *end != '\0') {
            fprintf(stderr, "Invalid sprite type: %s\n", (*argv)[1]);
            fprintf(stderr, "Usage: %s find_sprite SPRITENAME\n", program);
            fprintf(stderr, "Available types:\n");
            _Static_assert(NUM_SPRITE_TYPES == 8, "Unexpected number of sprite types");
            for (size_t i = 0; i < NUM_SPRITE_TYPES; i ++) {
                switch ((SpriteType)i) {
                    case SHARK: fprintf(stderr, "    SHARK\n"); break;
                    case MUMMY: fprintf(stderr, "    MUMMY\n"); break;
                    case BLUE_MAN: fprintf(stderr, "    BLUE_MAN\n"); break;
                    case WOLF: fprintf(stderr, "    WOLF\n"); break;
                    case R2D2: fprintf(stderr, "    R2D2\n"); break;
                    case DINOSAUR: fprintf(stderr, "    DINOSAUR\n"); break;
                    case RAT: fprintf(stderr, "    RAT\n"); break;
                    case SHOTGUN_LADY: fprintf(stderr, "    SHOTGUN_LADY\n"); break;

                    default:
                        fprintf(stderr, "%s:%d: UNREACHABLE\n", __FILE__, __LINE__);
                        exit(1);
                }
            }
            return false;
        }
    }
    *argv += 2;
    *argc -= 2;

    return true;
}

int editor_main();
int main(int argc, char **argv) {
    char *fileName = ROOMS_FILE;
    RoomFile file = {0};
    PatchInstructionArray patches = {0};
    bool recompress = false;
    int recompress_room = -1;
    bool display = false;
    bool list = false;
    int display_room = -1;
    int find_tile = -1;
    int find_tile_offset = 0;
    int find_sprite = -1;
    char *program = argv[0];
    ARRAY(uint8_t) rooms = {0};
    FILE *fp = NULL;
    int ret = 0;
#define defer_return(code) { ret = code; goto defer; }
    fp = fopen(fileName, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open %s for reading.\n", fileName);
        defer_return(1);
    }
    fprintf(stderr, "Reading file %s.\n", fileName);
    if (!readFile(&file, fp)) defer_return(1);
    fclose(fp);
    fp = NULL;
    argc --;
    argv ++;
    while (argc > 0) {
        if (strcasecmp(argv[0], "patch") == 0) {
            if (!main_patch(&argc, &argv, program, &file, &patches)) {
                defer_return(1);
            }
        } else if (strcasecmp(argv[0], "delete") == 0) {
            if (!main_delete(&argc, &argv, program, &file, &patches)) {
                defer_return(1);
            }
        } else if (strcasecmp(argv[0], "recompress") == 0) {
            if (!main_recompress(&argc, &argv, program, &file, &recompress, &recompress_room)) {
                defer_return(1);
            }
        } else if (strcasecmp(argv[0], "display") == 0) {
            if (!main_display(&argc, &argv, program, &file, &display, &display_room)) {
                defer_return(1);
            }
        } else if (strcasecmp(argv[0], "find_tile") == 0) {
            if (!main_find_tile(&argc, &argv, program, &find_tile, &find_tile_offset)) {
                defer_return(1);
            }
        } else if (strcasecmp(argv[0], "find_sprite") == 0) {
            if (!main_find_sprite(&argc, &argv, program, &find_sprite)) {
                defer_return(1);
            }
        } else if (strcasecmp(argv[0], "find_switch") == 0) {
            printf("scanning\n");
            for (size_t room = 0; room < 64; room ++) {
                if (file.rooms[room].valid) {
                    for (size_t i = 0; i < file.rooms[room].data.num_switches; i ++) {
                        for (size_t chunk = 0; chunk < file.rooms[room].data.switches[i].chunks.length; chunk ++) {
                            if (file.rooms[room].data.switches[i].chunks.data[chunk].type == TOGGLE_BIT) {
                                printf("room %02lx %s, switch %lu\n", room, file.rooms[room].data.name, i);
                                break;
                            }
                        }
                    }
                }
            }
            argv ++;
            argc = 0;
        } else if (strcasecmp(argv[0], "editor") == 0) {
            ARRAY_FREE(rooms);
            ARRAY_FREE(patches);
            freeRoomFile(&file);
            if (fp) { fclose(fp); fp = NULL; }
            return editor_main();
        } else if (strcasecmp(argv[0], "rooms") == 0) {
            list = true;
            argv ++;
            argc --;
        } else if (strcasecmp(argv[0], "help") == 0) {
            fprintf(stderr, "Subcommands:\n");
            fprintf(stderr, "    rooms                                - List rooms\n");
            fprintf(stderr, "    display [ROOMID]                     - Defaults to all rooms\n");
            fprintf(stderr, "    recompress                           - No changes to underlying data, just recompress\n");
            fprintf(stderr, "    patch ROOMID ADDR VAL [ADDR VAL]...  - Patch room by changing the bytes requested. For multiple rooms provide patch command again\n");
            fprintf(stderr, "    delete ROOM_ID thing...              - Delete switch/chunk/object from room\n");
            fprintf(stderr, "    find_tile TILE [OFFSET]              - Find a tile/offset pair\n");
            fprintf(stderr, "    find_sprite SPRITENAME               - Find a sprite\n");
            fprintf(stderr, "    editor                               - Start an editor\n");
            fprintf(stderr, "    help                                 - Display this message\n");
            defer_return(1);
        } else {
            fileName = argv[0];
            // Should we allow args after this?
            argv ++;
            argc --;
        }
    }
    if (find_sprite == -1 && find_tile == -1 && !list && !display && !recompress && patches.length == 0) {
        fprintf(stderr, "Usage: %s subcommand [subcommand]... [FILENAME]\n", program);
        fprintf(stderr, "Subcommands:\n");
        fprintf(stderr, "    rooms                                - List rooms\n");
        fprintf(stderr, "    display [ROOMID]                     - Defaults to all rooms\n");
        fprintf(stderr, "    recompress                           - No changes to underlying data, just recompress\n");
        fprintf(stderr, "    patch ROOMID ADDR VAL [ADDR VAL]...  - Patch room by changing the bytes requested. For multiple rooms provide patch command again\n");
        fprintf(stderr, "    delete ROOM_ID thing...              - Delete switch/chunk/object from room\n");
        fprintf(stderr, "    find_tile TILE [OFFSET]              - Find a tile/offset pair\n");
        fprintf(stderr, "    find_sprite SPRITENAME               - Find a sprite\n");
        fprintf(stderr, "    editor                               - Start an editor\n");
        fprintf(stderr, "    help                                 - Display this message\n");
        defer_return(1);
    }
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

    if (find_sprite != -1) {
        bool found = false;
        for (size_t i = 0; i < C_ARRAY_LEN(file.rooms); i ++) {
            if (file.rooms[i].valid) {
                for (size_t idx = 0; idx < file.rooms[i].data.num_objects; idx ++) {
                    struct RoomObject *obj = &file.rooms[i].data.objects[idx];
                    if (obj->type == SPRITE && (int)obj->sprite.type == find_sprite) {
                        printf("Found sprite ");
                        switch ((SpriteType)find_sprite) {
                            case SHARK: fprintf(stderr, "SHARK\n"); break;
                            case MUMMY: fprintf(stderr, "MUMMY\n"); break;
                            case BLUE_MAN: fprintf(stderr, "BLUE_MAN\n"); break;
                            case WOLF: fprintf(stderr, "WOLF\n"); break;
                            case R2D2: fprintf(stderr, "R2D2\n"); break;
                            case DINOSAUR: fprintf(stderr, "DINOSAUR\n"); break;
                            case RAT: fprintf(stderr, "RAT\n"); break;
                            case SHOTGUN_LADY: fprintf(stderr, "SHOTGUN_LADY\n"); break;

                            default:
                                fprintf(stderr, "%s:%d: UNREACHABLE: Unexpected sprite type %d\n", __FILE__, __LINE__, find_sprite);
                                exit(1);
                        }
                        printf(" in room %ld (%s)\n", i, file.rooms[i].data.name);
                        found = true;
                        break;
                    }
                }
            }
        }
        if (!found) {
            printf("Sprite ");
            _Static_assert(NUM_SPRITE_TYPES == 8, "Unexpected number of sprite types");
            switch ((SpriteType)find_sprite) {
                case SHARK: fprintf(stderr, "SHARK\n"); break;
                case MUMMY: fprintf(stderr, "MUMMY\n"); break;
                case BLUE_MAN: fprintf(stderr, "BLUE_MAN\n"); break;
                case WOLF: fprintf(stderr, "WOLF\n"); break;
                case R2D2: fprintf(stderr, "R2D2\n"); break;
                case DINOSAUR: fprintf(stderr, "DINOSAUR\n"); break;
                case RAT: fprintf(stderr, "RAT\n"); break;
                case SHOTGUN_LADY: fprintf(stderr, "SHOTGUN_LADY\n"); break;

                default:
                    fprintf(stderr, "%s:%d: UNREACHABLE: Unexpected sprite type %d\n", __FILE__, __LINE__, find_sprite);
                    exit(1);
            }
            printf(" not found\n");
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
            ARRAY_FREE(file.rooms[patch.room_id].compressed);
            file.rooms[patch.room_id].valid = true;
            bool found = false;
            for (size_t r = 0; r < rooms.length; r ++) {
                if (patch.room_id == rooms.data[r]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                ARRAY_ADD(rooms, patch.room_id);
            }
            _Static_assert(NUM_PATCH_TYPES == 5, "Unexpected number of patch types");
            switch (patch.type) {
                case NORMAL:
                    assert(patch.delete == false);
                    if ((unsigned)patch.address >= offsetof(struct DecompresssedRoom, end_marker) || (unsigned)patch.address >= sizeof(file.rooms[patch.room_id].data)) {
                        fprintf(stderr, "%s:%d: WARNING: Patching *rest* may not be stable currently\n", __FILE__, __LINE__);
                        if (patch.address - sizeof(file.rooms[patch.room_id].data) >= file.rooms[patch.room_id].rest.length) {
                            fprintf(stderr, "Address %d invalid\n", patch.address);
                            fprintf(stderr, "Usage: %s patch ROOM_ID ADDR VALUE [ADDR VALUE]... [FILENAME]\n", program);
                            defer_return(1);
                        }
                        file.rooms[patch.room_id].rest.data[patch.address - sizeof(file.rooms[patch.room_id].data)] = patch.value;
                    } else {
                        fprintf(stderr, "Writing at %d with %02x (was %02x)\n", patch.address, patch.value,
                                ((uint8_t *)&file.rooms[patch.room_id].data)[patch.address]);
                        ((uint8_t *)&file.rooms[patch.room_id].data)[patch.address] = patch.value;
                    }
                    break;

                case OBJECT: {
                    int idx = patch.address / sizeof(struct RoomObject);
                    if (patch.address <= -1) {
                        Room *room = &file.rooms[patch.room_id];
                        if (patch.delete) {
                            if (room->data.num_objects == 0) {
                                fprintf(stderr, "Object id [] for room %d is out of bounds for deletion\n", patch.room_id);
                                defer_return(1);
                            }
                            idx = room->data.num_objects - 1;
                        } else {
                            idx = room->data.num_objects;
                            patch.address += sizeof(struct RoomObject);
                        }
                        fprintf(stderr, "Picking object id %d in place of [] for room %d is out of bounds for deletion\n", idx, patch.room_id);
                        fprintf(stderr, "addr = %d\n", patch.address);
                    }
                    if (idx < 0) {
                        fprintf(stderr, "Object id %d for room %d is out of bounds\n", idx, patch.room_id);
                        defer_return(1);
                    }
                    if (patch.delete) {
                        Room *room = &file.rooms[patch.room_id];
                        if (idx >= file.rooms[patch.room_id].data.num_objects) {
                            fprintf(stderr, "Object id %d for room %d is out of bounds\n", idx, patch.room_id);
                            defer_return(1);
                        }
                        fprintf(stderr, "Deleting object %d at from room %d\n", idx, patch.room_id);
                        if (room->data.objects[idx].tiles) free(room->data.objects[idx].tiles);
                        memmove(room->data.objects + idx, room->data.objects + idx + 1, (room->data.num_objects - idx - 1) * sizeof(struct RoomObject));
                        room->data.num_objects --;
                    } else {
                        if (idx >= file.rooms[patch.room_id].data.num_objects) {
                            Room *room = &file.rooms[patch.room_id];
                            room->data.objects = realloc(room->data.objects, (idx + 1) * sizeof(struct RoomObject));
                            assert(room->data.objects != NULL);
                            memset(room->data.objects + room->data.num_objects, 0, (idx + 1 - room->data.num_objects) * sizeof(struct RoomObject));
                            room->data.num_objects = idx + 1;
                        }
                        int addr = patch.address % sizeof(struct RoomObject);
                        struct RoomObject *object = file.rooms[patch.room_id].data.objects + idx;
                        fprintf(stderr, "addr %d idx %d\n", addr, idx);
                        if (addr == offsetof(struct RoomObject, tiles)) {
                            assert(object->type == BLOCK);
                            assert(object->tiles != NULL);
                            uint8_t value = patch.value & 0xFF;
                            int tile_idx = patch.value >> 8;
                            int x = tile_idx % WIDTH_TILES;
                            int y = tile_idx / WIDTH_TILES;
                            tile_idx = y * object->block.width + x;
                            fprintf(stderr, "Writing object %d tiles[%d][%d] with %02x\n", idx, x, y, value);
                            assert(tile_idx < object->block.width * object->block.height);
                            object->tiles[tile_idx] = value;
                        } else {
                            if (object->type == BLOCK) {
                                if (addr == offsetof(struct RoomObject, block.width) && patch.value > object->block.width) {
                                    object->tiles = realloc(object->tiles, patch.value * object->block.height);
                                    assert(object->tiles != NULL);
                                } else if (addr == offsetof(struct RoomObject, block.height) && patch.value > object->block.height) {
                                    object->tiles = realloc(object->tiles, object->block.width * patch.value);
                                    assert(object->tiles != NULL);
                                }
                            } else if (addr == offsetof(struct RoomObject, type) && patch.value == BLOCK) {
                                object->tiles = realloc(object->tiles, object->block.width * object->block.height);
                                assert(object->tiles != NULL);
                            }
                            fprintf(stderr, "Writing object %d at %d with %02x\n", idx, addr, patch.value);
                            ((uint8_t *)object)[addr] = patch.value;
                        }
                    }
                }; break;

                case OBJECT_TILESET: {
                    fp = fopen(patch.filename, "r");
                    if (fp == NULL) {
                        fprintf(stderr, "Could not open file for reading: %s: %s", patch.filename, strerror(errno));
                        defer_return(1);
                    }

                    assert(fseek(fp, 0L, SEEK_END) == 0);
                    long ftold = ftell(fp);
                    assert(ftold != -1);
                    size_t filesize = ftold;
                    assert(fseek(fp, 0L, SEEK_SET) == 0);

                    uint8_t *data = malloc(filesize);
                    assert(data != NULL);

                    if (fread(data, sizeof(uint8_t), filesize, fp) != (size_t)filesize) {
                        free(data);
                        fprintf(stderr, "Could read file fully: %s: %s", patch.filename, strerror(errno));
                        defer_return(1);
                    }

                    struct DecompresssedRoom *room = &file.rooms[patch.room_id].data;
                    if (patch.object_id == -1) {
                        fprintf(stderr, "%s:%d: UNIMPLEMENTED: object tileset patch is -ve", __FILE__, __LINE__);
                        defer_return(1);
                    }
                    struct RoomObject *object = file.rooms[patch.room_id].data.objects + patch.object_id;
                    size_t tile_idx = 0;
                    size_t data_idx = 0;
                    uint16_t fullbyte = 0;
                    size_t width = 0;
                    size_t height = 0;
                    while (data_idx < filesize) {
                        char c = data[data_idx ++];
                        if (c == '\033') {
                            if (data_idx >= filesize || data[data_idx] != '[') {
                                free(data);
                                fprintf(stderr, "Invalid escape sequence at %ld: %s\n", data_idx, patch.filename);
                                defer_return(1);
                            }
                            while (data_idx < filesize && !isalpha(data[data_idx])) data_idx ++;
                            if (data_idx >= filesize) {
                                free(data);
                                fprintf(stderr, "Incomplete escape sequence at EOF: %s\n", patch.filename);
                                defer_return(1);
                            }
                            data_idx++;
                        } else if (c == '\n') {
                            if (width < tile_idx / 2) {
                                width = tile_idx / 2;
                            }
                            tile_idx = 0;
                            height ++;
                        } else {
                            tile_idx ++;
                        }
                    }
                    if (width == 0 || height == 0) {
                        fprintf(stderr, "%s:%d: UNREACHABLE: Could not find the width or height of object tiles\n", __FILE__, __LINE__);
                        exit(1);
                    }
                    if (width * height < width || width * height < height) {
                        fprintf(stderr, "%s:%d: UNREACHABLE: Overflow of width*height\n", __FILE__, __LINE__);
                        exit(1);
                    }
                    if (width * height >= sizeof(room->tiles)) {
                        fprintf(stderr, "%s:%d: UNREACHABLE: Object tiles bigger than room\n", __FILE__, __LINE__);
                        exit(1);
                    }
                    object->type = BLOCK;
                    if (object->tiles) free(object->tiles);
                    object->tiles = malloc(width * height);
                    object->block.width = width;
                    object->block.height = height;


                    tile_idx = 0;
                    data_idx = 0;
                    while (tile_idx < width * height) {
                        if (data_idx >= (size_t)filesize) {
                            free(data);
                            fprintf(stderr, "Could read full tileset from file: %s: Read %ld tiles\n", patch.filename, tile_idx);
                            defer_return(1);
                        }

                        char c = data[data_idx++];
                        if (c == '\033') {
                            if (data_idx >= filesize) {
                                free(data);
                                fprintf(stderr, "Incomplete escape sequence at EOF: %s\n", patch.filename);
                                defer_return(1);
                            }

                            if (data[data_idx] != '[') {
                                free(data);
                                fprintf(stderr, "Invalid escape sequence at %ld: %s\n", data_idx, patch.filename);
                                defer_return(1);
                            }
                            while (data_idx < filesize && !isalpha(data[data_idx])) data_idx ++;
                            if (data_idx >= filesize) {
                                free(data);
                                fprintf(stderr, "Incomplete escape sequence at EOF: %s\n", patch.filename);
                                defer_return(1);
                            }
                            data_idx++;
                        } else if (c == '\n') {
                            if ((fullbyte & 0xFF00) != 0) {
                                free(data);
                                fprintf(stderr, "Unexpected newline at %ld: %s\n", data_idx - 1, patch.filename);
                                defer_return(1);
                            }
                            if (tile_idx == 0 || (data_idx >= 2 && data[data_idx - 2] == '\n')) {
                                object->tiles[tile_idx++] = 0;
                            }
                            while (tile_idx < width * height && (tile_idx % width) != 0) {
                                object->tiles[tile_idx++] = 0;
                            }
                            assert(tile_idx % width == 0);
                        } else {
                            if (c != ' ' && !isxdigit(c)) {
                                free(data);
                                fprintf(stderr, "Invalid hex digit at %ld: %s\n", data_idx - 1, patch.filename);
                                defer_return(1);
                            }

                            uint8_t b = 0;
                            if (c == ' ') b = 0;
                            else if (isdigit(c)) b = c - '0';
                            else b = 10 + tolower(c) - 'a';

                            if ((fullbyte & 0xFF00) == 0) {
                                fullbyte = 0xFF00 | (b << 4);
                            } else {
                                fullbyte = (fullbyte & 0xF0) | b;
                                object->tiles[tile_idx++] = fullbyte;
                            }
                        }
                    }
                    if (data_idx < filesize) {
                        fprintf(stderr, "WARNING: Still more file to read at %ld: %s\n", data_idx, patch.filename);
                    }

                    free(data);
                    fclose(fp);
                    fp = NULL;
                }; break;

                case SWITCH: {
                    if (patch.address >= (0x1 << 8)) {
                        int addr = patch.address & ((0x1 << 8) - 1);
                        int idx = (patch.address >> 8) / sizeof(struct SwitchObject) - 1;
                        int chunk_idx = addr / sizeof(struct SwitchChunk);
                        addr %= sizeof(struct SwitchChunk);
                        if (idx < 0) {
                            fprintf(stderr, "Switch id %d for room %d is out of bounds\n", idx, patch.room_id);
                            defer_return(1);
                        }
                        if (idx >= file.rooms[patch.room_id].data.num_switches) {
                            Room *room = &file.rooms[patch.room_id];
                            room->data.switches = realloc(room->data.switches, (idx + 1) * sizeof(struct SwitchObject));
                            assert(room->data.switches != NULL);
                            memset(room->data.switches + room->data.num_switches, 0, (idx + 1 - room->data.num_switches) * sizeof(struct SwitchObject));
                            room->data.num_switches = idx + 1;
                        }
                        struct SwitchObject *sw = file.rooms[patch.room_id].data.switches + idx;
                        if (chunk_idx < 0) {
                            fprintf(stderr, "Chunk id %d for switch %d in room %d is out of bounds\n", chunk_idx, idx, patch.room_id);
                            defer_return(1);
                        }
                        if (patch.delete) {
                            if ((unsigned)chunk_idx >= sw->chunks.length) {
                                fprintf(stderr, "Chunk id %d for switch %d in room %d is out of bounds\n", chunk_idx, idx, patch.room_id);
                                defer_return(1);
                            }
                            fprintf(stderr, "Deleting chunk %d for switch %d from room %d\n", chunk_idx, idx, patch.room_id);
                            memmove(sw->chunks.data + chunk_idx, sw->chunks.data + chunk_idx + 1, (sw->chunks.length - chunk_idx - 1) * sizeof(struct SwitchChunk));
                            sw->chunks.length --;
                        } else {
                            if ((unsigned)chunk_idx >= sw->chunks.length) {
                                ARRAY_ENSURE(sw->chunks, (unsigned)chunk_idx + 1);
                                sw->chunks.length = chunk_idx + 1;
                            }
                            fprintf(stderr, "Writing switch %d chunk[%d] at %d with %02x\n", idx, chunk_idx, addr, patch.value);
                            ((uint8_t *)&sw->chunks.data[chunk_idx])[addr] = patch.value;
                        }
                    } else {
                        if (!patch.delete) {
                            fprintf(stderr, "%s:%d: UNREACHABLE: switches now only have chunks, and no local fields\n", __FILE__, __LINE__);
                            exit(1);
                        }

                        int idx = patch.address / sizeof(struct SwitchObject) - 1;
                        if (idx < 0) {
                            fprintf(stderr, "Switch id %d for room %d is out of bounds\n", idx, patch.room_id);
                            defer_return(1);
                        }
                        if (patch.delete) {
                            Room *room = &file.rooms[patch.room_id];
                            if (idx >= file.rooms[patch.room_id].data.num_switches) {
                                fprintf(stderr, "Switch id %d for room %d is out of bounds\n", idx, patch.room_id);
                                defer_return(1);
                            }
                            ARRAY_FREE(room->data.switches[idx].chunks);
                            fprintf(stderr, "Deleting switch %d from room %d\n", idx, patch.room_id);
                            memmove(room->data.switches + idx, room->data.switches + idx + 1, (room->data.num_switches - idx - 1) * sizeof(struct RoomObject));
                            room->data.num_switches --;
                        } else {
                            if (idx >= file.rooms[patch.room_id].data.num_switches) {
                                Room *room = &file.rooms[patch.room_id];
                                room->data.switches = realloc(room->data.switches, (idx + 1) * sizeof(struct SwitchObject));
                                assert(room->data.switches != NULL);
                                memset(room->data.switches + room->data.num_switches, 0, (idx + 1 - room->data.num_switches) * sizeof(struct SwitchObject));
                                room->data.num_switches = idx + 1;
                            }
                            int addr = patch.address % sizeof(struct SwitchObject);
                            struct SwitchObject *sw = file.rooms[patch.room_id].data.switches + idx;
                            fprintf(stderr, "Writing switch %d at %d with %02x\n", idx, addr, patch.value);
                            ((uint8_t *)sw)[addr] = patch.value;
                        }
                    }
                }; break;

                case TILESET: {
                    fp = fopen(patch.filename, "r");
                    if (fp == NULL) {
                        fprintf(stderr, "Could not open file for reading: %s: %s", patch.filename, strerror(errno));
                        defer_return(1);
                    }
                    Room *room = &file.rooms[patch.room_id];
                    readRoomFromFile(room, fp, patch.filename);
                    fclose(fp);
                    fp = NULL;
                }; break;

                default:
                    fprintf(stderr, "%s:%d: UNREACHABLE: Unexpected patch type %d\n", __FILE__, __LINE__, patch.type);
                    exit(1);
                    break;
            }
        }
        for (size_t i = 0; i < rooms.length; i ++) {
            if (!file.rooms[rooms.data[i]].valid) {
                fprintf(stderr, "Room %d is invalid\n", rooms.data[i]);
                defer_return(1);
            }
            dumpRoom(&file.rooms[rooms.data[i]]);
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
                    fprintf(stderr, "Room %d is invalid\n", recompress_room);
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
    ARRAY_FREE(rooms);
    ARRAY_FREE(patches);
    freeRoomFile(&file);
    if (fp) { fclose(fp); fp = NULL; }
    return ret;
}
