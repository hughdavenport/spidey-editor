#ifndef ROOM_H
#define ROOM_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define ROOMS_FILE "ROOMS.SPL"

#define WIDTH_TILES 32
#define HEIGHT_TILES 22

#define TILE_IDX(x, y) ((y) * WIDTH_TILES + (x))

#include "array.h"

#define BLANK_TILE 0x00

// Kept for the few places that still bit-test a specific direction/speed value
// directly; for a human-readable label use move_desc() instead (room.c), which
// covers the full verified table (see its own comment).
#define MOVE_UP    0x60
#define MOVE_DOWN  0x20
#define MOVE_LEFT  0x06
#define MOVE_RIGHT 0x02

const char *move_desc(uint8_t value);
const char *movertab_behavior_desc(uint8_t value, bool is_sprite);   // best-guess label for a MOVERTAB entry's byte 2
uint8_t movertab_behavior_cycle(uint8_t current, int dir);   // next/prev known byte-2 value (wraps)
extern const uint8_t MOVERTAB_KNOWN_BEHAVIORS[];
extern const size_t MOVERTAB_KNOWN_BEHAVIORS_LEN;
/*
 * 0x00 - stop
 * 0x01 - right slow
 * 0x02 - right normal
 * 0x03 - right quick?
 * 0x04 - invert bit?
 * 0x05 - left slow
 * 0x06 - left med
 * 0x07 - left high
 * 0x08 - stop (ignored bit)
 *
 * 0x10 - down slow
 * 0x20 - down med
 * 0x30 - down quick
 *
 *
 *
 * when sprite (midnight obj 1 test 2)
 * 00 dead
 * 77 dead
 * 80 freeze in place
 * 08 dead
 * 87 freeze in place
 * 88 freeze on next wall
 * 8f freeze on next wall
 * 90 freeze in place
 * 97 freeze in place
 * 98 no action
 * 9f no action
 * a0 freeze in place
 * a7 freeze in place
 * a8 freeze on next wall
 * af freeze on next wall
 * b0 freeze in place
 * b7 freeze in place
 * b8 no action
 * bf no action
 * c0 freeze in place
 * c7 freeze in place
 * c8 freeze on next wall
 * cf freeze on next wall
 * d0 freeze in place
 * d7 freeze in place
 * d8 always move towards right, hits wall then turns twice to go back right
 * df always move towards right, hits wall then turns twice to go back right
 * e0 freeze in place
 * e7 freeze in place
 * e8 moves towards right, never turns back
 * ef moves towards right, never turns back
 * f0 freeze in place
 * f7 freeze in place
 * f8 moves towards right, never turns back
 * ff moves towards right, never turns back
 * 09-0f kills
 *
 * test 0 - activates on second press
 *
 *
 *
 * memory
 * tile_offset - require room re-enter
 * background - no effect
 * room damage - no effect? hard to tell
 * gravity - immediate effect
 * name - requires re-enter
 * above name - no effect noticed
 *
 * */

typedef enum {
    SHARK, // for damage 4 this is mary jane, 3 is mysterio, 2 and 1 are sharks
    MUMMY,
    BLUE_MAN,
    WOLF,
    R2D2,
    DINOSAUR,
    RAT,
    SHOTGUN_LADY,
    NUM_SPRITE_TYPES
} SpriteType;

enum RoomObjectType {
    BLOCK,
    SPRITE,
};

enum SwitchChunkType {
    PREAMBLE,
    TOGGLE_BLOCK,
    TOGGLE_BIT,
    TOGGLE_OBJECT,

    NUM_CHUNK_TYPES // _Static_asserts depend on this being at the end
};

enum SwitchChunkDirection {
    HORIZONTAL,
    VERTICAL,
    NUM_DIRECTIONS,
};

enum SwitchSide {
    TOP,
    RIGHT,
    BOTTOM,
    LEFT,
    NUM_SIDES
};

#define SWITCH_SIDE(s) (const char *[]){ \
    "TOP", \
    "RIGHT", \
    "BOTTOM", \
    "LEFT", \
}[(size_t)(s)]

