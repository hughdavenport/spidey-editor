#include "editor_types.h"
#include "render.h"
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
        {"o[n]", "goto object"},
        {"O", "preview switch under cursor active/idle on the map"},
        {"u/U", "undo/redo"},
        {"mX", "record to slot X; m=stop"},
        {",X", "replay slot X; ,,=repeat; ,.=saved"},
        {"Ctrl-p", "play (play.sh)"},
        {"q", "quit"},
        {"Ctrl-?", "help"},
        {"Escape", "cancel"},
        {"Ctrl-h", "toggle hex"},
        {"Ctrl-t", "tile edit mode"},
        {"Ctrl-d[acdnopst]", "toggle display"},
        {0},
    },

    [TILE_EDIT]={
        {"0-9a-fA-F", "Enter hex nibble"},
        {"Shift+dir", "move thing"},
        {"Alt+dir", "stretch thing"},
        {"Alt+S+dir", "shrink thing"},
        {"Left/h", "Move cursor left"},
        {"Down/j", "Move cursor down"},
        {"Up/k", "Move cursor up"},
        {"Right/l", "Move cursor right"},
        {"Left click", "move cursor (drag object: moves it)"},
        {"Right/Ctrl+click", "move active item to tile"},
        {"u/U", "undo/redo"},
        {"mX", "record to slot X; m=stop"},
        {",X", "replay slot X; ,,=repeat; ,.=saved"},
        {"R", "edit room name"},
        {"r[nn]", "goto room"},
        {"Ctrl-r[gtdbcef|-]", "edit room detail (hjkl/arrows = room links)"},
        {"Ctrl-s", "create/edit switch"},
        {"Ctrl-o", "create/cycle object/sprites (over a switch instead: preview it active/idle on the map)"},
        {"Enter", "edit object's MOVERTAB.DAT entry (over an object)"},
        {"s[n]", "goto switch"},
        {"o[n]", "goto object"},
        {"Delete/Backspace", "remove nibble / delete / clear tile"},
        {"x/DEL/BS", "cut (yank + delete)"},
        {"y", "yank (obj/switch/block; empty cell: room)"},
        {"p", "paste at cursor (room: replaces this room)"},
        {"+", "increase id"},
        {"-", "decrease id"},
        {"Ctrl-p", "play (play.sh)"},
        {"q", "quit"},
        {"Ctrl-?", "help"},
        {"Escape", "cancel"},
        {"Ctrl-h", "toggle hex"},
        {"Ctrl-t", "tile edit mode"},
        {"Ctrl-d[acdnopst]", "toggle display"},
        {0},
    },

    [GOTO_OBJECT]={
        {"0-9a-fA-F", "object id (highlighted on map)"},
        {"ESC", "cancel"},
        {"q", "cancel"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [EDIT_MOVERTAB_OBJECT]={
        {"d", "sprite: toggle dead/alive"},
        {"f", "sprite (alive): cycle flip none/horizontal/vertical/both; block: cycle horizontal stopped/right/left + speed"},
        {"m/M", "sprite (alive): cycle mode stopped/patrol/chase"},
        {"s", "sprite (chasing): toggle stopped / actively moving"},
        {"w", "sprite (patrol/chasing): toggle stop at wall / turn around; block (moving horizontally): toggle stop at wall / bounce"},
        {"u", "block: cycle vertical stopped/down/up + speed"},
        {"e", "sprite (patrol/chasing): toggle fall at edge / turn around; block (moving vertically): toggle stop at wall / bounce"},
        {"g", "sprite: cycle damage 1-4"},
        {"hjkl, arrows", "nudge the x/y tile offset (bytes 0/1)"},
        {"r", "toggle raw hex entry of byte 2 (no glitch protection) / back to attributes"},
        {"0-9a-fA-F", "raw mode: set byte 2 (2 hex digits)"},
        {"ESC/q", "cancel"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {"Ctrl-p", "play (play.sh)"},
        {0},
    },

    [GOTO_SWITCH]={
        {"0-9a-fA-F", "switch id (highlighted on map)"},
        {"ESC", "cancel"},
        {"q", "cancel"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [GOTO_ROOM]={
        {"0-9a-fA-F", "room number"},
        {"any printable char", "navigate by room name"},
        {"UP/Alt+k", "room above"},
        {"DOWN/Alt+j", "room below"},
        {"LEFT/Alt+h", "room left"},
        {"RIGHT/Alt+l", "room right"},
        {"DEL/BACKSPACE", "delete from name"},
        {"ESC/Ctrl+q", "cancel"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [GOTO_COORD]={
        {"0-9 / 0-9a-f", "x then y (two digits each)"},
        {"BACKSPACE/DEL", "undo digit"},
        {"ESC", "cancel"},
        {"Ctrl-h", "toggle hex input"},
        {"Ctrl-?", "help"},
        {0},
    },

    [EDIT_ROOMNAME]={
        {"any printable char", "insert character"},
        {"DEL", "delete character after cursor"},
        {"BACKSPACE", "delete character before cursor"},
        {"ENTER", "save room name"},
        {"LEFT", "move cursor left"},
        {"RIGHT", "move cursor right"},
        {"Home/End", "move cursor to start/end"},
        {"ESC", "cancel"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [TOGGLE_DISPLAY]={
        {"a", "all debug info"},
        {"c", "Spider-Man at the cursor (GUI, default on)"},
        {"d", "room data"},
        {"n", "neighbours"},
        {"o", "objects"},
        {"p", "position"},
        {"s", "switches"},
        {"t", "tile types (GUI)"},
        {"ESC", "cancel"},
        {"q", "cancel"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [EDIT_ROOMDETAILS]={
        {"g", "background"},
        {"t", "tileset"},
        {"d", "damage"},
        {"|", "vertical gravity"},
        {"-", "horizontal gravity"},

        // UNKNOWN_b isn't shown here at all any more: every original room has it set to 1,
        // and testing (disassembly + play) found no code path that ever reads it, so there's
        // nothing useful to look at. Object base / switch base aren't shown either - they're
        // recomputed automatically from the file's layout on every save and weren't useful in
        // the UI (Hugh: "don't think they are of any use").

        {"LEFT/h", "room left"},
        {"RIGHT/l", "room right"},
        {"UP/k", "room above"},
        {"DOWN/j", "room below"},

        {"ESC/q/Ctrl-r", "cancel"},
        {"Ctrl-p", "play (play.sh)"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [EDIT_ROOMDETAILS_NUM]={
        {"0-9a-fA-F", "value"},
        {"ESC/q/Ctrl-r", "cancel"},
        {"Ctrl-p", "play (play.sh)"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [EDIT_ROOMDETAILS_ROOM]={
        {"0-9a-fA-F", "room number"},
        {"any printable char", "change character"},
        {"DEL/BACKSPACE", "delete character"},
        {"ESC/Ctrl-q/Ctrl-r", "cancel"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [EDIT_SWITCHDETAILS]={
        {"e", "flip on-entry bit (only for non-blank tiles)"},
        {"o", "flip one-time-only bit"},
        {"O", "preview this switch active/idle on the map"},
        {"s", "rotate switch side"},
        {"Left/h", "side: left"},
        {"Down/j", "side: bottom"},
        {"Up/k", "side: top"},
        {"Right/l", "side: right"},
        {"X", "edit x (0-31 / 0x00-0x1f)"},
        {"Y", "edit y (0-21 / 0x00-0x15 on grid; 22 / 0x16 is off screen)"},
        {"+", "increase switch id"},
        {"-", "decrease switch id"},
        {"y", "yank switch"},
        {"x/DEL/BS", "cut (yank + delete)"},
        {"p", "paste chunk"},
        {"c[n]", "select chunk"},
        {"C", "create chunk"},
        {"ESC/q/Ctrl-s", "cancel"},
        {"Ctrl-p", "play (play.sh)"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [EDIT_SWITCHDETAILS_XY]={
        {"0-9a-fA-F", "value (see switchdetails_xy_help - text depends on X vs Y)"},
        {"ESC/q/Ctrl-s", "cancel"},
        {"Ctrl-p", "play (play.sh)"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [EDIT_SWITCHDETAILS_SELECT_CHUNK]={
        {"0-9a-fA-F", "chunk number"},
        {"ESC/q/Ctrl-s", "cancel"},
        {"+", "add chunk"},
        {"Ctrl-p", "play (play.sh)"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS]={
        {"t", "change chunk type"},
        {"y", "copy chunk to clipboard"},
        {"x/DEL/BS", "cut (yank + delete)"},
        {"+", "increase chunk id"},
        {"-", "decrease chunk id"},
        {"s", "cycle through the target room's actual switches"},
        {"r", "change room number"},
        {"n", "cycle on value: noop/turn on/turn off/toggle"},
        {"f", "cycle off value: noop/turn on/turn off/toggle"},
        {"ESC/q/Ctrl-s", "cancel"},
        {"Ctrl-p", "play (play.sh)"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM]={
        {"0-9a-fA-F", "room number"},
        {"any printable char", "change character"},
        {"DEL/BACKSPACE", "delete character"},
        {"ESC/Ctrl-q/Ctrl-s", "cancel"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS]={
        {"t", "change chunk type"},
        {"y", "copy chunk to clipboard"},
        {"x/DEL/BS", "cut (yank + delete)"},
        {"+", "increase chunk id"},
        {"-", "decrease chunk id"},
        {"0-9a-fA-F", "change on/off tile value"},
        {"Left/h", "Move block left"},
        {"Down/j", "Move block down"},
        {"Up/k", "Move block up"},
        {"Right/l", "Move block right"},
        {"^", "increase block size (width/height)"},
        {"v", "decrease block size (width/height)"},
        {"Space", "toggle vertical/horizontal"},
        {"o", "toggle on/off value"},
        {"ESC/q/Ctrl-s", "cancel"},
        {"Ctrl-p", "play (play.sh)"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS]={
        {"t", "change chunk type"},
        {"y", "copy chunk to clipboard"},
        {"x/DEL/BS", "cut (yank + delete)"},
        {"+", "increase chunk id"},
        {"-", "decrease chunk id"},
        {"0-9a-fA-F", "change on/off memory value"},
        {"o", "toggle on/off value"},
        {"^", "increase size (advanced)"},
        {"v", "decrease size (advanced)"},
        {"r", "toggle direction (advanced)"},
        {"Left/Up/h/k", "move memory pointer back"},
        {"Down/Right/j/l/Space", "move memory pointer forward"},
        {"ESC/q/Ctrl-s", "cancel"},
        {"Ctrl-p", "play (play.sh)"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS]={
        {"t", "change chunk type"},
        {"y", "copy chunk to clipboard"},
        {"x/DEL/BS", "cut (yank + delete)"},
        {"+", "increase chunk id"},
        {"-", "decrease chunk id"},
        {"i", "cycle index (idx) within the room's actual objects"},
        {"o", "toggle whether this chunk fires on switch off or on"},
        {"idx a block: s/d/w", "arm speed/direction/walls focus, then hjkl/arrows or |/- adjust it"},
        {"idx a sprite: d/m/M", "toggle dead/alive, cycle mode stopped/patrol/chase (flip is fixed by MOVERTAB.DAT)"},
        {"idx a sprite: s/w/e", "toggle chase's stopped flag / wall-turn / edge-turn"},
        {"r", "toggle raw hex entry of the value byte / back to attributes"},
        {"0-9a-fA-F", "raw mode: set the value byte (2 hex digits)"},
        {"ESC/q/Ctrl-s", "cancel"},
        {"Ctrl-p", "play (play.sh)"},
        {"Ctrl-?", "help"},
        {"Ctrl-h", "toggle hex"},
        {0},
    },

};
_Static_assert(C_ARRAY_LEN(help) == NUM_STATES, "Unhandled for all states");

// EDIT_SWITCHDETAILS_XY's first line depends on which field is being entered: X's full
// 0-31 range is on screen (WIDTH_TILES=32), but Y's on-screen range is only 0-21
// (HEIGHT_TILES=22) - row 22 is the switch-out-of-bounds convention, a single row, not
// a range up to 31.
static const char *switchdetails_xy_help(void) {
    return state->room_detail == 'X'
        ? "value 0-31 / 0x00-0x1f"
        : "value 0-21 / 0x00-0x15 on grid; 22 / 0x16 is off screen";
}

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
        const char *action = (state->current_state == EDIT_SWITCHDETAILS_XY && i == 0)
            ? switchdetails_xy_help() : help[state->current_state][i].action;
        len = strlen(action);
        if (len > x2) x2 = len;
    }
    x = x1 + x2 + 5;
    if (x % 2) x++;
    y += 3;

    /* Use sidebar (right of map) when there is room; otherwise centre-overlay */
    int hx, hy;
    if (MIN_WIDTH + x + 1 <= state->screen_dimensions.x &&
            y <= state->screen_dimensions.y) {
        /* Centre the box in the space to the right of the map */
        hx = MIN_WIDTH + (state->screen_dimensions.x - MIN_WIDTH - x - 1) / 2;
        hy = 1;
    } else {
        hx = state->screen_dimensions.x / 2 - x / 2;
        hy = state->screen_dimensions.y / 2 - y / 2;
    }

    GOTO(hx, hy);
    scr_printf("\033[47;30;1m");
    scr_printf("+");
    for (int i = 0; i < x / 2 - 3; i ++) scr_printf("-");
    scr_printf("help");
    for (int i = 0; i < x / 2 - 3; i ++) scr_printf("-");
    scr_printf("+\033[m ");
    for (int _y = 1; _y < y - 1; _y ++) {
        GOTO(hx, hy + _y);
        scr_printf("\033[47;30;1;m|%*s|\033[40;37;1m ", x - 2, "");
    }
    GOTO(hx, hy + (y - 2));
    scr_printf("\033[47;30;1m");
    scr_printf("+");
    for (int i = 0; i < x - 2; i ++) scr_printf("-");
    scr_printf("+\033[40;37;1m ");
    GOTO(hx, hy + (y - 1));
    scr_printf("\033[m \033[40;37;1m%*s", x, "");
    scr_printf("\033[47;30;1m");

    int line = 1;
    for (size_t i = 0; i < C_ARRAY_LEN(help[state->current_state]); i ++) {
        if (help[state->current_state][i].key == NULL) break;
        GOTO(hx, hy + line); line ++;
        const char *action = (state->current_state == EDIT_SWITCHDETAILS_XY && i == 0)
            ? switchdetails_xy_help() : help[state->current_state][i].action;
        scr_printf("|%-*s - %-*s|", x1, help[state->current_state][i].key, x - x1 - 5, action);
    }
}

// A room's name, trimmed of its trailing space padding, for chunk summaries
// that name a target room (a TOGGLE_BIT switch). buf must be at least 25 bytes.
static const char *room_name_trimmed(uint16_t room_idx, char *buf, size_t buf_size) {
    memset(buf, 0, buf_size);
    strncpy(buf, state->rooms.rooms[room_idx].data.name, buf_size - 1);
    for (int j = (int)buf_size - 1; j >= 0; j--)
        if (buf[j] == '\0' || isspace((unsigned char)buf[j])) buf[j] = '\0';
        else break;
    return buf;
}

static bool chunk_at_cursor(struct SwitchChunk *chunk, int cx, int cy) {
    if (chunk->type != TOGGLE_BLOCK) return false;
    return chunk->dir == HORIZONTAL
        ? cy == chunk->y && cx >= chunk->x && cx < chunk->x + chunk->size
        : cx == chunk->x && cy >= chunk->y && cy < chunk->y + chunk->size;
}

static void draw_switch_chunks_brief(struct SwitchObject *sw, int cx, int cy, int *bottom) {
    if (sw->chunks.length <= 1) return;
    /* A switch can carry up to 16 chunks, and several switches can share the
     * cursor's tile; a packed room can list more lines here than a real
     * terminal (or even the GUI's virtual screen) is tall. Stop and say so
     * rather than let the GOTO below assert. */
    if (*bottom >= state->screen_dimensions.y - 1) return;
    GOTO(0, *bottom); (*bottom)++;
    scr_printf("  chunks:");
    for (size_t i = 1; i < sw->chunks.length; i++) {
        if (*bottom >= state->screen_dimensions.y - 1) {
            GOTO(0, *bottom); (*bottom)++;
            scr_printf("  ... %zu more chunk%s not shown", sw->chunks.length - i, sw->chunks.length - i == 1 ? "" : "s");
            return;
        }
        GOTO(0, *bottom); (*bottom)++;
        struct SwitchChunk *chunk = sw->chunks.data + i;
        bool at_cur = chunk_at_cursor(chunk, cx, cy);
        scr_printf("    (%zu) type ", i);
        switch (chunk->type) {
            case TOGGLE_BLOCK:
                scr_printf("block: (x,y)=%d,%d (%s)=%d (on/off)=%02X/%02X%s",
                    chunk->x, chunk->y,
                    chunk->dir == VERTICAL ? "height" : "width", chunk->size,
                    chunk->on, chunk->off,
                    at_cur ? " *" : "");
                break;
            case TOGGLE_BIT: {
                char name[25];
                scr_printf("toggle: (room) %s (switch) %d (on/off)=%d/%d",
                    room_name_trimmed(chunk->room_idx, name, sizeof(name)), chunk->switch_idx, chunk->on, chunk->off);
                break;
            }
            case TOGGLE_OBJECT:
                scr_printf("object: idx=%d on=%d", chunk->index, chunk->on);
                break;
            case PREAMBLE: break;
            default: break;
        }
    }
}

// One "word: value" attribute field, matching the rest of this panel's hotkey convention (see
// e.g. "bkgrnd:" in EDIT_ROOMDETAILS above): `hot` underlines the word's leading letter (always
// its hotkey) or not, but the text is otherwise identical either way - this is what lets
// draw_movertab_entry show the exact same wording whether the object is just being looked at or
// is the open EDIT_MOVERTAB_OBJECT.
static void movertab_field(bool hot, const char *word, const char *value) {
    if (hot) scr_printf("\033[1;4m%c\033[m%s: %s", word[0], word + 1, value);
    else scr_printf("%s: %s", word, value);
}

// A label word with its hotkey letter embedded (see movertab_field) but no ": value" of its
// own - for a block's direction/speed/walls fields, where one label governs a "(|): .. (-): .."
// pair rather than a single value (see movertab_axis_field just below).
static void movertab_word(bool hot, const char *word) {
    if (hot) scr_printf("\033[1;4m%c\033[m%s", word[0], word + 1);
    else scr_printf("%s", word);
}
// "(|): value" / "(-): value" - the vertical/horizontal half of a block's direction/speed/walls
// field, matching EDIT_ROOMDETAILS's "gravity (|)"/"gravity (-)" convention exactly. `focus_hot`
// (not just `hot`) underlines the symbol itself: only while that field's key (s/d/w) is the one
// currently armed (state->movertab_block_focus), since | and - do something different depending
// on which of those is active (see handle_edit_movertab_object's '|'/'-' case).
static void movertab_axis_field(bool focus_hot, char sym, const char *value) {
    if (focus_hot) scr_printf("(\033[1;4m%c\033[m): %s", sym, value);
    else scr_printf("(%c): %s", sym, value);
}

// The decoded attribute fields for a behaviour byte - a MOVERTAB entry's byte 2, or a
// TOGGLE_OBJECT chunk's value byte (same scheme either way, see room.c's doc comment above
// move_desc() and editor_input.c's chunk_target_is_sprite) - so both editors show and edit it
// identically (Hugh: "consistent language and editing"). `focus` is whichever of the two
// editors' own focus state applies (state->movertab_block_focus or ->chunk_value_focus) -
// passed in rather than read directly so this one function serves both.
// flip_editable: false for a TOGGLE_OBJECT chunk targeting a sprite - a switch command can't
// flip a sprite in the real game, only MOVERTAB.DAT at room load can (Hugh: "remove the flip
// option from just the sprite object chunk of a switch... leave it in place for the actual
// sprite edit menu"), so its hotkey underline and the glitchy-value fix hint don't mention 'f'
// there, even though the same bits are still shown.
static void draw_movertab_fields(uint8_t value, bool is_sprite, bool hot, char focus, bool flip_editable) {
    if (!is_sprite) {
        // Two independent axes (see the "heli" table comment above handle_edit_movertab_object,
        // editor_input.c): speed/walls each split into a vertical (|) and horizontal (-) half,
        // matching EDIT_ROOMDETAILS's gravity fields. No separate direction field - speed is
        // signed instead, -3..3 (Hugh: "drop the direction display, and instead do speed:
        // -3..3, and -ve means v and <"). Walls only matters for an axis actually moving.
        int vspeed_mag = (value >> 4) & 0x03, hspeed_mag = value & 0x03;
        int vspeed = (value & 0x40) ? vspeed_mag : -vspeed_mag;    // + up, - down
        int hspeed = (value & 0x04) ? -hspeed_mag : hspeed_mag;    // + right, - left
        char vspeedbuf[4], hspeedbuf[4];
        snprintf(vspeedbuf, sizeof vspeedbuf, "%d", vspeed);
        snprintf(hspeedbuf, sizeof hspeedbuf, "%d", hspeed);
        bool speed_focus = hot && (focus == 's' || focus == 'd');
        bool wall_focus = hot && focus == 'w';

        movertab_word(hot, "speed");
        scr_printf(" "); movertab_axis_field(speed_focus, '|', vspeedbuf);
        scr_printf(", "); movertab_axis_field(speed_focus, '-', hspeedbuf);
        if (vspeed_mag || hspeed_mag) {
            scr_printf(", ");
            movertab_word(hot, "walls");
            scr_printf(" ");
            bool first = true;
            if (vspeed_mag) { movertab_axis_field(wall_focus, '|', (value & 0x80) ? "back" : "stop"); first = false; }
            if (hspeed_mag) { if (!first) scr_printf(", "); movertab_axis_field(wall_focus, '-', (value & 0x08) ? "back" : "stop"); }
        }
        return;
    }
    if (value == 0x00) {
        movertab_field(hot, "dead", "yes");
        return;
    }
    if ((value & 0x81) != 0x80) {
        // Raw mode (or an older editor version) can leave a value this picker would never
        // produce itself (bit 0x01 set, or 0x80 clear on a nonzero byte) - say so plainly
        // rather than decode it as if it were a clean attribute combination: pressing d/(f/)m
        // normalises it back to one the moment you use them (see handle_edit_movertab_object).
        if (hot && flip_editable) scr_printf("GLITCHY - \033[1;4md\033[m/\033[1;4mf\033[m/\033[1;4mm\033[m fixes it");
        else if (hot) scr_printf("GLITCHY - \033[1;4md\033[m/\033[1;4mm\033[m fixes it");
        else if (flip_editable) scr_printf("GLITCHY - d/f/m fixes it");
        else scr_printf("GLITCHY - d/m fixes it");
        return;
    }
    static const char *FLIP[4] = {"none", "<", "^", "both"};   // < horizontal (facing), ^ vertical (upside-down) - same compact-symbol style as "gravity (|)"/"gravity (-)"
    // chase-first: 0x40 and 0x08 can coexist (see the 0x08/0x40 comment in editor_input.c) -
    // within chase, 0x08 is its own "currently moving" sub-flag (the "stopped" field), not patrol.
    int mode = (value & 0x40) ? 2 : (value & 0x08) ? 1 : 0;
    movertab_field(hot, "dead", "no");
    if (flip_editable) {
        // Not shown at all for a TOGGLE_OBJECT chunk - flip isn't meaningful to a switch
        // command, only MOVERTAB.DAT at room load (Hugh: "dont show flip: none, in chunk view
        // or edit... it isn't meaningful to switches").
        scr_printf(", ");
        movertab_field(hot, "flip", FLIP[(value & 0x06) >> 1]);
    }
    scr_printf(", ");
    movertab_field(hot, "mode", mode == 0 ? "stopped" : mode == 1 ? "patrol" : "chase");
    if (mode == 2 && !(value & 0x08)) {   // chasing already implies "not stopped" by default - only worth saying when it IS stopped
        scr_printf(", ");
        movertab_field(hot, "stopped", "yes");
    }
    if (mode != 0) {
        scr_printf(", ");
        movertab_field(hot, "edge", (value & 0x20) ? "turn" : "fall");
        scr_printf(", ");
        movertab_field(hot, "walls", (value & 0x10) ? "turn" : "stop");
    }
}

// The x/y tile offset (MOVERTAB bytes 1/0) on the object's own sprite/block summary line, not
// the movertab attribute line below it (Hugh: "put the offsets of x/y on the line above"; "show
// them even if 0"). Edited with hjkl/arrows while EDIT_MOVERTAB_OBJECT has this object open (see
// movertab_offset_adjust, editor_input.c) - `hot` (that condition) underlines the hjkl hint.
static void draw_movertab_offset(unsigned global, bool is_sprite, bool hot) {
    if (global >= C_ARRAY_LEN(state->movertab) / 3) return;
    const uint8_t *entry = state->movertab + global * 3;
    int dx = movertab_offset_x(entry), dy = movertab_offset_y(entry, is_sprite);
    scr_printf(" (offset x,y)=%+d,%+d", dx, dy);
    if (hot) scr_printf(" (\033[1;4marrows\033[m/\033[1;4mhjkl\033[m)");
}

// One object's MOVERTAB entry, in the bottom panel next to its sprite/block line (that line
// carries the x/y tile offset - bytes 0/1 - itself; see the sprite/block listing above). Read-
// only and the active EDIT_MOVERTAB_OBJECT view show identical wording (draw_movertab_fields) -
// the only difference is EDIT_MOVERTAB_OBJECT's hotkey letters being underlined, and raw hex
// entry (state->movertab_raw) replacing just the "raw: XX" tail (byte 2 only) with the
// byte-being-typed display, decoded fields still shown above it. Kept out of the title row: that
// row also carries state->status_message, which "Saved MOVERTAB.DAT" overwrites after every
// edit, making a live view there useless.
static void draw_movertab_entry(unsigned global, bool active_edit, int *bottom, bool is_sprite) {
    if (global >= C_ARRAY_LEN(state->movertab) / 3) return;
    const uint8_t *entry = state->movertab + global * 3;
    GOTO(0, *bottom); (*bottom)++;
    scr_printf("  ");
    bool raw_editing = active_edit && state->movertab_raw;
    draw_movertab_fields(entry[2], is_sprite, active_edit && !raw_editing, state->movertab_block_focus, true);
    scr_printf(", ");
    if (!raw_editing) {
        char raw[8];
        snprintf(raw, sizeof raw, "%02x", entry[2]);
        movertab_field(active_edit, "raw", raw);
    } else {
        char hi = "0123456789abcdef"[entry[2] >> 4];
        char lo = "0123456789abcdef"[entry[2] & 0xf];
        scr_printf("\033[1;4mr\033[maw: ");
        if (state->partial_byte) {
            scr_printf("%x", state->partial_byte & 0xFF);
            scr_printf("\033[4m%c\033[m", lo);
        } else {
            scr_printf("\033[4m%c%c\033[m", hi, lo);
        }
    }
    if (active_edit && !state->movertab_loaded) scr_printf(" (no MOVERTAB.DAT found)");
}

/* Where redraw() put the out-of-bounds switch list (GUI pins it and makes the
 * entries clickable): text rows [oob_row0, oob_row1), and for each listed
 * switch the row of its header line. */
int panel_oob_row0 = -1, panel_oob_row1 = -1;
int panel_oob_switch_row[16];
int panel_oob_switch_id[16];
size_t panel_oob_switch_n = 0;

void redraw() {
#define PRINTF_DATA(num) scr_printf(state->debug.hex ? "%02X" : "%d", (num))
    // A long-running hot-reloaded session never re-runs the startup init that first calls
    // movertab_load() (hot-reload only swaps in new code, not the one-time setup that ran
    // before this feature existed), so try once here too - the first redraw of any session,
    // old or new, is guaranteed to hit this.
    if (!state->movertab_load_attempted) movertab_load();
    panel_oob_row0 = panel_oob_row1 = -1;
    panel_oob_switch_n = 0;

    /* Guard before any GOTO: terminal must meet the absolute floor.
     * The existing check further down handles the debug-panel overage case. */
    if (state->screen_dimensions.x < MIN_WIDTH || state->screen_dimensions.y < MIN_HEIGHT) {
        scr_printf("\033[1;1HTerminal too small (need %dx%d, have %dx%d)",
                   MIN_WIDTH, MIN_HEIGHT,
                   state->screen_dimensions.x, state->screen_dimensions.y);
        screen_flush();
        return;
    }

    int offset_y = 0;
    int offset_x = 0;
    if (debugany()) offset_y ++;
    if (state->debug.data) offset_y ++;
    if (state->debug.neighbours) offset_y += 4;
    if (state->debug.objects) offset_y ++;

    size_t level = state->current_level;
    struct DecompressedRoom room = state->rooms.rooms[level].data;
    int hot_switch = -1, hot_chunk = -1; bool hot_on = false;
    editor_hot_block_preview(&hot_switch, &hot_chunk, &hot_on);
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
            break;
        } else if (object->type == SPRITE && x == object->x && y == object->y) {
            obj = true;
            sprite = true;
            tile = (object->sprite.type << 4) | object->sprite.damage;
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
                            tile = chunk->off;
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
                        tile = chunk->off;
                    }
                    ch = true;
                    chunk_switch_underneath = switcch;
                    chunk_underneath = chunk;
                    break;
                }
            }
        }
        if (ch) break;
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
        if (x >= state->screen_dimensions.x) x = state->screen_dimensions.x - 1;
        if (y < 0) y = 0;
        if (y + 1 >= state->screen_dimensions.y) y = state->screen_dimensions.y - 2;
        GOTO(x, y);
        scr_printf("%s", message);
        free(message);
        screen_flush();
        return;
    }

    GOTO(6, 0);
    scr_printf("%s", state->debug.hex ? "[HEX]" : "[DEC]");
    switch (state->clipboard.type) {
        case CLIP_OBJECT: scr_printf(" \033[1m[Y:obj]\033[m"); break;
        case CLIP_SWITCH: scr_printf(" \033[1m[Y:sw]\033[m"); break;
        case CLIP_CHUNK:  scr_printf(" \033[1m[Y:chk]\033[m"); break;
        case CLIP_TILE:   scr_printf(" \033[1m[Y:tile]\033[m"); break;
        case CLIP_ROOM:   scr_printf(" \033[1m[Y:room]\033[m"); break;
        case CLIP_NONE: break;
    }
    if (state->macro_recording) {
        scr_printf(" \033[1;31m[M:%c]\033[m", state->macro_slot);
    }

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
        scr_printf("\nEnter room number or q to go to main view\n");
        if (state->current_state == GOTO_ROOM) {
            if (state->room_delete_mode) {
                scr_printf("\033[1;31mCtrl-X: DELETE MODE - next room picked is blanked, not entered\033[m\n");
            } else {
                scr_printf("Ctrl-X: delete mode (blank a room instead of entering it)\n");
            }
        } else {
            scr_printf("\n");
        }
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
    } else if (state->current_state == GOTO_COORD) {
        scr_printf("%s> x:", state->goto_coord_move ? "G" : "g");
        if (state->goto_coord_x >= 0) {
            if (state->debug.hex) scr_printf("%02x", state->goto_coord_x);
            else scr_printf("%02d", state->goto_coord_x);
        } else if (state->partial_byte) {
            if (state->debug.hex) scr_printf("%x", state->partial_byte & 0xFF);
            else scr_printf("%u", state->partial_byte & 0xFF);
            scr_printf("\033[4;5m_\033[m");
        } else {
            scr_printf("\033[4;5m__\033[m");
        }
        scr_printf(",y:");
        if (state->goto_coord_x >= 0 && state->partial_byte) {
            if (state->debug.hex) scr_printf("%x", state->partial_byte & 0xFF);
            else scr_printf("%u", state->partial_byte & 0xFF);
            scr_printf("\033[4;5m_\033[m");
        } else {
            scr_printf("\033[4;5m__\033[m");
        }
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
    if (state->status_message[0]) {
        int title_right = MIN_WIDTH / 2 + (room_name_len + 7 + 1) / 2 + 1;
        if (title_right + 1 < state->screen_dimensions.x) {
            GOTO(title_right, 0);
            scr_printf("\033[1m%s\033[m", state->status_message);
        }
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
    } else if (state->current_state == GOTO_OBJECT) {
        for (int y = 0; y < HEIGHT_TILES; y ++) {
            for (int x = 0; x < WIDTH_TILES; x ++) {
                uint8_t tile = room.tiles[TILE_IDX(x, y)];
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
                    scr_printf(" %x", o);
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
                if (state->debug.switches)
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
                                    if (s == hot_switch && (hot_chunk == -1 || (int)c == hot_chunk)) {
                                        tile = hot_on ? chunk->on : chunk->off;
                                    } else {
                                        tile = chunk->off;
                                    }
                                }
                            } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                                scr_printf("\033[3%d;40m", (s % 3) + 4);
                                colored = true;
                                if (s == hot_switch && (hot_chunk == -1 || (int)c == hot_chunk)) {
                                    tile = hot_on ? chunk->on : chunk->off;
                                } else {
                                    tile = chunk->off;
                                }
                            }
                        }
                    }
                }
                bool attr_colored = false;
                // Solid gets no overlay - same as the GUI's room tint (Hugh: "make solid be
                // no overlay, just the tile as it is defined").
                if (!colored && state->debug.tiles && tile != BLANK_TILE && (tile >> 6) != 1) {
                    // Same blue/violet/cyan legend as Ctrl-D t on the room picture
                    // (pass/magnet/slip), in the 8-colour ANSI palette the hex map itself is
                    // drawn in.
                    static const char *attr_ansi[4] = { "\033[44;37m", "", "\033[45;37m", "\033[46;30m" };
                    scr_printf("%s", attr_ansi[tile >> 6]);
                    attr_colored = true;
                }
                if (colored || attr_colored || tile != BLANK_TILE) {
                    GOTO(2 * x, y + 1);
                    scr_printf("%02X", tile);
                    if (colored || attr_colored) scr_printf("\033[m");
                }
            }
        }
    }

    uint8_t dirty[MIN_HEIGHT * MIN_WIDTH] = {0};
    if (state->debug.objects && state->current_state != GOTO_OBJECT) {
        /* Pass 1: blocks */
        for (size_t i = 0; i < room.num_objects; i++) {
            struct RoomObject *object = room.objects + i;
            if (object->type != BLOCK) continue;
            if (state->current_state == GOTO_SWITCH) continue;
            assert(object->x >= 0 && object->y >= 0);
            assert(object->x < WIDTH_TILES && object->y < HEIGHT_TILES);
            assert(object->x + object->block.width <= WIDTH_TILES);
            assert(object->y + object->block.height <= HEIGHT_TILES);
            size_t tile_n = object->block.width * object->block.height;
            bool all_blank = true;
            for (size_t t = 0; t < tile_n; t++)
                if (object->tiles[t] != BLANK_TILE) { all_blank = false; break; }
            scr_printf("\033[3%ldm", (i % 3) + 1);
            for (int oy = object->y; oy < object->y + (int)object->block.height; oy++) {
                GOTO(2 * object->x, oy + 1);
                for (int ox = object->x; ox < object->x + (int)object->block.width; ox++) {
                    if (dirty[oy * MIN_WIDTH + ox]) continue;
                    dirty[oy * MIN_WIDTH + ox] = 1;
                    uint8_t tile = object->tiles[(oy - object->y) * object->block.width + (ox - object->x)];
                    scr_printf(tile == BLANK_TILE && !all_blank ? "  " : "%02X", tile);
                }
            }
            scr_printf("\033[m");
        }
        /* Pass 2: sprites — always on top of blocks */
        for (size_t i = 0; i < room.num_objects; i++) {
            struct RoomObject *object = room.objects + i;
            if (object->type != SPRITE) continue;
            if (state->current_state == GOTO_SWITCH) continue;
            assert(object->x >= 0 && object->y >= 0);
            assert(object->x < WIDTH_TILES && object->y < HEIGHT_TILES);
            GOTO(2 * object->x, object->y + 1);
            scr_printf("\033[4%ld;30m%d%d\033[m", (i % 3) + 1, object->sprite.type, object->sprite.damage);
        }
    }

    // Whenever the cursor sits on an object's own definition tile (not only while its MOVERTAB
    // entry is actively open - Hugh: "show the shadows when you are over the object def, not
    // just when you are in edit mode... but when you are not over the object [even over where it
    // really is], then don't show shadow"), mark the tile(s) it's actually drawn at (x/y offset
    // applied) the same way its definition is marked above (type+damage digits, or the raw tile
    // hex bytes) but underlined, so the two are visually distinct - "similar to other" markers
    // on this map, since the plain TUI has no picture to show this any other way. This is drawn
    // into the same cell buffer the GUI's own hex-map view (F2) shows as text too. Standing on
    // the offset (drawn) location itself shows the same shadow in the same place, rather than a
    // marker back at the definition (Hugh: "in tui, when you are over the actual offset of a
    // block or sprite, show the shadow"; "show it where it would be shown if you had that
    // object selected, not the definition place") - "over the definition" and "over the offset"
    // both point at the one shadow at the offset, they just never both apply at once once the
    // two differ.
    if (state->debug.objects && state->current_state != GOTO_OBJECT) {
        for (size_t i = 0; i < room.num_objects; i++) {
            struct RoomObject *object = room.objects + i;
            unsigned global = (unsigned)room.UNKNOWN_c + (unsigned)i;
            if (object->type == SPRITE && global < C_ARRAY_LEN(state->movertab) / 3) {
                const uint8_t *entry = state->movertab + global * 3;
                int ox = object->x + movertab_offset_x(entry);
                int oy = object->y + movertab_offset_y(entry, true);
                bool offset_visible = (ox != object->x || oy != object->y) &&
                        ox >= 0 && ox < WIDTH_TILES && oy >= 0 && oy < HEIGHT_TILES;
                bool at_def = x == object->x && y == object->y;
                // Standing on the offset itself shows the same shadow already shown from the
                // definition, in the same place (Hugh: "show it where it would be shown if you
                // had that object selected, not the definition place") - not a marker back at
                // the definition, which would be a second, different location to make sense of.
                bool at_off = offset_visible && x == ox && y == oy;
                if (offset_visible && (at_def || at_off)) {
                    GOTO(2 * ox, oy + 1);
                    scr_printf("\033[4;33;40m%d%d\033[m", object->sprite.type, object->sprite.damage);
                }
            } else if (object->type == BLOCK && object->tiles) {
                int ox, oy;
                render_block_offset(object, state->movertab, global, &ox, &oy);
                bool offset_visible = ox != object->x || oy != object->y;
                bool in_def = x >= object->x && x < object->x + (int)object->block.width &&
                              y >= object->y && y < object->y + (int)object->block.height;
                bool in_off = offset_visible && x >= ox && x < ox + (int)object->block.width &&
                              y >= oy && y < oy + (int)object->block.height;
                if (offset_visible && (in_def || in_off)) {
                    for (int by = 0; by < (int)object->block.height; by++) {
                        int ty = oy + by;
                        if (ty < 0 || ty >= HEIGHT_TILES) continue;
                        for (int bx = 0; bx < (int)object->block.width; bx++) {
                            int tx = ox + bx;
                            if (tx < 0 || tx >= WIDTH_TILES) continue;
                            GOTO(2 * tx, ty + 1);
                            scr_printf("\033[4;33;40m%02X\033[m", object->tiles[by * (int)object->block.width + bx]);
                        }
                    }
                }
            }
        }
    }

    /* Phase 3: Re-overlay TOGGLE_BLOCK chunks, then preambles on top */
    if (state->debug.switches && state->current_state != GOTO_OBJECT
            && state->current_state != GOTO_SWITCH) {
        /* 3a: chunk tiles */
        for (size_t s = 0; s < (size_t)room.num_switches; s++) {
            struct SwitchObject *sw = room.switches + s;
            for (size_t c = 1; c < sw->chunks.length; c++) {
                struct SwitchChunk *chunk = sw->chunks.data + c;
                if (chunk->type != TOGGLE_BLOCK) continue;
                if (chunk->y >= HEIGHT_TILES) continue;   // a "memory" chunk, off-map by design
                uint8_t ctile = ((int)s == hot_switch && (hot_chunk == -1 || (int)c == hot_chunk))
                    ? (hot_on ? chunk->on : chunk->off)
                    : chunk->off;
                scr_printf("\033[3%d;40m", (int)(s % 3) + 4);
                if (chunk->dir == HORIZONTAL) {
                    for (int cx = chunk->x; cx < chunk->x + (int)chunk->size; cx++) {
                        GOTO(2 * cx, chunk->y + 1);
                        scr_printf("%02X", ctile);
                    }
                } else {
                    for (int cy = chunk->y; cy < chunk->y + (int)chunk->size; cy++) {
                        GOTO(2 * chunk->x, cy + 1);
                        scr_printf("%02X", ctile);
                    }
                }
                scr_printf("\033[m");
            }
        }
        /* 3b: preambles on top of chunks */
        for (size_t s = 0; s < (size_t)room.num_switches; s++) {
            struct SwitchObject *sw = room.switches + s;
            if (sw->chunks.length == 0) continue;
            int sx = sw->chunks.data[0].x;
            int sy = sw->chunks.data[0].y;
            // An out-of-bounds switch (shown in its own "Out of bounds switches" panel
            // listing instead) has no tile on the map to mark - GOTOing to 2*sx, sy+1 for one
            // spilled into the panel text below the map (Hugh: "there is a line with just a
            // blue 00 on it just above out of bound switches").
            if (sx >= WIDTH_TILES || sy >= HEIGHT_TILES) continue;
            uint8_t tile = room.tiles[TILE_IDX(sx, sy)];
            GOTO(2 * sx, sy + 1);
            scr_printf("\033[4%d;30m%02X\033[m", (int)(s % 3) + 4, tile);
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
            if (state->current_state == GOTO_SWITCH || state->current_state == GOTO_OBJECT) {
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
            case EDIT_SWITCHDETAILS_XY:
            case EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS:
            case EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS:
            case EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS:
            case EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS:
            case EDIT_MOVERTAB_OBJECT:
                break;

            case NORMAL:
            case TILE_EDIT:
            case GOTO_OBJECT:
            case GOTO_SWITCH:
            case GOTO_COORD:
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
            // A compass layout for the plain (non-editing) view: up/down centred
            // over the map width, left and right sharing one line at each edge
            // (Hugh: more like a compass than a stacked list). Room-edit mode
            // (above) keeps its original stacked caps LEFT/DOWN/UP/RIGHT layout.
            const char *idfmt = state->debug.hex ? "%02X" : "%d";
            char idbuf[8], up_buf[64], down_buf[64], left_buf[64], right_buf[64];

            READ_NEIGHBOUR(room.room_north);
            snprintf(idbuf, sizeof idbuf, idfmt, room.room_north);
            snprintf(up_buf, sizeof up_buf, "up: %s - \"%s\"", idbuf, neighbour_name);

            READ_NEIGHBOUR(room.room_south);
            snprintf(idbuf, sizeof idbuf, idfmt, room.room_south);
            snprintf(down_buf, sizeof down_buf, "down: %s - \"%s\"", idbuf, neighbour_name);

            READ_NEIGHBOUR(room.room_west);
            snprintf(idbuf, sizeof idbuf, idfmt, room.room_west);
            snprintf(left_buf, sizeof left_buf, "left: %s - \"%s\"", idbuf, neighbour_name);

            READ_NEIGHBOUR(room.room_east);
            snprintf(idbuf, sizeof idbuf, idfmt, room.room_east);
            snprintf(right_buf, sizeof right_buf, "right: %s - \"%s\"", idbuf, neighbour_name);

            int up_len = (int)strlen(up_buf), down_len = (int)strlen(down_buf), right_len = (int)strlen(right_buf);
            GOTO(up_len < MIN_WIDTH ? (MIN_WIDTH - up_len) / 2 : 0, bottom); bottom ++;
            scr_printf("%s", up_buf);
            GOTO(0, bottom);
            scr_printf("%s", left_buf);
            GOTO(right_len < MIN_WIDTH ? MIN_WIDTH - right_len : 0, bottom); bottom ++;
            scr_printf("%s", right_buf);
            GOTO(down_len < MIN_WIDTH ? (MIN_WIDTH - down_len) / 2 : 0, bottom); bottom ++;
            scr_printf("%s", down_buf);
            bottom ++;   // a blank line before whatever comes next (Hugh: "add a blank line
                         // between the room neighbours compass and the info below")
        }
    }

    // Sprite/block hover info right after the neighbours compass - previously this sat after
    // the switches section (including its out-of-bounds listing, which can run long), so it
    // could end up many lines below the compass instead of just after that one blank line
    // (Hugh: "blocks, sprites show up below the out of bounds switches. they should be
    // above, just with one blank line below the down room").
    if (state->debug.objects || state->current_state == EDIT_MOVERTAB_OBJECT) {
        bool obj_selected = switch_underneath == NULL && chunk_switch_underneath == NULL;
        bool first_hit = true;
        /* Pass 1: sprites first (on top) */
        for (size_t i = 0; i < room.num_objects; i++) {
            struct RoomObject *obj = room.objects + i;
            if (obj->type != SPRITE) continue;
            // Its logical (x,y) - the definition - always counts as "on" a sprite, regardless of
            // how far an x/y offset has since moved it away from there (Hugh: "when i go to
            // where it is defined in the level... it is as if i were on an empty tile"). The
            // plain offset tile (movertab_offset_x/y applied) counts too, in the TUI as well as
            // the GUI - it's just a tile coordinate, no picture needed to know where it is
            // (Hugh: "it doesn't show up when the spidey cursor... i go to where a sprite would
            // be drawn, and no info shows up in tui"), matching the shadow marker above. Only
            // the *wider* GUI rendered box (SPRITE_X_PAD widens and shifts it even with no
            // offset at all, see render.c) stays GUI-only, since that's a picture-pixel detail
            // with nothing to correspond to in the TUI. The Enter-key hit-test in handle_tile_edit
            // (editor_input.c) uses this same set of checks.
            bool hit = (int)x == obj->x && (int)y == obj->y;
            unsigned obj_global = (unsigned)room.UNKNOWN_c + (unsigned)i;
            if (!hit && obj_global < C_ARRAY_LEN(state->movertab) / 3) {
                const uint8_t *oe = state->movertab + obj_global * 3;
                int ox = obj->x + movertab_offset_x(oe);
                int oy = obj->y + movertab_offset_y(oe, true);
                hit = (int)x == ox && (int)y == oy;
            }
            if (!hit && screen_headless) {
                int dx, dy, w, h; bool fh, fv;
                render_sprite_box(obj, state->movertab, (unsigned)room.UNKNOWN_c + (unsigned)i, &dx, &dy, &w, &h, &fh, &fv);
                int px = (int)x * 8, py = (int)y * 8;
                hit = px < dx + w && px + 8 > dx && py < dy + h && py + 8 > dy;
            }
            if (!hit) continue;
            // The box/offset checks above can now match several sprites/blocks around the
            // cursor at once (unlike the old exact-tile-only test, which could match at most
            // one) - stop adding rows once the screen is nearly full rather than assert past its
            // bottom (a real crash Hugh hit: "editor_draw.c:1859: redraw: Assertion `(bottom) <
            // state->screen_dimensions.y ... failed"). Two rows of margin: the summary line
            // below plus draw_movertab_entry's own line.
            if (bottom >= state->screen_dimensions.y - 2) continue;
            bool is_active = first_hit && obj_selected;
            first_hit = false;
            GOTO(0, bottom); bottom++;
            scr_printf("sprite ");
            if (state->current_state == TILE_EDIT && is_active) scr_printf("\033[4;1m");
            PRINTF_DATA((uint16_t)i);
            if (state->current_state == TILE_EDIT && is_active) scr_printf("\033[m");
            scr_printf(": ");
            switch(obj->sprite.type) {
                case SHARK:
                    switch (obj->sprite.damage) {
                        case 1:
                        case 2: scr_printf("Shark"); break;
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
            PRINTF_DATA(obj->x);
            scr_printf(",");
            PRINTF_DATA(obj->y);
            // "(dmg)=" isn't shown at all for Mysterio/Mary Jane - it's not a stat you tweak
            // for them, that identity only changes via Ctrl-O's sprite cycle (Hugh: "don't show
            // dmg= for myst or mary, do for others"). Otherwise, highlight the 'g' in "(dmg)="
            // itself (a mnemonic, not the value) while its key is live.
            bool is_myst_or_mj = obj->sprite.type == SHARK && obj->sprite.damage >= 3;
            if (!is_myst_or_mj) {
                bool dmg_hot = state->current_state == EDIT_MOVERTAB_OBJECT && state->current_object == i;
                scr_printf(" (dm");
                if (dmg_hot) scr_printf("\033[4;1m");
                scr_printf("g");
                if (dmg_hot) scr_printf("\033[m");
                scr_printf(")=");
                PRINTF_DATA(obj->sprite.damage);
            }
            draw_movertab_offset((unsigned)room.UNKNOWN_c + (unsigned)i, true,
                                  state->current_state == EDIT_MOVERTAB_OBJECT && state->current_object == i);
            draw_movertab_entry((unsigned)room.UNKNOWN_c + (unsigned)i,
                                 state->current_state == EDIT_MOVERTAB_OBJECT && state->current_object == i,
                                 &bottom, true);
        }
        /* Pass 2: blocks underneath */
        for (size_t i = 0; i < room.num_objects; i++) {
            struct RoomObject *obj = room.objects + i;
            if (obj->type != BLOCK) continue;
            // Its definition rectangle always counts as "on" a block, same reasoning as a
            // sprite's logical (x,y) - plus, unlike a sprite's rendered box (only meaningful
            // where there's an actual picture), a block's x/y-offset rectangle is a plain,
            // well-defined set of tiles regardless of TUI or GUI, so it applies in both (Hugh:
            // "i would like to be able to edit it when spidey is over its drawn location, and
            // also its definition byte... i want both").
            bool in_def = x >= obj->x && x < obj->x + (int)obj->block.width &&
                          y >= obj->y && y < obj->y + (int)obj->block.height;
            bool in_off = false;
            if (!in_def) {
                int ox, oy;
                render_block_offset(obj, state->movertab, (unsigned)room.UNKNOWN_c + (unsigned)i, &ox, &oy);
                in_off = x >= ox && x < ox + (int)obj->block.width &&
                         y >= oy && y < oy + (int)obj->block.height;
            }
            if (!in_def && !in_off) continue;
            if (bottom >= state->screen_dimensions.y - 2) continue;   // see the same guard in the sprite pass above
            bool is_active = first_hit && obj_selected;
            first_hit = false;
            GOTO(0, bottom); bottom++;
            scr_printf("block ");
            if (state->current_state == TILE_EDIT && is_active) scr_printf("\033[4;1m");
            PRINTF_DATA((uint16_t)i);
            if (state->current_state == TILE_EDIT && is_active) scr_printf("\033[m");
            scr_printf(": (x,y)=");
            PRINTF_DATA(obj->x);
            scr_printf(",");
            PRINTF_DATA(obj->y);
            scr_printf(" (w,h)=");
            PRINTF_DATA(obj->block.width);
            scr_printf(",");
            PRINTF_DATA(obj->block.height);
            draw_movertab_offset((unsigned)room.UNKNOWN_c + (unsigned)i, false,
                                  state->current_state == EDIT_MOVERTAB_OBJECT && state->current_object == i);
            draw_movertab_entry((unsigned)room.UNKNOWN_c + (unsigned)i,
                                 state->current_state == EDIT_MOVERTAB_OBJECT && state->current_object == i,
                                 &bottom, false);
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

    if (state->debug.switches) {

        GOTO(0, bottom);
    (void)chunk_switch_underneath;
    (void)chunk_underneath;
        struct SwitchObject *switcz = switch_underneath;
        if (!switcz && chunk_underneath) switcz = chunk_switch_underneath;
        if (state->current_state != GOTO_SWITCH && switcz != NULL) {
#define BOOL_S(b) ((b) ? "true" : "false")
            assert(switcz->chunks.length >= 1);
            struct SwitchChunk *preamble = switcz->chunks.data;
            // "side" is meaningless for a switch with no backing tile at all (out of bounds or
            // blank); "entry" is separately forced/hidden only for the in-bounds-blank case -
            // an out-of-bounds switch's entry keeps its ordinary meaning (see switch_entry_forced).
            bool side_hidden = switch_side_hidden(&room, preamble);
            bool entry_forced = switch_entry_forced(&room, preamble);
            if (state->current_state == EDIT_SWITCHDETAILS) {
                scr_printf("switch ");
                if (switcz == switch_underneath) scr_printf("\033[4;1m");
                PRINTF_DATA((uint16_t)(switcz - room.switches));
                if (switcz == switch_underneath) scr_printf("\033[m");
                scr_printf(": (x,y)=%d,%d", preamble->x, preamble->y);
                if (!entry_forced) {
                    scr_printf(" (\033[1;4me\033[mn\033[mtry)=%s", BOOL_S(preamble->room_entry));
                }
                scr_printf(" (\033[1;4mo\033[mnce)=%s", BOOL_S(preamble->one_time_use));
                if (!side_hidden) {
                    scr_printf(" (\033[1;4ms\033[mide)=%s", SWITCH_SIDE(preamble->side));
                }
                scr_printf("\n");
            } else {
                scr_printf("switch ");
                if (state->current_state == TILE_EDIT && switcz == switch_underneath) scr_printf("\033[4;1m");
                PRINTF_DATA((uint16_t)(switcz - room.switches));
                if (state->current_state == TILE_EDIT && switcz == switch_underneath) scr_printf("\033[m");
                scr_printf(": (x,y)=%d,%d", preamble->x, preamble->y);
                if (!entry_forced) {
                    scr_printf(" (entry)=%s", BOOL_S(preamble->room_entry));
                }
                scr_printf(" (once)=%s", BOOL_S(preamble->one_time_use));
                if (!side_hidden) {
                    scr_printf(" (side)=%s", SWITCH_SIDE(preamble->side));
                }
                scr_printf("\n");
            }
            if (switcz->chunks.length > 1) {
                bottom +=2;
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
                            char target_name[25];
                            room_name_trimmed(chunk->room_idx, target_name, sizeof(target_name));
                            static const char *TOGGLE_BIT_DESC[4] = { "noop", "turn on", "turn off", "toggle" };
                            bool hot = state->current_chunk == i && state->current_state == EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                            if (state->current_chunk == i) {
                                scr_printf("toggle: (\033[4;1mr\033[moom) %s (\033[4;1ms\033[mwitch) ", target_name);
                                PRINTF_DATA(chunk->switch_idx);
                            } else {
                                scr_printf("toggle: (room) %s (switch) %u", target_name, chunk->switch_idx);
                            }
                            scr_printf("\n"); bottom++;
                            scr_printf("        ");
                            if (hot) {
                                scr_printf("(o\033[4;1mn\033[m)=%s (o\033[4;1mf\033[mf)=%s", TOGGLE_BIT_DESC[chunk->on], TOGGLE_BIT_DESC[chunk->off]);
                            } else {
                                scr_printf("(on)=%s (off)=%s", TOGGLE_BIT_DESC[chunk->on], TOGGLE_BIT_DESC[chunk->off]);
                            }
                        }; break;

                        case TOGGLE_OBJECT:
                        {
                            if (state->current_chunk == i) {
                                scr_printf("object: (\033[4;1mi\033[mdx)=%d (\033[4;1mo\033[mn/off)=%s", chunk->index, (chunk->test & 0x20) ? "on" : "off");
                            } else {
                                scr_printf("object: (idx)=%d (on/off)=%s", chunk->index, (chunk->test & 0x20) ? "on" : "off");
                            }
                            {
                                bool hot = state->current_chunk == i && state->current_state == EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS;
                                scr_printf("\n"); bottom++;
                                scr_printf("        ");
                                draw_movertab_fields(chunk->value, chunk_target_is_sprite(chunk), hot && !state->chunk_value_raw, state->chunk_value_focus, false);
                                scr_printf("  ");
                                if (hot && state->chunk_value_raw) {
                                    // Only reachable once 'r' has armed raw entry (Hugh: "only
                                    // let you edit raw if you push r... toggle raw edit mode with
                                    // r, that way hex keys aren't swallowed, and e still works
                                    // for test") - same digit-cursor convention as MOVERTAB's own
                                    // raw byte entry.
                                    char hi = "0123456789abcdef"[chunk->value >> 4];
                                    char lo = "0123456789abcdef"[chunk->value & 0xf];
                                    scr_printf("\033[1;4mr\033[maw: ");
                                    if (state->partial_byte) {
                                        scr_printf("%x", state->partial_byte & 0xFF);
                                        scr_printf("\033[4m%c\033[m", lo);
                                    } else {
                                        scr_printf("\033[4m%c%c\033[m", hi, lo);
                                    }
                                } else {
                                    char rawbuf[8];
                                    snprintf(rawbuf, sizeof rawbuf, "%02x", chunk->value);
                                    movertab_field(hot, "raw", rawbuf);
                                }
                            }
                        }; break;

                        default: UNREACHABLE();
                    }
                    if (i < switcz->chunks.length - 1) {
                        scr_printf("\n");
                        bottom ++;
                    }
                }
            } else {
                bottom +=1;
            }
        } else if (state->current_state == TILE_EDIT) {
            /* scr_printf("No switch here, Ctrl-s to a create new one"); */
        }
        /* Display any additional switches whose preamble is at the same cursor tile */
        if (switch_underneath != NULL && state->current_state != GOTO_SWITCH) {
            for (size_t sw_i = switch_underneath - room.switches + 1; sw_i < room.num_switches; sw_i++) {
                struct SwitchObject *extra = room.switches + sw_i;
                if (extra->chunks.length < 1) continue;
                if (extra->chunks.data[0].x != x || extra->chunks.data[0].y != y) continue;
                if (bottom >= state->screen_dimensions.y - 1) {
                    GOTO(0, bottom); bottom++;
                    scr_printf("  ... more switches at this tile not shown");
                    break;
                }
                struct SwitchChunk *pr = extra->chunks.data;
                bool has_chunk_at_cur = false;
                for (size_t c = 1; c < extra->chunks.length; c++) {
                    if (chunk_at_cursor(extra->chunks.data + c, x, y)) {
                        has_chunk_at_cur = true;
                        break;
                    }
                }
                bool extra_side_hidden = switch_side_hidden(&room, pr);
                bool extra_entry_forced = switch_entry_forced(&room, pr);
                GOTO(0, bottom); bottom++;
                scr_printf("switch ");
                PRINTF_DATA((uint16_t)sw_i);
                scr_printf(": (x,y)=%d,%d", pr->x, pr->y);
                if (!extra_entry_forced) scr_printf(" (entry)=%s", BOOL_S(pr->room_entry));
                scr_printf(" (once)=%s", BOOL_S(pr->one_time_use));
                if (!extra_side_hidden) scr_printf(" (side)=%s", SWITCH_SIDE(pr->side));
                scr_printf("%s\n", has_chunk_at_cur ? " [chunk at cursor]" : "");
                draw_switch_chunks_brief(extra, x, y, &bottom);
            }
        }
        /* Display switch whose TOGGLE_BLOCK chunk covers cursor but preamble is elsewhere */
        if (state->current_state != GOTO_SWITCH
                && chunk_switch_underneath != NULL
                && chunk_switch_underneath != switcz
                && (chunk_switch_underneath->chunks.length == 0
                    || chunk_switch_underneath->chunks.data[0].x != x
                    || chunk_switch_underneath->chunks.data[0].y != y)
                && bottom < state->screen_dimensions.y - 1) {
            struct SwitchChunk *pr = chunk_switch_underneath->chunks.data;
            bool pr_side_hidden = switch_side_hidden(&room, pr);
            bool pr_entry_forced = switch_entry_forced(&room, pr);
            GOTO(0, bottom); bottom++;
            scr_printf("switch ");
            PRINTF_DATA((uint16_t)(chunk_switch_underneath - room.switches));
            scr_printf(": (x,y)=%d,%d", pr->x, pr->y);
            if (!pr_entry_forced) scr_printf(" (entry)=%s", BOOL_S(pr->room_entry));
            scr_printf(" (once)=%s", BOOL_S(pr->one_time_use));
            if (!pr_side_hidden) scr_printf(" (side)=%s", SWITCH_SIDE(pr->side));
            scr_printf(" [chunk at cursor]\n");
            draw_switch_chunks_brief(chunk_switch_underneath, x, y, &bottom);
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
        if (found) panel_oob_row0 = bottom + 2;   /* the GUI pins the list from its heading, not the two spacer lines */
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
                if (panel_oob_switch_n < 16) { panel_oob_switch_row[panel_oob_switch_n] = bottom; panel_oob_switch_id[panel_oob_switch_n++] = (int)s; }
                GOTO(0, bottom); bottom ++;
#define BOOL_S(b) ((b) ? "true" : "false")
                // Everything in this panel is out of bounds by definition, so "side" never
                // applies here (see switch_side_hidden) - but "entry" keeps its ordinary meaning
                // for an out-of-bounds switch (see switch_entry_forced), so it's still shown.
                if (state->current_state == EDIT_SWITCHDETAILS && state->current_switch == s) {
                    scr_printf("\033[1;4mswitch id ");
                    PRINTF_DATA((uint16_t)s);
                    scr_printf("\033[m: (x,y)=");
                    PRINTF_DATA(sw->chunks.data[0].x);
                    scr_printf(",");
                    PRINTF_DATA(sw->chunks.data[0].y);
                    scr_printf(" (\033[1;4me\033[mn\033[mtry)=%s (\033[1;4mo\033[mnce)=%s\n",
                            BOOL_S(sw->chunks.data[0].room_entry), BOOL_S(sw->chunks.data[0].one_time_use));

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
                    scr_printf(" (entry)=%s (once)=%s\n",
                            BOOL_S(sw->chunks.data[0].room_entry), BOOL_S(sw->chunks.data[0].one_time_use));
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
                        /* the selected chunk (of this switch) is shown inverse; a chunk index
                         * alone says nothing, another switch may have the same one selected */
                        bool selected = state->current_chunk == i && state->current_switch == s + 1;
                        scr_printf("    ");
                        if (selected) {
                            scr_printf("\033[1;7m(");
                            PRINTF_DATA((uint16_t)i);
                            scr_printf(")\033[m ");
                        } else if (state->current_state == EDIT_SWITCHDETAILS_SELECT_CHUNK) {
                            scr_printf("\033[1;4m(");
                            PRINTF_DATA((uint16_t)i);
                            scr_printf(")\033[m ");
                        } else {
                            scr_printf("(");
                            PRINTF_DATA((uint16_t)i);
                            scr_printf(") ");
                        }
                        struct SwitchChunk *chunk = sw->chunks.data + i;
                        scr_printf("type ");
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
                                    scr_printf("block: (x,y)=");
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
                                char target_name[25];
                                room_name_trimmed(chunk->room_idx, target_name, sizeof(target_name));
                                static const char *TOGGLE_BIT_DESC[4] = { "noop", "turn on", "turn off", "toggle" };
                                scr_printf("toggle: (room) %s (switch) %u", target_name, chunk->switch_idx);
                                scr_printf("\n"); bottom++;
                                scr_printf("        (on)=%s (off)=%s", TOGGLE_BIT_DESC[chunk->on], TOGGLE_BIT_DESC[chunk->off]);
                            }; break;

                            case TOGGLE_OBJECT:
                            {
                                scr_printf("object: (idx)=%d (on/off)=%s", chunk->index, (chunk->test & 0x20) ? "on" : "off");
                                scr_printf("\n"); bottom++;
                                scr_printf("        ");
                                draw_movertab_fields(chunk->value, chunk_target_is_sprite(chunk), false, 0, false);
                                char rawbuf[8];
                                snprintf(rawbuf, sizeof rawbuf, "%02x", chunk->value);
                                scr_printf("  ");
                                movertab_field(false, "raw", rawbuf);
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
        if (found) panel_oob_row1 = bottom;
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

    /* Macro slot overlay: drawn at bottom row while awaiting a slot key */
    if (state->macro_meta || state->macro_replay_pending) {
        GOTO(0, state->screen_dimensions.y - 1);
        scr_printf("\033[7m");
        if (state->macro_replay_pending) {
            char lr = state->macro_last_replayed;
            char ls = state->macro_last_saved;
            scr_printf(",=%c .=%c  ", lr ? lr : '-', ls ? ls : '-');
        } else {
            scr_printf("m>  ");
        }
        bool any = false;
        for (int si = 0; si < 26; si++) {
            if (state->macro_slots[si].length > 0) {
                scr_printf("%c ", 'a' + si);
                any = true;
            }
        }
        if (!any) scr_printf("(empty)");
        scr_printf("\033[m");
    }

    screen_flush();
}

bool editor_hot_block_preview(int *switch_idx, int *chunk_idx, bool *on) {
    if (state->current_switch != 0 &&
            (state->current_state == EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS ||
             state->current_state == EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS)) {
        *switch_idx = (int)state->current_switch - 1;
        *chunk_idx = (int)state->current_chunk;
        *on = state->switch_on;
        return true;
    }
    if (state->current_switch != 0 && state->current_state == EDIT_SWITCHDETAILS) {
        *switch_idx = (int)state->current_switch - 1;
        *chunk_idx = -1;   // every TOGGLE_BLOCK chunk of this switch, not just one
        *on = state->switch_on;
        return true;
    }
    // 'O' outside of switch editing (see toggle_hover_switch_preview): stays live even after
    // the cursor moves away, but not past the room's own switch count - e.g. after switching
    // to a room with fewer switches, a stale high index must not be treated as valid.
    if (state->hot_preview_switch != 0) {
        struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
        if (state->hot_preview_switch <= (size_t)room->num_switches) {
            *switch_idx = (int)state->hot_preview_switch - 1;
            *chunk_idx = -1;
            *on = state->switch_on;
            return true;
        }
    }
    return false;
}
