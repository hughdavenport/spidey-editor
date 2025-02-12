#ifndef ROOM_H
#define ROOM_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define WIDTH_TILES 32
#define HEIGHT_TILES 22

#include "array.h"

#define BLANK_TILE 0x00

enum RoomObjectType {
    STATIC,
    ENEMY,
};

struct RoomObject {
    // written as ((x << 8) | y) | ((width << 5) << 8) | (height << 5)
    uint8_t x;
    uint8_t y;
    uint8_t width;
    uint8_t height;

    enum RoomObjectType type;
    uint8_t *tiles;
};

struct DecompresssedRoom {
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
    uint8_t UNKNOWN_c; // number of moving objects?
    uint8_t num_objects;
    uint8_t UNKNOWN_e; // counter lsb & 0x3
    uint8_t UNKNOWN_f; // counter msb

    char name[24];

    struct RoomObject *objects;
};

typedef struct {
    uint8_t index;
    uint16_t address;
    bool valid;
    struct DecompresssedRoom data;
    ARRAY(uint8_t) rest;
    ARRAY(uint8_t) compressed;
    ARRAY(uint8_t) decompressed;
} Room;

typedef struct RoomFile {
    Room rooms[64];
} RoomFile;

void freeRoomFile(RoomFile *file);
bool readFile(RoomFile *file, FILE *fp);
bool writeFile(RoomFile *file, FILE *fp);
bool readRooms(RoomFile *file);

#endif // ROOM_H