struct RoomObject {
    // written as ((x << 8) | y) | ((width << 5) << 8) | (height << 5)
    uint8_t x;
    uint8_t y;
    union {
        struct {
            uint8_t width;
            uint8_t height;
        } block;
        struct {
            SpriteType type;
            uint8_t damage;
        } sprite;
    };

    enum RoomObjectType type;
    uint8_t *tiles;
};

struct SwitchChunk {
    enum SwitchChunkType type;
    uint8_t msb;
    uint8_t lsb;

    bool room_entry;
    uint8_t x;
    uint8_t y;
    uint8_t size;
    enum SwitchChunkDirection dir;
    uint8_t off;
    uint8_t on;
    uint8_t bitmask;
    uint8_t index;
    uint8_t test;
    uint8_t value;
    enum SwitchSide side;
    bool one_time_use;

    uint16_t room_idx;
    uint16_t switch_idx;
};

struct SwitchObject {
    ARRAY(struct SwitchChunk) chunks;
};

struct __attribute__((__packed__)) DecompressedRoom {
//    32
//  * 22
//  =704
//  +  9
//  =713 (0x2c9) - start of UNKNOWN2
//  +  5
//  + 24
//  =742 (0x2e6) - start of rest
    uint8_t tiles[WIDTH_TILES * HEIGHT_TILES];
    uint8_t tile_offset; // DEPRECATED for tileset
    uint8_t background;
    uint8_t room_north; // DEPRECATED for room_up
    uint8_t room_east; // DEPRECATED for room_right
    uint8_t room_south; // DEPRECATED for room_down
    uint8_t room_west; // DEPRECATED for room_left

    uint8_t room_damage;

    uint8_t gravity_vertical;
    uint8_t gravity_horizontal;

    uint8_t UNKNOWN_b; // Every room in the original game has this set to 1. Disassembly of
                       // SPIDEY.EXE found no code path that ever reads this byte (searched
                       // both a direct absolute address and a base+displacement access into
                       // the room's resident struct - two other nearby fields, tile_offset
                       // and the gravity bytes, were found this way with no trouble), and
                       // setting it to 2 in-game showed no observable effect either. Not
                       // editable in the interactive editor for that reason (main.c's `patch`
                       // command still reaches it, for anyone who wants to poke at it further).
    uint8_t UNKNOWN_c; // ??
    uint8_t num_objects;
    uint8_t _num_switches; // num_switches << 2 | ... counter lsb & 0x3
    uint8_t UNKNOWN_f; // counter msb

    char name[24];

    uint8_t end_marker; // Can be used with offsetof() to work out if an address is in the raw room or not

    uint8_t num_switches; // _num_switches >> 2
    struct RoomObject *objects;
    struct SwitchObject *switches;
};
_Static_assert(offsetof(struct DecompressedRoom, end_marker) == 742, "Size of room is unexpected");

typedef struct {
    uint8_t index;
    uint16_t address;
    bool valid;
    struct DecompressedRoom data;
    uint8_array rest;
    uint8_array compressed;
    uint8_array decompressed;
} Room;

typedef struct RoomFile {
    Room rooms[64];
} RoomFile;

void freeRoomFile(RoomFile *file);
bool readFile(RoomFile *file, FILE *fp);
bool writeFile(RoomFile *file, FILE *fp, int *failed_room);   // failed_room (optional): the one room that overflowed, or -1
bool readRooms(RoomFile *file);
bool readRoomFromFile(Room *room, FILE *fp, const char *filename);
bool writeRooms(RoomFile *file, int *failed_room, char *dangling_msg, size_t dangling_msg_size);
// failed_room (optional): the one room that overflowed its own limit, -1 for the whole file over its
// total limit, or -2 for a TOGGLE_BIT chunk pointing at a switch that no longer exists (not a size
// problem at all - dangling_msg, if given, is filled with which chunk and what it pointed to).
bool importRooms(FILE *fp, RoomFile *file, int force_room_id);
bool recomputeToggleBits(RoomFile *file);
void recomputeBases(RoomFile *file);
bool swapRooms(RoomFile *file, int a, int b);
void dumpRoom(Room *room, RoomFile *file, bool color);

#endif // ROOM_H
