#ifndef ROOM_H
#define ROOM_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define WIDTH_TILES 32
#define HEIGHT_TILES 22

#include "array.h"

struct DecompresssedRoom {
    uint8_t tiles[WIDTH_TILES * HEIGHT_TILES];
    uint8_t tile_offset;
    uint8_t background;
    uint8_t room_north;
    uint8_t room_east;
    uint8_t room_south;
    uint8_t room_west;
    uint8_t UNKNOWN;
    uint8_t gravity_vertical;
    uint8_t gravity_horizontal;
    uint8_t UNKNOWN2[5];
    char name[24];
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
