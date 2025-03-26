#ifndef ROOM_H
#define ROOM_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define WIDTH_TILES 32
#define HEIGHT_TILES 22

#include "array.h"

#define BLANK_TILE 0x00

#define MOVE_UP    0x60
#define MOVE_DOWN  0x20
#define MOVE_LEFT  0x06
#define MOVE_RIGHT 0x02

typedef enum {
    SHARK,
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
    VERTICAL
};

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
};

struct SwitchObject {
    ARRAY(struct SwitchChunk) chunks;
};

struct __attribute__((__packed__)) DecompresssedRoom {
//    32
//  * 22
//  =704
//  +  9
//  =713 (0x2c9) - start of UNKNOWN2
//  +  5
//  + 24
//  =742 (0x2e6) - start of rest
    uint8_t tiles[WIDTH_TILES * HEIGHT_TILES];
    uint8_t tile_offset;
    uint8_t background;
    uint8_t room_north;
    uint8_t room_east;
    uint8_t room_south;
    uint8_t room_west;

    uint8_t UNKNOWN_a;

    uint8_t gravity_vertical;
    uint8_t gravity_horizontal;

    uint8_t UNKNOWN_b; // unused?
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
_Static_assert(offsetof(struct DecompresssedRoom, end_marker) == 742, "Size of room is unexpected");

typedef ARRAY(uint8_t) uint8_array;
typedef struct {
    uint8_t index;
    uint16_t address;
    bool valid;
    struct DecompresssedRoom data;
    uint8_array rest;
    uint8_array compressed;
    uint8_array decompressed;
} Room;

typedef struct RoomFile {
    Room rooms[64];
} RoomFile;

void freeRoomFile(RoomFile *file);
bool readFile(RoomFile *file, FILE *fp);
bool writeFile(RoomFile *file, FILE *fp);
bool readRooms(RoomFile *file);
bool writeRooms(RoomFile *file);

#endif // ROOM_H
