#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "room.h"

#if __has_attribute(__fallthrough__)
# define fallthrough                    __attribute__((__fallthrough__))
#else
# define fallthrough                    do {} while (0)  /* fallthrough */
#endif

/* Terminal control sequences */
#define SAVE_CURSOR "\x1b" "7"
#define RESTORE_CURSOR "\x1b" "8"
#define HIDE_CURSOR "\x1b[?25l"
#define SHOW_CURSOR "\x1b[?25h"

#define SAVE_SCREEN    "\x1b[?47h"
#define RESTORE_SCREEN "\x1b[?47l"

#define ENABLE_ALT_BUFFER  "\x1b[1049h"
#define DISABLE_ALT_BUFFER "\x1b[1049l"

#define RESET_GFX_MODE "\x1b[0m"
#define CLEAR_SCREEN   "\x1b[0J"

/* Screen dimensions */
#define MIN_HEIGHT (HEIGHT_TILES + 1)
#define MIN_WIDTH (2 * WIDTH_TILES)

/* Key sequences */
#define KEY_UP    "\x1b[A"
#define KEY_DOWN  "\x1b[B"
#define KEY_RIGHT "\x1b[C"
#define KEY_LEFT  "\x1b[D"

#define SHIFT_UP    "\x1b[1;2A"
#define SHIFT_DOWN  "\x1b[1;2B"
#define SHIFT_RIGHT "\x1b[1;2C"
#define SHIFT_LEFT  "\x1b[1;2D"

#define ALT_UP    "\x1b[1;3A"
#define ALT_DOWN  "\x1b[1;3B"
#define ALT_RIGHT "\x1b[1;3C"
#define ALT_LEFT  "\x1b[1;3D"

#define ALT_SHIFT_UP    "\x1b[1;4A"
#define ALT_SHIFT_DOWN  "\x1b[1;4B"
#define ALT_SHIFT_RIGHT "\x1b[1;4C"
#define ALT_SHIFT_LEFT  "\x1b[1;4D"

#define HOME      "\x1b[1~"
#define INSERT    "\x1b[2~"
#define DELETE    "\x1b[3~"
#define END       "\x1b[4~"
#define PAGE_UP   "\x1b[5~"
#define PAGE_DOWN "\x1b[6~"

#define KEYPAD_1  END
#define KEYPAD_2  "2"
#define KEYPAD_3  PAGE_DOWN
#define KEYPAD_4  "4"
#define KEYPAD_5  "\x1b[E"
#define KEYPAD_6  "6"
#define KEYPAD_7  HOME
#define KEYPAD_8  "8"
#define KEYPAD_9  PAGE_UP
#define KEYPAD_0  INSERT

#define F1  "\x1b[OP"
#define F2  "\x1b[OQ"
#define F3  "\x1b[OR"
#define F4  "\x1b[OS"
#define F5  "\x1b[15~"
#define F6  "\x1b[17~"
#define F7  "\x1b[18~"
#define F8  "\x1b[19~"
#define F9  "\x1b[20~"
#define F10  "\x1b[21~"
#define F11  "\x1b[23~"
#define F12  "\x1b[24~"

#define ESCAPE '\033'

/* Types */
typedef struct {
    int x;
    int y;
} v2;

struct debug {
    bool hex;
    bool pos;
    bool data;
    bool neighbours;
    bool unknowns;
    bool objects;
    bool switches;
    bool all;
};

typedef enum {
    NORMAL,
    TILE_EDIT,

    GOTO_OBJECT,
    GOTO_SWITCH,
    GOTO_ROOM,

    EDIT_ROOMNAME,
    EDIT_ROOMDETAILS,
    EDIT_ROOMDETAILS_NUM,
    EDIT_ROOMDETAILS_ROOM,

    EDIT_SWITCHDETAILS,

    EDIT_SWITCHDETAILS_SELECT_CHUNK,
    EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS,
    EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS,
    EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS,
    EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM,
    EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS,

    TOGGLE_DISPLAY,
    NUM_STATES
} game_state_state;

typedef struct {
    game_state_state current_state;
    game_state_state previous_state;
    size_t size;
    v2 cursors[64];
    size_t current_level;
    v2 screen_dimensions;
    struct termios original_termios;
    RoomFile rooms;
    bool resized;
    bool help;
    struct debug debug;
    // FIXME have a pos for each room now that there is a goto

    uint16_t partial_byte;

    char room_detail;
    char room_name[24];
    size_t roomname_length;
    size_t roomname_cursor;

    size_t current_switch;
    size_t current_chunk;
    bool switch_on;
} game_state;

extern game_state *state;

/* GOTO must come after game_state definition since it references state */
#define GOTO(_x, _y)     do { \
    assert((_x) < state->screen_dimensions.x && "width out of bounds"); \
    assert((_y) < state->screen_dimensions.y && "height out of bounds"); \
    printf("\033[%d;%dH", (_y) + 1, (_x) + 1); \
} while (false)

#define UNREACHABLE() do { \
    fprintf(stderr, "%s:%d: UNREACHABLE\n", __FILE__, __LINE__); \
    if (state != NULL) { \
        assert(tcsetattr(STDIN_FILENO, TCSANOW, &state->original_termios) == 0); \
        freeRoomFile(&state->rooms); \
        free(state); \
    } \
    exit(1); \
} while (false)

/* Forward declarations for cross-module calls */
void end(void);
void debugalltoggle(void);
bool debugany(void);
void get_screen_dimensions(void);
void move(int dx, int dy);
void stretch(int dx, int dy);
void shrink(int dx, int dy);
void update(void);
void process_input(void);
void show_help(void);
void redraw(void);
