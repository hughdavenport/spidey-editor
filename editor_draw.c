#include "editor_types.h"
#include <ctype.h>
#include <string.h>

typedef struct {
    const char *key;
    const char *action;
} help_keys;

help_keys help[][100] = {
    [NORMAL]={
        {"Left/h", "Move cursor left"},
        {"Down/j", "Move cursor down"},
        {"Up/k", "Move cursor up"},
        {"Right/l", "Move cursor right"},
        {"r[nn]", "goto room"},
        {"s[n]", "goto switch"},
        {"p", "play (runs play.sh)"},
        {"q", "quit"},
        {"Ctrl-?", "toggle help"},
        {"Escape", "close/cancel"},
        {"Ctrl-h", "toggle hex in debug info"},
        {"Ctrl-t", "toggle tile edit mode"},
        {"Ctrl-d[adnopsu]", "toggle display element"},
        {0},
    },

    [TILE_EDIT]={
        {"0-9a-fA-F", "Enter hex nibble"},
        {"Shift+dir", "Move thing under cursor"},
        {"Alt+dir", "Stretch thing under cursor"},
        {"Alt+S+dir", "TODO Shrink thing under cursor"},
        {"Left/h", "Move cursor left"},
        {"Down/j", "Move cursor down"},
        {"Up/k", "Move cursor up"},
        {"Right/l", "Move cursor right"},
        {"R", "edit room name"},
        {"r[nn]", "goto room"},
        {"Ctrl-r[ktdbcef|-]", "edit room detail"},
        {"Ctrl-s[eorcs]", "create/edit switch"},
        {"Ctrl-o", "TODO create/edit object"},
        {"s[n]", "goto switch"},
        {"o[n]", "goto object"},
        {"Delete/Backspace", "remove nibble; delete thing; or clear tile"},
        {"y", "TODO copy thing"},
        {"+", "increase id of thing under cursor"},
        {"-", "decrease id of thing under cursor"},
        {"p", "play (runs play.sh)"},
        {"q", "quit"},
        {"Ctrl-?", "toggle help"},
        {"Escape", "close/cancel"},
        {"Ctrl-h", "toggle hex in debug info"},
        {"Ctrl-t", "toggle tile edit mode"},
        {"Ctrl-d[adnopsu]", "toggle display element"},
        {0},
    },

    [GOTO_OBJECT]={
        {"0-9a-fA-F", "object id (highlighted on map)"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [GOTO_SWITCH]={
        {"0-9a-fA-F", "switch id (highlighted on map)"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [GOTO_ROOM]={
        {"0-9a-fA-F", "room number"},
        {"any printable char", "change character under cursor"},
        {"UP/Alt+j", "Goto room above current room"},
        {"DOWN/Alt+k", "Goto room below current room"},
        {"LEFT/Alt+h", "Goto room to the left current room"},
        {"RIGHT/Alt+l", "Goto room to the right current room"},
        {"DEL", "delete character under cursor"},
        {"BACKSPACE", "delete character under cursor"},
        {"ESC", "go back to main view"},
        {"Ctrl+q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_ROOMNAME]={
        {"any printable char", "change character under cursor"},
        {"DEL", "delete character under cursor"},
        {"BACKSPACE", "delete character under cursor"},
        {"ENTER", "save room name"},
        {"LEFT", "move cursor left"},
        {"RIGHT", "move cursor right"},
        {"ESC", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [TOGGLE_DISPLAY]={
        {"a", "toggle all debug info"},
        {"d", "toggle room data display"},
        {"n", "toggle neighbour display"},
        {"o", "toggle room object display"},
        {"p", "toggle position display"},
        {"s", "toggle room switch display"},
        {"u", "toggle unknown display"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_ROOMDETAILS]={
        {"g", "Edit room background"},
        {"t", "Edit room tileset"},
        {"d", "Edit room damage"},
        {"|", "Edit room vertical gravity"},
        {"-", "Edit room horizontal gravity"},

        {"b", "Edit room UNKNOWN_b"},
        {"c", "Edit room UNKNOWN_c"},
        {"e", "Edit room UNKNOWN_e (only bottom two bits)"},
        {"f", "Edit room UNKNOWN_f"},

        {"LEFT", "Edit room to left"},
        {"RIGHT", "Edit room to right"},
        {"UP", "Edit room above"},
        {"DOWN", "Edit room below"},

        {"ESC", "go back to main view"},
        {"p", "play (runs play.sh)"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_ROOMDETAILS_NUM]={
        {"0-9a-fA-F", "value"},
        {"ESC", "go back to main view"},
        {"p", "play (runs play.sh)"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_ROOMDETAILS_ROOM]={
        {"0-9a-fA-F", "room number"},
        {"any printable char", "change character under cursor"},
        {"DEL", "delete character under cursor"},
        {"BACKSPACE", "delete character under cursor"},
        {"ESC", "go back to main view"},
        {"Ctrl+q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS]={
        {"e", "flip the switch on entry bit"},
        {"o", "flip the switch one time only bit"},
        {"s", "rotate which side is the switch"},
        {"Left/h", "set switch side to left"},
        {"Down/j", "set switch side to bottom"},
        {"Up/k", "set switch side to top"},
        {"Right/l", "set switch side to right"},
        {"+", "increase switch id (shuffles up)"},
        {"-", "decrease switch id (shuffles down)"},
        {"y", "TODO copy switch"},
        {"c[n]", "select chunk"},
        {"C", "create new chunk"},
        {"ESC", "go back to main view"},
        {"DEL", "delete switch"},
        {"BACKSPACE", "delete switch"},
        {"p", "play (runs play.sh)"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS_SELECT_CHUNK]={
        {"0-9a-fA-F", "chunk number"},
        {"ESC", "go back to main view"},
        {"p", "play (runs play.sh)"},
        {"q", "go back to main view"},
        {"+", "add new chunk"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS]={
        {"t", "change chunk type"},
        {"DEL", "delete chunk"},
        {"BACKSPACE", "delete chunk"},
        {"y", "TODO copy chunk"},
        {"+", "increase chunk id (shuffles up)"},
        {"-", "decrease chunk id (shuffles down)"},
        {"0-9a-fA-F", "change switch number"},
        {"r", "change room number"},
        {"n", "cycle possible on values"},
        {"o", "cycle possible off values"},
        {"ESC", "go back to main view"},
        {"p", "play (runs play.sh)"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM]={
        {"0-9a-fA-F", "room number"},
        {"any printable char", "change character under cursor"},
        {"DEL", "delete character under cursor"},
        {"BACKSPACE", "delete character under cursor"},
        {"ESC", "go back to main view"},
        {"Ctrl+q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS]={
        {"t", "change chunk type"},
        {"DEL", "remove partial entry or delete chunk"},
        {"BACKSPACE", "remove partial entry or delete chunk"},
        {"y", "TODO copy chunk"},
        {"+", "increase chunk id (shuffles up)"},
        {"-", "decrease chunk id (shuffles down)"},
        {"0-9a-fA-F", "change selected block tile"},
        {"Left/h", "Move block left"},
        {"Down/j", "Move block down"},
        {"Up/k", "Move block up"},
        {"Right/l", "Move block right"},
        {"^", "increase block size (width/height)"},
        {"v", "decrease block size (width/height)"},
        {"Space", "toggle vertical/horizontal"},
        {"o", "toggle between editing on and off value"},
        {"ESC", "go back to main view"},
        {"p", "play (runs play.sh)"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS]={
        {"t", "change chunk type"},
        {"DEL", "remove partial entry or delete chunk"},
        {"BACKSPACE", "remove partial entry or delete chunk"},
        {"y", "TODO copy chunk"},
        {"+", "increase chunk id (shuffles up)"},
        {"-", "decrease chunk id (shuffles down)"},
        {"0-9a-fA-F", "change selected block tile"},
        {"Space", "cycle through memory fields"},
        {"o", "toggle between editing on and off value"},
        {"^", "increase block size (advanced)"},
        {"v", "decrease block size (advanced)"},
        {"r", "toggle direction (advanced)"},
        {"ESC", "go back to main view"},
        {"p", "play (runs play.sh)"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS]={
        {"t", "change chunk type"},
        {"DEL", "remove partial entry or delete chunk"},
        {"BACKSPACE", "remove partial entry or delete chunk"},
        {"y", "TODO copy chunk"},
        {"+", "increase chunk id (shuffles up)"},
        {"-", "decrease chunk id (shuffles down)"},
        {"i", "cycle through possible index values"},
        {"s", "cycle through possible test values"},
        {" TODO dir keys ?", "change value"},
        {"0-9a-fA-F", "change raw value (advanced)"},
        {"ESC", "go back to main view"},
        {"p", "play (runs play.sh)"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

};
_Static_assert(C_ARRAY_LEN(help) == NUM_STATES, "Unhandled for all states");

void show_help()
{
    int x = 0, x1 = 0, x2 = 0, y = 0;
    for (size_t i = 0; i < C_ARRAY_LEN(help[state->current_state]); i ++, y ++) {
        if (help[state->current_state][i].key == NULL) break;
        int len = strlen(help[state->current_state][i].key);
        if (len > x1) x1 = len;
        if (help[state->current_state][i].action == NULL) {
            fprintf(stderr, "here\n");
        }
        len = strlen(help[state->current_state][i].action);
        if (len > x2) x2 = len;
    }
    x = x1 + x2 + 5;
    if (x % 2) x++;
    y += 3;
    GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2);
    scr_printf("\033[47;30;1m");
    scr_printf("+");
    for (int i = 0; i < x / 2 - 3; i ++) scr_printf("-");
    scr_printf("help");
    for (int i = 0; i < x / 2 - 3; i ++) scr_printf("-");
    scr_printf("+\033[m ");
    for (int _y = 1; _y < y - 1; _y ++) {
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + _y);
        scr_printf("\033[47;30;1;m|%*s|\033[40;37;1m ", x - 2, "");
    }
    GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + (y - 2));
    scr_printf("\033[47;30;1m");
    scr_printf("+");
    for (int i = 0; i < x - 2; i ++) scr_printf("-");
    scr_printf("+\033[40;37;1m ");
    GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + (y - 1));
    scr_printf("\033[m \033[40;37;1m%*s", x, "");
    scr_printf("\033[47;30;1m");

    int line = 1;
    for (size_t i = 0; i < C_ARRAY_LEN(help[state->current_state]); i ++) {
        if (help[state->current_state][i].key == NULL) break;
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        scr_printf("|%-*s - %-*s|", x1, help[state->current_state][i].key, x - x1 - 5, help[state->current_state][i].action);
    }
}

void redraw() {
#define PRINTF_DATA(num) scr_printf(state->debug.hex ? "%02X" : "%d", (num))

    int offset_y = 0;
    int offset_x = 0;
    if (debugany()) offset_y ++;
    if (state->debug.data) offset_y ++;
    if (state->debug.unknowns) offset_y ++;
    if (state->debug.neighbours) offset_y += 4;
    if (state->debug.objects) offset_y ++;

    size_t level = state->current_level;
    struct DecompressedRoom room = state->rooms.rooms[level].data;
    char room_name[25] = {0};
    assert(C_ARRAY_LEN(room_name) >= C_ARRAY_LEN(room.name));
    strncpy(room_name, room.name, C_ARRAY_LEN(room.name));
    int room_name_len;
    for (room_name_len = (int)C_ARRAY_LEN(room_name) - 1; room_name_len >= 0; room_name_len --) {
        if (room_name[room_name_len] == '\0' || isspace(room_name[room_name_len])) {
            room_name[room_name_len] = '\0';
        } else {
            room_name_len ++;
            break;
        }
    }
    struct RoomObject *object_underneath = NULL;
    struct SwitchObject *switch_underneath = NULL;
    struct SwitchChunk *chunk_underneath = NULL;
    struct SwitchObject *chunk_switch_underneath = NULL;

    int x = state->cursors[state->current_level].x;
    int y = state->cursors[state->current_level].y;
    assert(x >= 0);
    assert(y >= 0);
    if (x < WIDTH_TILES && y < HEIGHT_TILES) {
        GOTO(2 * x, y + 1);
    }
    uint8_t tile = x < WIDTH_TILES && y < HEIGHT_TILES ? room.tiles[TILE_IDX(x, y)] : 0;
    bool obj = false;
    bool sw = false;
    bool ch = false;
    bool sprite = false;
    size_t i;
    for (i = 0; i < room.num_objects; i ++) {
        struct RoomObject *object = room.objects + i;
        if (object->type == BLOCK &&
                x >= object->x && x < object->x + object->block.width &&
                y >= object->y && y < object->y + object->block.height) {
            obj = true;
            tile = object->tiles[(y - object->y) * object->block.width + (x - object->x)];
            object_underneath = object;
            break;
        } else if (object->type == SPRITE && x == object->x && y == object->y) {
            obj = true;
            sprite = true;
            tile = (object->sprite.type << 4) | object->sprite.damage;
            object_underneath = object;
            break;
        }
    }
    size_t obj_i = i;
    for (i = 0; i < room.num_switches; i ++) {
        struct SwitchObject *switcch = room.switches + i;
        if (switcch->chunks.length > 0 && switcch->chunks.data[0].type == PREAMBLE &&
                x == switcch->chunks.data[0].x && y == switcch->chunks.data[0].y) {
            sw = true;
            switch_underneath = switcch;
            break;
        }
    }
    size_t sw_i = i;
    for (i = 0; i < room.num_switches; i ++) {
        struct SwitchObject *switcch = room.switches + i;
        size_t c;
        for (c = 1; c < switcch->chunks.length; c ++) {
            struct SwitchChunk *chunk = switcch->chunks.data + c;
            if (chunk->type == TOGGLE_BLOCK) {
                if (chunk->dir == HORIZONTAL) {
                    if (y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size) {
                        if (i + 1 == state->current_switch && c == state->current_chunk) {
                            tile = state->switch_on ? chunk->on : chunk->off;
                        } else {
                            tile = chunk->on;
                        }
                        ch = true;
                        chunk_switch_underneath = switcch;
                        chunk_underneath = chunk;
                        break;
                    }
                } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                    if (i + 1 == state->current_switch && c == state->current_chunk) {
                        tile = state->switch_on ? chunk->on : chunk->off;
                    } else {
                        tile = chunk->on;
                    }
                    ch = true;
                    chunk_switch_underneath = switcch;
                    chunk_underneath = chunk;
                    break;
                }
            }
        }
    }
    if (state->debug.switches) {
        if (switch_underneath != NULL) {
            offset_y += 2 + switch_underneath->chunks.length;
        } else if (state->current_state == TILE_EDIT) {
            offset_y ++;
        }
    }

    if (state->current_state == GOTO_ROOM) {
        offset_y = 36 - MIN_HEIGHT;
    }

    assert(MIN_WIDTH + offset_x >= 2 * WIDTH_TILES);
    assert(MIN_HEIGHT + offset_y >= HEIGHT_TILES);
    if (state->screen_dimensions.x < MIN_WIDTH + offset_x || state->screen_dimensions.y < MIN_HEIGHT + offset_y) {
        char *message = NULL;
        assert(asprintf(&message,
                    "Required screen dimension is %dx%d. Currently %s%d\033[mx%s%d",
                    MIN_WIDTH + offset_x + 1, MIN_HEIGHT + offset_y + 1,
                    (state->screen_dimensions.x < MIN_WIDTH + offset_x + 1 ? "\033[31;1m" : "\033[32m"),
                    state->screen_dimensions.x,
                    (state->screen_dimensions.y < MIN_HEIGHT + offset_y + 1 ? "\033[31;1m" : "\033[32m"),
                    state->screen_dimensions.y
        ) >= 0);
        int x = state->screen_dimensions.x / 2 - (int)(strlen(message) - 7) / 2;
        int y = state->screen_dimensions.y / 2;
        if (x < 0) x = 0;
        if (x >= MIN_WIDTH) x = MIN_WIDTH - 1;
        if (y < 0) y = 0;
        if (y >= MIN_HEIGHT) y = MIN_HEIGHT - 1;
        GOTO(x, y);
        scr_printf("%s", message);
        free(message);
        screen_flush();
        return;
    }

    GOTO(6, 0);
    scr_printf("%s", state->debug.hex ? "[HEX]" : "[DEC]");

    GOTO(MIN_WIDTH / 2 - ((room_name_len + 7) / 2), 0);
    if (state->current_state == GOTO_ROOM || state->current_state == EDIT_ROOMDETAILS_ROOM || state->current_state == EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM) {
        if (state->current_state == EDIT_ROOMDETAILS_ROOM) {
            switch (state->room_detail) {
                case 'A':
                case 'k':
                    scr_printf("up: ");
                    break;

                case 'B':
                case 'j':
                    scr_printf("down: ");
                    break;

                case 'C':
                case 'l':
                    scr_printf("right: ");
                    break;

                case 'D':
                case 'h':
                    scr_printf("left: ");
                    break;

                default: UNREACHABLE();
            }
        }
        if (state->partial_byte) {
            if (state->debug.hex) {
                scr_printf("%x", state->partial_byte & 0xFF);
            } else {
                scr_printf("%u", state->partial_byte & 0xFF);
            }
        } else {
            scr_printf("\033[4;5m_\033[m");
        }
        switch (state->current_state) {
            case GOTO_ROOM:
            case EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM:
                scr_printf("\033[4;5m_\033[m - \"%s???\"", state->room_name);
                break;

            case EDIT_ROOMDETAILS_ROOM:
            {
#define READ_NEIGHBOUR(id) do { \
neighbour_room = state->rooms.rooms[(id)].data; \
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
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                struct DecompressedRoom neighbour_room = {0};
                char neighbour_name[25] = {0};
                switch (state->room_detail) {
                    case 'A':
                    case 'k':
                        READ_NEIGHBOUR(room->room_north);
                        break;

                    case 'B':
                    case 'j':
                        READ_NEIGHBOUR(room->room_south);
                        break;

                    case 'C':
                    case 'l':
                        READ_NEIGHBOUR(room->room_east);
                        break;

                    case 'D':
                    case 'h':
                        READ_NEIGHBOUR(room->room_west);
                        break;

                    default: UNREACHABLE();
                }
                scr_printf("\033[4;5m_\033[m - \"%s\"", neighbour_name);
#undef READ_NEIGHBOUR
            }; break;

            default: UNREACHABLE();
        }
        for (int y = 0; y < HEIGHT_TILES; y ++) {
            for (int x = 0; x < WIDTH_TILES; x ++) {
                scr_printf("  ");
            }
            scr_printf("\n");
        }

        GOTO(0,1);
        scr_printf("\nEnter room number or q to go to main view\n\n");
        for (size_t i = 0; i < C_ARRAY_LEN(state->rooms.rooms) / 2; i ++) {
            size_t room_id = i;
            struct DecompressedRoom *room = &state->rooms.rooms[room_id].data;
            assert(C_ARRAY_LEN(room_name) >= C_ARRAY_LEN(room->name));
            strncpy(room_name, room->name, C_ARRAY_LEN(room->name));
            bool highlight = state->roomname_cursor && strncasecmp(room_name, state->room_name, state->roomname_cursor) == 0;
            int room_name_len;
            for (room_name_len = (int)C_ARRAY_LEN(room_name) - 1; room_name_len >= 0; room_name_len --) {
                if (room_name[room_name_len] == '\0' || isspace(room_name[room_name_len])) {
                    room_name[room_name_len] = '\0';
                } else {
                    room_name_len ++;
                    break;
                }
            }
            int printed = 0;
            uint8_t underline = 0;
            if (state->partial_byte && ((room_id / (state->debug.hex ? 16 : 10)) == (state->partial_byte & 0xFF))) {
                scr_printf("\033[4;1m");
                underline = 1;
                printed += scr_printf(state->debug.hex ? "%01lx" : "%01ld", room_id / (state->debug.hex ? 16 : 10));
                scr_printf("\033[m");
                printed += scr_printf(state->debug.hex ? "%01lx" : "%01ld", room_id % (state->debug.hex ? 16 : 10));
            } else {
                if (highlight) scr_printf("\033[4;1m");
                printed += scr_printf(state->debug.hex ? "%02lx" : "%02ld", room_id);
            }
            printed += scr_printf(" - \"");
            if (underline) {
                scr_printf("\033[4;1m");
            }
            if (highlight) {
                char tmp = room_name[state->roomname_cursor];
                room_name[state->roomname_cursor] = 0;
                printed += scr_printf("%s", room_name);
                room_name[state->roomname_cursor] = tmp;
                scr_printf("\033[m");
                printed += scr_printf("%s", room_name + state->roomname_cursor);
            } else {
                printed += scr_printf("%s", room_name);
            }
            if (underline) {
                scr_printf("\033[m");
                underline = 0;
            }
            printed += scr_printf("\"");
            for (int j = printed; j < WIDTH_TILES; j ++) {
                printed += scr_printf(" ");
            }

            room_id = C_ARRAY_LEN(state->rooms.rooms) / 2 + i;
            room = &state->rooms.rooms[room_id].data;
            assert(C_ARRAY_LEN(room_name) >= C_ARRAY_LEN(room->name));
            strncpy(room_name, room->name, C_ARRAY_LEN(room->name));
            highlight = state->roomname_cursor && strncasecmp(room_name, state->room_name, state->roomname_cursor) == 0;
            for (room_name_len = (int)C_ARRAY_LEN(room_name) - 1; room_name_len >= 0; room_name_len --) {
                if (room_name[room_name_len] == '\0' || isspace(room_name[room_name_len])) {
                    room_name[room_name_len] = '\0';
                } else {
                    room_name_len ++;
                    break;
                }
            }
            if (state->partial_byte && ((room_id / (state->debug.hex ? 16 : 10)) == (state->partial_byte & 0xFF))) {
                scr_printf("\033[4;1m");
                underline = 1;
                scr_printf(state->debug.hex ? "%01lx" : "%01ld", room_id / (state->debug.hex ? 16 : 10));
                scr_printf("\033[m");
                scr_printf(state->debug.hex ? "%01lx" : "%01ld", room_id % (state->debug.hex ? 16 : 10));
            } else {
                if (highlight) scr_printf("\033[4;1m");
                scr_printf(state->debug.hex ? "%02lx" : "%02ld", room_id);
            }
            scr_printf(" - \"");
            if (underline) {
                scr_printf("\033[4;1m");
            }
            if (highlight) {
                char tmp = room_name[state->roomname_cursor];
                room_name[state->roomname_cursor] = 0;
                printed += scr_printf("%s", room_name);
                room_name[state->roomname_cursor] = tmp;
                scr_printf("\033[m");
                printed += scr_printf("%s", room_name + state->roomname_cursor);
            } else {
                printed += scr_printf("%s", room_name);
            }
            if (underline) {
                scr_printf("\033[m");
                underline = 0;
            }
            scr_printf("\"");

            if (i != C_ARRAY_LEN(state->rooms.rooms) / 2 - 1) scr_printf("\n");
        }
        goto show_help_if_needed;
    } else {
        PRINTF_DATA((int)level);
        scr_printf(" - \"");
        if (state->current_state == EDIT_ROOMNAME) {
            for (size_t i = 0; i < state->roomname_cursor; i ++) {
                scr_printf("%c", state->room_name[i]);
            }
            scr_printf("\033[40;4;5m%c\033[m", state->room_name[state->roomname_cursor] ? state->room_name[state->roomname_cursor] : '_');
            for (size_t i = state->roomname_cursor + 1; i < C_ARRAY_LEN(state->room_name); i ++) {
                if (state->room_name[i] == '\0') break;
                scr_printf("%c", state->room_name[i]);
            }
        } else {
            scr_printf("%s", room_name);
        }
        scr_printf("\"");
    }


    if (state->current_state == GOTO_SWITCH) {
        for (int y = 0; y < HEIGHT_TILES; y ++) {
            for (int x = 0; x < WIDTH_TILES; x ++) {
                uint8_t tile = room.tiles[TILE_IDX(x, y)];
                struct SwitchObject *found_switch = NULL;
                int s;
                for (s = 0; s < room.num_switches; s ++) {
                    struct SwitchObject *switcch = room.switches + s;
                    if (switcch->chunks.length > 0 && switcch->chunks.data[0].type == PREAMBLE &&
                            x == switcch->chunks.data[0].x && y == switcch->chunks.data[0].y) {
                        found_switch = switcch;
                        break;
                    }
                }
                GOTO(2 * x, y + 1);
                if (found_switch) {
                    scr_printf("\033[4%d;30;1m", (s % 3) + 4);
                    if (s > 15) {
                        // FIXME support 2 digit nums
                        UNREACHABLE();
                    }
                    scr_printf(" %x", s);
                    scr_printf("\033[m");
                } else {
                    if (tile == BLANK_TILE) {
                        scr_printf("  ");
                    } else {
                        scr_printf("%02X", tile);
                    }
                }
            }
        }
    } else {
        for (int y = 0; y < HEIGHT_TILES; y ++) {
            for (int x = 0; x < WIDTH_TILES; x ++) {
                uint8_t tile = room.tiles[TILE_IDX(x, y)];
                bool colored = false;
                if (state->debug.switches && state->current_state != GOTO_OBJECT)
                for (int s = 0; s < room.num_switches; s ++) {
                    struct SwitchObject *sw = room.switches + s;
                    assert(sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE);
                    if (x == sw->chunks.data[0].x && y == sw->chunks.data[0].y) {
                        colored = true;
                        scr_printf("\033[4%d;30m", (s % 3) + 4);
                        break;
                    }
                    for (size_t c = 1; !colored && c < sw->chunks.length; c ++) {
                        struct SwitchChunk *chunk = sw->chunks.data + c;
                        if (chunk->type == TOGGLE_BLOCK) {
                            if (chunk->dir == HORIZONTAL) {
                                if (y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size) {
                                    scr_printf("\033[3%d;40m", (s % 3) + 4);
                                    colored = true;
                                    if ((size_t)s + 1 == state->current_switch && c == state->current_chunk) {
                                        tile = state->switch_on ? chunk->on : chunk->off;
                                    } else {
                                        tile = chunk->on;
                                    }
                                }
                            } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                                scr_printf("\033[3%d;40m", (s % 3) + 4);
                                colored = true;
                                if ((size_t)s + 1 == state->current_switch && c == state->current_chunk) {
                                    tile = state->switch_on ? chunk->on : chunk->off;
                                } else {
                                    tile = chunk->on;
                                }
                            }
                        }
                    }
                }
                if (colored || tile != BLANK_TILE) {
                    GOTO(2 * x, y + 1);
                    scr_printf("%02X", tile);
                    if (colored) scr_printf("\033[m");
                }
            }
        }
    }

    uint8_t dirty[MIN_HEIGHT * MIN_WIDTH] = {0};
    if (state->debug.objects) {
        for (size_t i = 0; i < room.num_objects; i ++) {
            struct RoomObject *object = room.objects + i;
            assert(object->x >= 0);
            assert(object->y >= 0);
            assert(object->x < WIDTH_TILES);
            assert(object->y < HEIGHT_TILES);
            switch (object->type) {
                case BLOCK:
                    assert(object->x + object->block.width <= WIDTH_TILES);
                    assert(object->y + object->block.height <= HEIGHT_TILES);
                    scr_printf("\033[3%ldm", (i % 3) + 1);
                    for (int y = object->y; y < object->y + object->block.height; y ++) {
                        GOTO(2 * object->x, y + 1);
                        for (int x = object->x; x < object->x + object->block.width; x ++) {
                            if (dirty[y * MIN_WIDTH + x] == 1) continue;
                            dirty[y * MIN_WIDTH + x] = 1;
                            uint8_t tile = object->tiles[(y - object->y) * object->block.width + (x - object->x)];
                            if (tile == BLANK_TILE) {
                                scr_printf("  ");
                            } else {
                                scr_printf("%02X", tile);
                            }
                        }
                    }
                    break;

                case SPRITE:
                    if (state->current_state == GOTO_SWITCH) break;
                    GOTO(2 * object->x, object->y + 1);
                    assert((unsigned)(object->y * MIN_WIDTH + object->x) < sizeof(dirty));
                    if (dirty[object->y * MIN_WIDTH + object->x] == 1) continue;
                    dirty[object->y * MIN_WIDTH + object->x] = 1;
                    scr_printf("\033[4%ld;30m%d%d", (i % 3) + 1, object->sprite.type, object->sprite.damage);
                    break;
            }
            scr_printf("\033[m");
        }
        if (state->current_state == GOTO_OBJECT) {
            for (int y = 0; y < HEIGHT_TILES; y ++) {
                for (int x = 0; x < WIDTH_TILES; x ++) {
                    struct RoomObject *found_object = NULL;
                    int o;
                    for (o = 0; o < room.num_objects; o ++) {
                        struct RoomObject *obj = room.objects + o;
                        if (x == obj->x && y == obj->y) {
                            found_object = obj;
                            break;
                        }
                    }
                    GOTO(2 * x, y + 1);
                    if (found_object) {
                        scr_printf("\033[4%d;30;1m", (o % 3) + 1);

                        if (o > 15) {
                            // FIXME support 2 digit nums
                            UNREACHABLE();
                        }
                        scr_printf(" %x", o);
                        scr_printf("\033[m");
                    }
                }
            }
        }
    }

    if (state->current_state == TILE_EDIT || (state->current_state != NORMAL)) {
        GOTO(2 * x, y + 1);
        size_t ch_i = i;
        if (obj) {
            if (sprite) {
                scr_printf("\033[3%ld;40;1m", (obj_i % 3) + 1);
            } else {
                scr_printf("\033[30;4%ld;1m", (obj_i % 8) + 1);
            }
        }
        if (!obj && sw) {
            scr_printf("\033[3%ld;1m", (sw_i % 3) + 4);
        }
        if (!obj && !sw && ch) {
            scr_printf("\033[4%ld;30;1m", (ch_i % 3) + 4);
        }
        if (obj || sw || ch) {
            if (state->current_state == GOTO_SWITCH) {
                scr_printf("@");
            } else {
                scr_printf("%02X", tile);
            }
            scr_printf("\033[m");
        } else {
            scr_printf("\033[47;30;1m%02X\033[m", tile);
        }
        switch (state->current_state) {
            case EDIT_ROOMDETAILS_NUM:
            case EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS:
            case EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS:
            case EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS:
            case EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS:
                break;

            case NORMAL:
            case TILE_EDIT:
            case GOTO_OBJECT:
            case GOTO_SWITCH:
            case EDIT_ROOMNAME:
            case EDIT_ROOMDETAILS:
            case EDIT_SWITCHDETAILS:
            case EDIT_SWITCHDETAILS_SELECT_CHUNK:
            case TOGGLE_DISPLAY:
                if (state->partial_byte) {
                    GOTO(2 * x, y + 1);
                    if (obj) {
                        if (sprite) {
                            scr_printf("\033[30;4%ld;1m", (obj_i % 3) + 1);
                        } else {
                            scr_printf("\033[3%ld;1m", (obj_i % 8) + 1);
                        }
                    } else if (sw) {
                        scr_printf("\033[30;4%ld;1m", (sw_i % 3) + 4);
                    } else if (ch) {
                        scr_printf("\033[40;3%ld;1m", (ch_i % 3) + 4);
                    } else {
                        scr_printf("\033[1m");
                    }
                    scr_printf("%X\033[m", state->partial_byte & 0xFF);
                }
                break;

            case EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM:
            case EDIT_ROOMDETAILS_ROOM:
            case GOTO_ROOM:
            default: UNREACHABLE();
        }
    } else {
        assert(state->cursors[state->current_level].x >= 0);
        assert(state->cursors[state->current_level].y >= 0);
        assert(state->cursors[state->current_level].x < WIDTH_TILES);
        assert(state->cursors[state->current_level].y < HEIGHT_TILES);
        if (state->cursors[state->current_level].y - 2 >= 0) {
            GOTO(2 * state->cursors[state->current_level].x, state->cursors[state->current_level].y - 1);
            scr_printf("\033[41;30;1m@@\033[m");
        }
        if (state->cursors[state->current_level].y - 1 >= 0) {
            GOTO(2 * state->cursors[state->current_level].x, state->cursors[state->current_level].y);
            scr_printf("\033[41;30;1m@@\033[m");
        }
        if (state->cursors[state->current_level].y >= 0) {
            GOTO(2 * state->cursors[state->current_level].x, state->cursors[state->current_level].y + 1);
            scr_printf("\033[41;30;1m@@\033[m");
        }
    }

    if (state->debug.pos) {
        GOTO(0, 0);
        PRINTF_DATA(state->cursors[state->current_level].x);
        scr_printf(",");
        PRINTF_DATA(state->cursors[state->current_level].y);
    }

    int bottom = HEIGHT_TILES + 1;

    if (debugany()) bottom ++;
    if (state->debug.data) {
        GOTO(0, bottom); bottom ++;
        switch (state->current_state) {
            case EDIT_ROOMDETAILS:
                scr_printf("bk\033[1;4mg\033[mrnd: ");PRINTF_DATA(room.background);
                scr_printf(", \033[1;4mt\033[miles: ");PRINTF_DATA(room.tile_offset);
                scr_printf(", \033[1;4md\033[mmg: ");PRINTF_DATA(room.room_damage);
                scr_printf(", gravity (\033[1;4m|\033[m): ");PRINTF_DATA(room.gravity_vertical);
                scr_printf(", gravity (\033[1;4m-\033[m): ");PRINTF_DATA(room.gravity_horizontal);
                break;

            case EDIT_ROOMDETAILS_NUM:
                if (state->room_detail == 'g') {
                    scr_printf("\033[1;4mbkgrnd\033[m: ");
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            scr_printf("%x", state->partial_byte & 0xFF);
                        } else {
                            scr_printf("%u", state->partial_byte & 0xFF);
                        }
                    } else {
                        scr_printf("\033[4;5m_\033[m");
                    }
                    scr_printf("\033[4;5m_\033[m");
                } else {
                    scr_printf("bkgrnd: ");PRINTF_DATA(room.background);
                }
                if (state->room_detail == 't') {
                    scr_printf(", \033[1;4mt\033[miles: ");
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            scr_printf("%x", state->partial_byte & 0xFF);
                        } else {
                            scr_printf("%u", state->partial_byte & 0xFF);
                        }
                    } else {
                        scr_printf("\033[4;5m_\033[m");
                    }
                    scr_printf("\033[4;5m_\033[m");
                } else {
                    scr_printf(", tiles: ");PRINTF_DATA(room.tile_offset);
                }
                if (state->room_detail == 'd') {
                    scr_printf(", \033[1;4md\033[mmg: ");
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            scr_printf("%x", state->partial_byte & 0xFF);
                        } else {
                            scr_printf("%u", state->partial_byte & 0xFF);
                        }
                    } else {
                        scr_printf("\033[4;5m_\033[m");
                    }
                    scr_printf("\033[4;5m_\033[m");
                } else {
                    scr_printf(", dmg: ");PRINTF_DATA(room.room_damage);
                }
                if (state->room_detail == '|') {
                    scr_printf(", gravity (\033[1;4m|\033[m): ");
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            scr_printf("%x", state->partial_byte & 0xFF);
                        } else {
                            scr_printf("%u", state->partial_byte & 0xFF);
                        }
                    } else {
                        scr_printf("\033[4;5m_\033[m");
                    }
                    scr_printf("\033[4;5m_\033[m");
                } else {
                    scr_printf(", gravity (|): ");PRINTF_DATA(room.gravity_vertical);
                }
                if (state->room_detail == '-') {
                    scr_printf(", gravity (\033[1;4m-\033[m): ");
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            scr_printf("%x", state->partial_byte & 0xFF);
                        } else {
                            scr_printf("%u", state->partial_byte & 0xFF);
                        }
                    } else {
                        scr_printf("\033[4;5m_\033[m");
                    }
                    scr_printf("\033[4;5m_\033[m");
                } else {
                    scr_printf(", gravity (-): ");PRINTF_DATA(room.gravity_horizontal);
                }
                break;

            default:
                scr_printf("bkgrnd: ");PRINTF_DATA(room.background);
                scr_printf(", tiles: ");PRINTF_DATA(room.tile_offset);
                scr_printf(", dmg: ");PRINTF_DATA(room.room_damage);
                scr_printf(", gravity (|): ");PRINTF_DATA(room.gravity_vertical);
                scr_printf(", gravity (-): ");PRINTF_DATA(room.gravity_horizontal);
        }
    }
    if (state->debug.unknowns) {
        GOTO(0, bottom); bottom ++;
        switch (state->current_state) {
            case EDIT_ROOMDETAILS:
                scr_printf("UNKNOWN_\033[1;4mb\033[m: ");PRINTF_DATA(room.UNKNOWN_b);
                scr_printf(", UNKNOWN_\033[1;4mc\033[m: ");PRINTF_DATA(room.UNKNOWN_c);
                scr_printf(", UNKNOWN_\033[1;4me\033[m: ");PRINTF_DATA(room._num_switches & 0x3);
                scr_printf(", UNKNOWN_\033[1;4mf\033[m: ");PRINTF_DATA(room.UNKNOWN_f);
                break;

            case EDIT_ROOMDETAILS_NUM:
                if (state->room_detail == 'b') {
                    scr_printf("UNKNOWN_\033[1;4mb\033[m: ");
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            scr_printf("%x", state->partial_byte & 0xFF);
                        } else {
                            scr_printf("%u", state->partial_byte & 0xFF);
                        }
                    } else {
                        scr_printf("\033[4;5m_\033[m");
                    }
                    scr_printf("\033[4;5m_\033[m");
                } else {
                    scr_printf("UNKNOWN_b: ");PRINTF_DATA(room.UNKNOWN_b);
                }

                if (state->room_detail == 'c') {
                    scr_printf(", UNKNOWN_\033[1;4mc\033[m: ");
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            scr_printf("%x", state->partial_byte & 0xFF);
                        } else {
                            scr_printf("%u", state->partial_byte & 0xFF);
                        }
                    } else {
                        scr_printf("\033[4;5m_\033[m");
                    }
                    scr_printf("\033[4;5m_\033[m");
                } else {
                    scr_printf(", UNKNOWN_c: ");PRINTF_DATA(room.UNKNOWN_c);
                }

                if (state->room_detail == 'e') {
                    scr_printf(", UNKNOWN_\033[1;4me\033[m: ");
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            scr_printf("%x", state->partial_byte & 0xFF);
                        } else {
                            scr_printf("%u", state->partial_byte & 0xFF);
                        }
                    } else {
                        scr_printf("\033[4;5m_\033[m");
                    }
                    scr_printf("\033[4;5m_\033[m");
                } else {
                    scr_printf(", UNKNOWN_e: ");PRINTF_DATA(room._num_switches & 0x3);
                }

                if (state->room_detail == 'f') {
                    scr_printf(", UNKNOWN_\033[1;4mf\033[m: ");
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            scr_printf("%x", state->partial_byte & 0xFF);
                        } else {
                            scr_printf("%u", state->partial_byte & 0xFF);
                        }
                    } else {
                        scr_printf("\033[4;5m_\033[m");
                    }
                    scr_printf("\033[4;5m_\033[m");
                } else {
                    scr_printf(", UNKNOWN_f: ");PRINTF_DATA(room.UNKNOWN_f);
                }

                break;

            default:
                scr_printf("UNKNOWN_b: ");PRINTF_DATA(room.UNKNOWN_b);
                scr_printf(", UNKNOWN_c: ");PRINTF_DATA(room.UNKNOWN_c);
                scr_printf(", UNKNOWN_e: ");PRINTF_DATA(room._num_switches & 0x3);
                scr_printf(", UNKNOWN_f: ");PRINTF_DATA(room.UNKNOWN_f);
        }
    }
    if (state->debug.neighbours) {
        char neighbour_name[25] = {0};
        struct DecompressedRoom neighbour_room = {0};
#define READ_NEIGHBOUR(id) do { \
neighbour_room = state->rooms.rooms[(id)].data; \
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

        if (state->current_state == EDIT_ROOMDETAILS) {
            GOTO(0, bottom); bottom ++;
            scr_printf("\033[1;4mLEFT\033[m: ");PRINTF_DATA(room.room_west);
            READ_NEIGHBOUR(room.room_west);
            scr_printf(" - \"%s\"", neighbour_name);
            GOTO(0, bottom); bottom ++;
            scr_printf("\033[1;4mDOWN\033[m: ");PRINTF_DATA(room.room_south);
            READ_NEIGHBOUR(room.room_south);
            scr_printf(" - \"%s\"", neighbour_name);
            GOTO(0, bottom); bottom ++;
            scr_printf("\033[1;4mUP\033[m: ");PRINTF_DATA(room.room_north);
            READ_NEIGHBOUR(room.room_north);
            scr_printf(" - \"%s\"", neighbour_name);
            GOTO(0, bottom); bottom ++;
            scr_printf("\033[1;4mRIGHT\033[m: ");PRINTF_DATA(room.room_east);
            READ_NEIGHBOUR(room.room_east);
            scr_printf(" - \"%s\"", neighbour_name);
        } else {
            GOTO(0, bottom); bottom ++;
            scr_printf("left: ");PRINTF_DATA(room.room_west);
            READ_NEIGHBOUR(room.room_west);
            scr_printf(" - \"%s\"", neighbour_name);
            GOTO(0, bottom); bottom ++;
            scr_printf("down: ");PRINTF_DATA(room.room_south);
            READ_NEIGHBOUR(room.room_south);
            scr_printf(" - \"%s\"", neighbour_name);
            GOTO(0, bottom); bottom ++;
            scr_printf("up: ");PRINTF_DATA(room.room_north);
            READ_NEIGHBOUR(room.room_north);
            scr_printf(" - \"%s\"", neighbour_name);
            GOTO(0, bottom); bottom ++;
            scr_printf("right: ");PRINTF_DATA(room.room_east);
            READ_NEIGHBOUR(room.room_east);
            scr_printf(" - \"%s\"", neighbour_name);
        }
    }

    if (state->current_state == EDIT_ROOMNAME) {
        GOTO(0, bottom); bottom +=2;
        scr_printf("\nEnter new room name in space highlighted at top of screen");
        goto show_help_if_needed;
    }

    if (state->current_state == GOTO_SWITCH) {
        GOTO(0, bottom);
        scr_printf("\nEnter switch id (in hex) or q to go to main view");
    }

    if (state->current_state == GOTO_OBJECT) {
        GOTO(0, bottom);
        scr_printf("\nEnter object id (in hex) or q to go to main view");
    }

    if (state->current_state == TOGGLE_DISPLAY) {
        GOTO(0, bottom); bottom +=2;
        scr_printf("\nEnter type to toggle, a for all, Ctrl-? for help");
        goto show_help_if_needed;
    }

    if (state->current_state == EDIT_ROOMDETAILS) {
        GOTO(0, bottom); bottom +=2;
        scr_printf("\nEnter element to edit, Ctrl-? for help");
        goto show_help_if_needed;
    }

    if (state->debug.objects) {
        GOTO(0, bottom); bottom ++;
        if (object_underneath != NULL) {
            for (size_t i = object_underneath - room.objects + 1; i < room.num_objects; i ++) {
                if (room.objects[i].x == x && room.objects[i].y == y) {
                    fprintf(stderr, "%s:%d: %s: UNIMPLEMENTED: multiple objects underneath\n", __FILE__, __LINE__, __func__);
                    break;
                }
            }
            switch (object_underneath->type) {
                case BLOCK:
                    scr_printf("block: (x,y)=");
                    PRINTF_DATA(object_underneath->x);
                    scr_printf(",");
                    PRINTF_DATA(object_underneath->y);
                    scr_printf(" (w,h)=");
                    PRINTF_DATA(object_underneath->block.width);
                    scr_printf(",");
                    PRINTF_DATA(object_underneath->block.height);
                    break;

                case SPRITE:
                    scr_printf("sprite: ");
                    switch(object_underneath->sprite.type) {
                        case SHARK: 
                            switch (object_underneath->sprite.damage) {
                                case 1:
                                case 2:
                                    scr_printf("Shark");
                                    break;

                                case 3: scr_printf("Mysterio"); break;
                                case 4: scr_printf("Mary Jane"); break;

                                default: UNREACHABLE();
                            }
                            break;

                        case MUMMY: scr_printf("Mummy"); break;
                        case BLUE_MAN: scr_printf("Blue man"); break;
                        case WOLF: scr_printf("Wolf"); break;
                        case R2D2: scr_printf("R2D2"); break;
                        case DINOSAUR: scr_printf("Dinosaur"); break;
                        case RAT: scr_printf("Rat"); break;
                        case SHOTGUN_LADY: scr_printf("Shotgun_lady"); break;

                        default: UNREACHABLE();
                    }
                    scr_printf(" (x,y)=");
                    PRINTF_DATA(object_underneath->x);
                    scr_printf(",");
                    PRINTF_DATA(object_underneath->y);
                    scr_printf(" (dmg)=");
                    PRINTF_DATA(object_underneath->sprite.damage);
                    break;
            }
        } else if (state->current_state == TILE_EDIT) {
            scr_printf("No object here, TODO create new?");
        }
    }

    if (state->debug.switches) {

        GOTO(0, bottom);
    (void)chunk_switch_underneath;
    (void)chunk_underneath;
        struct SwitchObject *switcz = switch_underneath;
        if (switcz) {
            for (size_t i = switcz - room.switches + 1; i < room.num_switches; i ++) {
                if (room.switches[i].chunks.data[0].x == x && room.switches[i].chunks.data[0].y == y) {
                    fprintf(stderr, "%s:%d: %s: UNIMPLEMENTED: multiple switches underneath\n", __FILE__, __LINE__, __func__);
                    break;
                }
            }
        }
        if (!switcz && chunk_underneath) switcz = chunk_switch_underneath;
        if (state->current_state != GOTO_SWITCH && switcz != NULL) {
#define BOOL_S(b) ((b) ? "true" : "false")
            assert(switcz->chunks.length >= 1);
            struct SwitchChunk *preamble = switcz->chunks.data;
            if (state->current_state == EDIT_SWITCHDETAILS) {
                scr_printf("switch ");
                if (switcz == switch_underneath) scr_printf("\033[4;1m");
                PRINTF_DATA((uint16_t)(switcz - room.switches));
                if (switcz == switch_underneath) scr_printf("\033[m");
                scr_printf(": (x,y)=%d,%d (\033[1;4me\033[mn\033[mtry)=%s (\033[1;4mo\033[mnce)=%s (\033[1;4ms\033[mide)=%s\n",
                        preamble->x, preamble->y,
                        BOOL_S(preamble->room_entry), BOOL_S(preamble->one_time_use),
                        SWITCH_SIDE(preamble->side));
            } else {
                scr_printf("switch ");
                if (state->current_state == TILE_EDIT && switcz == switch_underneath) scr_printf("\033[4;1m");
                PRINTF_DATA((uint16_t)(switcz - room.switches));
                if (state->current_state == TILE_EDIT && switcz == switch_underneath) scr_printf("\033[m");
                scr_printf(": (x,y)=%d,%d (entry)=%s (once)=%s (side)=%s\n",
                        preamble->x, preamble->y,
                        BOOL_S(preamble->room_entry), BOOL_S(preamble->one_time_use),
                        SWITCH_SIDE(preamble->side));
            }
            bottom +=2;
            if (switcz->chunks.length > 1) {
                if (state->current_state == EDIT_SWITCHDETAILS) {
                    scr_printf("  \033[1;4mc\033[mhunks:\n");
                } else {
                    scr_printf("  chunks:\n");
                }
                bottom ++;
                for (size_t i = 1; i < switcz->chunks.length; i ++) {
                    scr_printf("    ");
                    if ((state->current_state != EDIT_SWITCHDETAILS_SELECT_CHUNK && switcz == chunk_switch_underneath && (size_t)(chunk_underneath - switcz->chunks.data) == i) ||
                            (state->current_state == EDIT_SWITCHDETAILS_SELECT_CHUNK || state->current_chunk == i)) {
                        scr_printf("\033[1;4m(");
                        PRINTF_DATA((uint16_t)i);
                        scr_printf(")\033[m ");
                    } else {
                        scr_printf("(");
                        PRINTF_DATA((uint16_t)i);
                        scr_printf(") ");
                    }
                    if (state->current_chunk == i) {
                        scr_printf("\033[1;4mt\033[mype ");
                    } else {
                        scr_printf("type ");
                    }
                    struct SwitchChunk *chunk = switcz->chunks.data + i;
                    switch (chunk->type) {
                        case PREAMBLE: UNREACHABLE();
                        case TOGGLE_BLOCK: {
                            size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                            size_t point = chunk->y * WIDTH_TILES + chunk->x;
                            if (point >= overflow) {
                                scr_printf("memory:");
                                size_t offset = point - overflow;
                                size_t end = offsetof(struct DecompressedRoom, end_marker) - overflow;
                                if (offset >= end) {
                                    fprintf(stderr, "way out of bounds, overflowing name end by %lu bytes\n", offset - end + 1);
                                    /* assert(false); */
                                }
                                switch (offset) {
                                    case 0: scr_printf(" tile_offset"); break;
                                    case 1: scr_printf(" background"); break;
                                    case 2: scr_printf(" room_north"); break;
                                    case 3: scr_printf(" room_east"); break;
                                    case 4: scr_printf(" room_south"); break;
                                    case 5: scr_printf(" room_west"); break;
                                    case 6: scr_printf(" room_damage"); break;
                                    case 7: scr_printf(" gravity_vertical"); break;
                                    case 8: scr_printf(" gravity_horizontal"); break;
                                    case 9: scr_printf(" UNKNOWN_b"); break;
                                    case 10: scr_printf(" UNKNOWN_c"); break;
                                    case 11: scr_printf(" num_objects"); break;
                                    case 12: scr_printf(" _num_switches"); break;
                                    case 13: scr_printf(" UNKNOWN_f"); break;

                                    case 14:
                                    case 15:
                                    case 16:
                                    case 17:
                                    case 18:
                                    case 19:
                                    case 20:
                                    case 21:
                                    case 22:
                                    case 23:
                                    case 24:
                                    case 25:
                                    case 26:
                                    case 27:
                                    case 28:
                                    case 29:
                                    case 30:
                                    case 31:
                                    case 32:
                                    case 33:
                                    case 34:
                                    case 35:
                                    case 36:
                                    case 37:
                                             scr_printf(" name[%lu]", offset-14);
                                             break;

                                    default: scr_printf(" out_of_bounds[%lu]", offset-end);
                                }
                                if (state->current_chunk == i) {
                                    scr_printf(" (\033[4;1mo\033[mn/\033[4;1mo\033[mff)=");
                                    if (state->switch_on) {
                                        if (state->partial_byte) {
                                            if (state->debug.hex) {
                                                scr_printf("%x", state->partial_byte & 0xFF);
                                            } else {
                                                scr_printf("%u", state->partial_byte & 0xFF);
                                            }
                                        } else {
                                            scr_printf("\033[4;5m%x\033[m", chunk->on / (state->debug.hex ? 16 : 10));
                                        }
                                        scr_printf("\033[4;5m%x\033[m", chunk->on % (state->debug.hex ? 16 : 10));
                                    } else {
                                        PRINTF_DATA(chunk->on);
                                    }
                                    scr_printf("/");
                                    if (state->switch_on) {
                                        PRINTF_DATA(chunk->off);
                                    } else {
                                        if (state->partial_byte) {
                                            if (state->debug.hex) {
                                                scr_printf("%x", state->partial_byte & 0xFF);
                                            } else {
                                                scr_printf("%u", state->partial_byte & 0xFF);
                                            }
                                        } else {
                                            scr_printf("\033[4;5m%x\033[m", chunk->off / (state->debug.hex ? 16 : 10));
                                        }
                                        scr_printf("\033[4;5m%x\033[m", chunk->off % (state->debug.hex ? 16 : 10));
                                    }
                                    scr_printf(" (size)=%d (di\033[4;1mr\033[m)=%s", chunk->size, chunk->dir == HORIZONTAL ? "horiz" : "vert");
                                } else {
                                    scr_printf(" (on/off)=");
                                    PRINTF_DATA(chunk->on);
                                    scr_printf("/");
                                    PRINTF_DATA(chunk->off);
                                    scr_printf(" (size)=%d (dir)=%s", chunk->size, chunk->dir == HORIZONTAL ? "horiz" : "vert");
                                }
                            } else {
                                scr_printf("block: ");
                                if (state->current_chunk == i) {
                                    scr_printf("\033[3%ld;40m", (i % 7));
                                }
                                scr_printf("(x,y)=");
                                PRINTF_DATA(chunk->x);
                                scr_printf(",");
                                PRINTF_DATA(chunk->y);
                                if (state->current_chunk == i) scr_printf("\033[m");
                                if (state->current_chunk == i) {
                                    scr_printf(" (%s)=%d (\033[4;1mo\033[mn/\033[4;1mo\033[mff)=", (chunk->dir == VERTICAL ? "height" : "width"), chunk->size);
                                    if (state->switch_on) {
                                        if (state->partial_byte) {
                                            if (state->debug.hex) {
                                                scr_printf("%x", state->partial_byte & 0xFF);
                                            } else {
                                                scr_printf("%u", state->partial_byte & 0xFF);
                                            }
                                        } else {
                                            scr_printf("\033[4;5m%x\033[m", chunk->on / (state->debug.hex ? 16 : 10));
                                        }
                                        scr_printf("\033[4;5m%x\033[m", chunk->on % (state->debug.hex ? 16 : 10));
                                    } else {
                                        PRINTF_DATA(chunk->on);
                                    }
                                    scr_printf("/");
                                    if (state->switch_on) {
                                        PRINTF_DATA(chunk->off);
                                    } else {
                                        if (state->partial_byte) {
                                            if (state->debug.hex) {
                                                scr_printf("%x", state->partial_byte & 0xFF);
                                            } else {
                                                scr_printf("%u", state->partial_byte & 0xFF);
                                            }
                                        } else {
                                            scr_printf("\033[4;5m%x\033[m", chunk->off / (state->debug.hex ? 16 : 10));
                                        }
                                        scr_printf("\033[4;5m%x\033[m", chunk->off % (state->debug.hex ? 16 : 10));
                                    }
                                } else {
                                    scr_printf(" (%s)=%d (on/off)=", (chunk->dir == VERTICAL ? "height" : "width"), chunk->size);
                                    PRINTF_DATA(chunk->on);
                                    scr_printf("/");
                                    PRINTF_DATA(chunk->off);
                                }
                                if (state->current_chunk == i) {
                                    for (int offset = 0; offset < chunk->size; offset ++) {
                                        if (chunk->dir == HORIZONTAL) {
                                            GOTO(2 * (chunk->x + offset), chunk->y + 1);
                                        } else {
                                            GOTO(2 * chunk->x, chunk->y + offset + 1);
                                        }
                                        assert(state->current_switch);
                                        scr_printf("\033[3%ld;40m", (i % 7));
                                        if (state->partial_byte) {
                                            if (state->debug.hex) {
                                                scr_printf("%x", state->partial_byte & 0xFF);
                                            } else {
                                                scr_printf("%u", state->partial_byte & 0xFF);
                                            }
                                        } else {
                                            scr_printf("\033[4;1m%x\033[m", (state->switch_on ? chunk->on : chunk->off) / (state->debug.hex ? 16 : 10));
                                        }
                                        scr_printf("\033[3%ld;40m", (i % 7));
                                        scr_printf("\033[4;1m%x\033[m", (state->switch_on ? chunk->on : chunk->off) % (state->debug.hex ? 16 : 10));
                                    }
                                    GOTO(0, bottom - 1);
                                }
                            }
                        }; break;

                        case TOGGLE_BIT:
                        {
                            struct DecompressedRoom target_room = state->rooms.rooms[chunk->room_idx].data;
                            char target_name[25] = {0};
                            assert(C_ARRAY_LEN(target_name) >= C_ARRAY_LEN(target_room.name));
                            strncpy(target_name, target_room.name, C_ARRAY_LEN(target_room.name));
                            for (int i = C_ARRAY_LEN(target_name) - 1; i >= 0; i --) {
                                if (target_name[i] == '\0' || isspace(target_name[i])) {
                                    target_name[i] = '\0';
                                } else {
                                    break;
                                }
                            }
                            if (state->current_chunk == i) {
                                scr_printf("toggle: (\033[4;1mr\033[moom) %s (switch) ", target_name);
                                if (state->partial_byte) {
                                    if (state->debug.hex) {
                                        scr_printf("%x", state->partial_byte & 0xFF);
                                    } else {
                                        scr_printf("%u", state->partial_byte & 0xFF);
                                    }
                                } else {
                                    scr_printf("\033[4;5m%x\033[m", chunk->switch_idx / (state->debug.hex ? 16 : 10));
                                }
                                scr_printf("\033[4;5m%x\033[m", chunk->switch_idx % (state->debug.hex ? 16 : 10));
                                scr_printf(" (o\033[4;1mn\033[m/\033[4;1mo\033[mff)=%x/%x", chunk->on, chunk->off);
                            } else {
                                scr_printf("toggle: (room) %s (switch) %u (on/off)=%d/%d",
                                        target_name, chunk->switch_idx, chunk->on, chunk->off);
                            }
                        }; break;

                        case TOGGLE_OBJECT:
                        {
                            if (state->current_chunk == i) {
                                scr_printf("object: (\033[4;1mi\033[mdx)=%d (te\033[4;1ms\033[mt)=%d", chunk->index, chunk->test >> 4);
                            } else {
                                scr_printf("object: (idx)=%d (test)=%d", chunk->index, chunk->test >> 4);
                            }
                            scr_printf(" (value)=");
                            switch (chunk->value & MOVE_LEFT) {
                                case MOVE_LEFT:
                                    switch (chunk->value & MOVE_UP) {
                                        case MOVE_UP: scr_printf("up+left"); break;
                                        case MOVE_DOWN: scr_printf("down+left"); break;
                                        case '\0': scr_printf("left"); break;
                                    }
                                    break;

                                case MOVE_RIGHT:
                                    switch (chunk->value & MOVE_UP) {
                                        case MOVE_UP: scr_printf("up+right"); break;
                                        case MOVE_DOWN: scr_printf("down+right"); break;
                                        case '\0': scr_printf("right"); break;
                                    }
                                    break;

                                case '\0':
                                    switch (chunk->value & MOVE_UP) {
                                        case MOVE_UP: scr_printf("up"); break;
                                        case MOVE_DOWN: scr_printf("down"); break;
                                        case '\0': scr_printf("stop"); break;
                                    }
                                    break;
                            }

                            scr_printf(" (value_without_direction)=");
                            PRINTF_DATA(chunk->value & ~(MOVE_UP | MOVE_DOWN | MOVE_LEFT | MOVE_RIGHT));
                            scr_printf(" (value_raw)=");
                            if (state->current_chunk == i) {
                                if (state->partial_byte) {
                                    if (state->debug.hex) {
                                        scr_printf("%x", state->partial_byte & 0xFF);
                                    } else {
                                        scr_printf("%u", state->partial_byte & 0xFF);
                                    }
                                } else {
                                    scr_printf("\033[4;5m%x\033[m", chunk->value / (state->debug.hex ? 16 : 10));
                                }
                                scr_printf("\033[4;5m%x\033[m", chunk->value % (state->debug.hex ? 16 : 10));
                            } else {
                                PRINTF_DATA(chunk->value);
                            }
                        }; break;

                        default: UNREACHABLE();
                    }
                    if (i < switcz->chunks.length - 1) {
                        scr_printf("\n");
                        bottom ++;
                    }
                }
            }
        } else if (state->current_state == TILE_EDIT) {
            /* scr_printf("No switch here, Ctrl-s to a create new one"); */
        }

        uint8_t found = 0;
        for (size_t s = 0; s < room.num_switches; s ++) {
            struct SwitchObject *sw = room.switches + s;
            int x = sw->chunks.data[0].x;
            int y = sw->chunks.data[0].y;
            if (x >= WIDTH_TILES || y >= HEIGHT_TILES) {
                found = 1;
                break;
            }
        }
        if (found) {
            GOTO(0, bottom);
            scr_printf("\n\nOut of bounds switches:\n");
            bottom +=3;
        }
        for (size_t s = 0; s < room.num_switches; s ++) {
            struct SwitchObject *sw = room.switches + s;
            int x = sw->chunks.data[0].x;
            int y = sw->chunks.data[0].y;
            if (x >= WIDTH_TILES || y >= HEIGHT_TILES) {
                GOTO(0, bottom); bottom ++;
#define BOOL_S(b) ((b) ? "true" : "false")
                if (state->current_state == EDIT_SWITCHDETAILS && state->current_switch == s) {
                    scr_printf("\033[1;4mswitch id ");
                    PRINTF_DATA((uint16_t)s);
                    scr_printf("\033[m: (x,y)=");
                    PRINTF_DATA(sw->chunks.data[0].x);
                    scr_printf(",");
                    PRINTF_DATA(sw->chunks.data[0].y);
                    scr_printf(" (\033[1;4me\033[mntry)=%s (\033[1;4mo\033[mnce)=%s (\033[1;4ms\033[mide)=%s\n",
                            BOOL_S(sw->chunks.data[0].room_entry), BOOL_S(sw->chunks.data[0].one_time_use),
                            SWITCH_SIDE(sw->chunks.data[0].side));

                } else {
                    scr_printf("switch id ");
                    if (state->current_state == GOTO_SWITCH) {
                        scr_printf("\033[4%ld;30;1m", (s % 4) + 4);
                        PRINTF_DATA((uint16_t)s);
                        scr_printf("\033[m");
                    } else {
                        PRINTF_DATA((uint16_t)s);
                    }
                    scr_printf(": (x,y)=");
                    PRINTF_DATA(sw->chunks.data[0].x);
                    scr_printf(",");
                    PRINTF_DATA(sw->chunks.data[0].y);
                    scr_printf(" (entry)=%s (once)=%s (side)=%s\n",
                            BOOL_S(sw->chunks.data[0].room_entry), BOOL_S(sw->chunks.data[0].one_time_use),
                            SWITCH_SIDE(sw->chunks.data[0].side));
                }

                bottom ++;
                if (sw->chunks.length > 1) {
                    if (state->current_state == EDIT_SWITCHDETAILS) {
                        scr_printf("  \033[1;4mc\033[mhunks:\n");
                    } else {
                        scr_printf("  chunks:\n");
                    }
                    bottom ++;
                    for (size_t i = 1; i < sw->chunks.length; i ++) {
                        if (state->current_chunk == i) {
                            fprintf(stderr, "%s:%d: %s: UNIMPLEMENTED: selected chunk\n", __FILE__, __LINE__, __func__);
                            UNREACHABLE();
                        }
                        scr_printf("    ");
                        if (state->current_state == EDIT_SWITCHDETAILS_SELECT_CHUNK) {
                            scr_printf("\033[1;4m(");
                            PRINTF_DATA((uint16_t)i);
                            scr_printf(")\033[m ");
                        } else {
                            scr_printf("(");
                            PRINTF_DATA((uint16_t)i);
                            scr_printf(") ");
                        }
                        struct SwitchChunk *chunk = sw->chunks.data + i;
                        switch (chunk->type) {
                            case PREAMBLE: UNREACHABLE();
                            case TOGGLE_BLOCK: {
                                size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                                size_t point = chunk->y * WIDTH_TILES + chunk->x;
                                if (point >= overflow) {
                                    scr_printf("memory:");
                                    size_t offset = point - overflow;
                                    size_t end = offsetof(struct DecompressedRoom, end_marker) - overflow;
                                    if (offset >= end) {
                                        fprintf(stderr, "way out of bounds, overflowing name end by %lu bytes\n", offset - end + 1);
                                        /* assert(false); */
                                    }
                                    switch (offset) {
                                        case 0: scr_printf(" tile_offset"); break;
                                        case 1: scr_printf(" background"); break;
                                        case 2: scr_printf(" room_north"); break;
                                        case 3: scr_printf(" room_east"); break;
                                        case 4: scr_printf(" room_south"); break;
                                        case 5: scr_printf(" room_west"); break;
                                        case 6: scr_printf(" room_damage"); break;
                                        case 7: scr_printf(" gravity_vertical"); break;
                                        case 8: scr_printf(" gravity_horizontal"); break;
                                        case 9: scr_printf(" UNKNOWN_b"); break;
                                        case 10: scr_printf(" UNKNOWN_c"); break;
                                        case 11: scr_printf(" num_objects"); break;
                                        case 12: scr_printf(" _num_switches"); break;
                                        case 13: scr_printf(" UNKNOWN_f"); break;

                                        case 14:
                                        case 15:
                                        case 16:
                                        case 17:
                                        case 18:
                                        case 19:
                                        case 20:
                                        case 21:
                                        case 22:
                                        case 23:
                                        case 24:
                                        case 25:
                                        case 26:
                                        case 27:
                                        case 28:
                                        case 29:
                                        case 30:
                                        case 31:
                                        case 32:
                                        case 33:
                                        case 34:
                                        case 35:
                                        case 36:
                                        case 37:
                                                 scr_printf(" name[%lu]", offset-14);
                                                 break;

                                    default: scr_printf(" out_of_bounds[%lu]", offset-end);
                                    }
                                    scr_printf(" (on/off)=%02x/%02x (size)=%d", chunk->on, chunk->off, chunk->size);
                                } else {
                                    scr_printf("block - (x,y)=");
                                    PRINTF_DATA(chunk->x);
                                    scr_printf(",");
                                    PRINTF_DATA(chunk->y);
                                    scr_printf(" (%s)=%d (on/off)=", (chunk->dir == VERTICAL ? "height" : "width"), chunk->size);
                                    PRINTF_DATA(chunk->on);
                                    scr_printf("/");
                                    PRINTF_DATA(chunk->off);
                                }
                            }; break;

                            case TOGGLE_BIT:
                            {
                                scr_printf("bit - (idx)=%d (on/off)=%d/%d (mask)=", chunk->index, chunk->on, chunk->off);
                                PRINTF_DATA(chunk->bitmask);
                            }; break;

                            case TOGGLE_OBJECT:
                            {
                                scr_printf("object - (idx)=%d (test)=", chunk->index >> 4);
                                PRINTF_DATA(chunk->test);
                                scr_printf(" (value)=");
                                switch (chunk->value & MOVE_LEFT) {
                                    case MOVE_LEFT:
                                        switch (chunk->value & MOVE_UP) {
                                            case MOVE_UP: scr_printf("up+left"); break;
                                            case MOVE_DOWN: scr_printf("down+left"); break;
                                            case '\0': scr_printf("left"); break;
                                        }
                                        break;

                                    case MOVE_RIGHT:
                                        switch (chunk->value & MOVE_UP) {
                                            case MOVE_UP: scr_printf("up+right"); break;
                                            case MOVE_DOWN: scr_printf("down+right"); break;
                                            case '\0': scr_printf("right"); break;
                                        }
                                        break;

                                    case '\0':
                                        switch (chunk->value & MOVE_UP) {
                                            case MOVE_UP: scr_printf("up"); break;
                                            case MOVE_DOWN: scr_printf("down"); break;
                                            case '\0': scr_printf("stop"); break;
                                        }
                                        break;
                                }

                                scr_printf(" (value_without_direction)=");
                                PRINTF_DATA(chunk->value & ~(MOVE_UP | MOVE_DOWN | MOVE_LEFT | MOVE_RIGHT));
                                scr_printf(" (value_raw)=");
                                PRINTF_DATA(chunk->value);
                            }; break;

                            default: UNREACHABLE();
                        }
                        if (i < sw->chunks.length - 1) {
                            scr_printf("\n");
                            bottom ++;
                        }
                    }
                }
#undef BOOL_S
            }
        }
    }

    /* GOTO(0, bottom); bottom ++; */
    /* uint8_array rest = state->rooms.rooms[level].rest; */
    /* int pre = scr_printf("Rest (length=%zu):", rest.length); */
    /* for (size_t i = 0; i < rest.length; i ++) { */
    /*     if (pre + 3 * (i + 2) > (size_t)state->screen_dimensions.x) { */
    /*         scr_printf("..."); */
    /*         break; */
    /*     } */
    /*     scr_printf(" "); */
    /*     PRINTF_DATA(rest.data[i]); */
    /* } */

show_help_if_needed:
    if (state->help) show_help();

    screen_flush();
}
