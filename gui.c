// raylib front end for the room editor. The keys are the terminal editor's
// keys: every raylib key/mouse event is decoded into the editor's Key struct
// and run through the same state machine (editor_input.c, via gui_bridge.h),
// so modes, goto, switch/room detail editing, yank/paste, macros and help all
// behave exactly as in `./a.out editor`. The editor's text screen is drawn
// with a monospace font whose cell is half a tile wide and a tile tall
// (4*scale x 8*scale px), so the hex map region (cells 0..63 x 1..22) is
// exactly the rendered picture of the room, which replaces it. The help box,
// which the editor lays out right of the map, is drawn as an overlay on top
// of the picture.
//
// Layout follows the window: the picture scale is chosen to fill the window
// (i3 decides the size), unless --scale N pins it.
//
// GUI-only extras: F2 shows the hex map instead of the picture; Spider-Man
// is drawn at the cursor as a size reference; a tileset palette sits right of
// the picture (click a tile, Shift+drag or middle-drag in the room paints it);
// under it a world map of every room (click = go there, right-click = move
// the thing under the cursor there); the text under the picture scrolls with
// the wheel. Switch links: with a switch open (Ctrl-S), right-click an object
// or another switch to link it, or right-drag a run of tiles for a block chunk.
#include <raylib.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "gui_bridge.h"
#include "render.h"
#include "room.h"

#define PAD 8
#define MAX_REPLAY_PER_FRAME 32
#define DEFAULT_SCALE 3
#define MIN_PANEL_ROWS 12       // text rows kept visible under the picture when auto-scaling
#define SIDE_COLS 68            // panel width right of the picture, in text cells (palette + item details)
#define PALETTE_LINES 8         // text lines under the palette before the world map
#define MAP_GAP 4               // pixels between world-map thumbnails
#define SCROLLBAR_W 8
#define MAP_COLS (2 * WIDTH_TILES)
#define INIT_ROWS (1 + HEIGHT_TILES + 18)   // rows shown by the initial window (i3 resizes anyway)

// Files whose change triggers a hot rebuild of gui.so.
static const char *WATCHED[] = {
    "gui.c", "gui.h", "gui_bridge.h", "gui_editor.c", "render.c", "render.h",
    "room.c", "room.h", "spl.c", "spl.h", "array.h", "editor_types.h",
    "editor_input.c", "editor_actions.c", "editor_draw.c", "editor_screen.c",
};

typedef struct {
    size_t structsize;
    void *es;               // editor state (gui_editor.c), survives hot reloads
    int fixed_scale;        // --scale N (0 = fit the window)
    int scale;              // current picture pixels per game pixel
    RenderCtx *rctx;
    Texture2D tex;          // rendered room
    bool tex_loaded;
    bool room_dirty;        // re-render the room texture this frame
    int drawn_room;
    Texture2D spidey;       // standing Spider-Man, drawn at the cursor
    bool spidey_loaded;
    Font font;              // one monospace font: cell = 4*scale x 8*scale px
    bool font_loaded;
    int font_scale;         // scale the font was sized for
    int font_px;
    Texture2D pal_tex;      // tileset palette (current tile_offset)
    bool pal_loaded;
    int pal_offset;
    uint8_t sel_tile;       // selected palette tile (0..63)
    bool painting;          // paint stroke in progress (Shift+left / middle drag)
    int paint_x, paint_y;
    bool paint_dirty;       // painted cells not yet written to disk
    float save_timer;       // seconds until the pending paint write
    float saved_flash;      // seconds left to show "saved"
    float src_check;        // seconds until the next hot-reload source check
    bool text_map;          // F2: show the editor's hex map instead of the picture
    bool mouse_down;        // right button held over the text grid
    bool lpress;            // plain left press waiting to become a click or a drag
    int lpress_col, lpress_row;
    int mod_latch[3];       // frames a Shift/Alt/Ctrl press still counts as held (see feed_keys)
    int scroll;             // text rows scrolled off the top of the panel under the picture
    Texture2D atlas;        // world map: every room rendered, 8x8 grid of ROOM_PX_W x ROOM_PX_H
    bool atlas_loaded;
    int hover_room;         // world-map thumbnail under the mouse, or -1
    bool rpress;            // right press in link mode waiting to become a click or a run
    int rpress_tx, rpress_ty;
    Font help_font;         // the help overlay shrunk to fit the window, if it must
    bool help_font_loaded;
    int help_font_cw;
    int help_font_px;
} GuiState;
#define MOD_LATCH_FRAMES 3

static const char *FONT_CANDIDATES[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};

/* --------------------------------------------------------------- geometry */

static int cw(GuiState *g) { return 4 * g->scale; }        // text cell width
static int ch(GuiState *g) { return 8 * g->scale; }        // text cell height (= one tile)
static int pic_w(GuiState *g) { return ROOM_PX_W * g->scale; }
static int pic_h(GuiState *g) { return ROOM_PX_H * g->scale; }
static int pic_x(GuiState *g) { return GED_MAP_X * cw(g); }
static int pic_y(GuiState *g) { return GED_MAP_Y * ch(g); }
static bool in_map_band(int y) { return y >= GED_MAP_Y && y < GED_MAP_Y + HEIGHT_TILES; }
static int palette_x(GuiState *g) { return pic_x(g) + pic_w(g) + PAD; }
static int palette_y(GuiState *g) { return pic_y(g); }
static int window_w(GuiState *g) { return palette_x(g) + SIDE_COLS * cw(g) + PAD; }
static int window_h(GuiState *g) { return INIT_ROWS * ch(g); }

// Largest picture scale that fits the window with the palette beside it and
// MIN_PANEL_ROWS text rows under it.
static int fit_scale(int win_w, int win_h) {
    int sw = (win_w - 2 * PAD) / (MAP_COLS * 4 + SIDE_COLS * 4);
    int sh = win_h / (8 * (1 + HEIGHT_TILES + MIN_PANEL_ROWS));
    int s = sw < sh ? sw : sh;
    return s < 1 ? 1 : s > 8 ? 8 : s;
}

// Picture pixel of a tile coordinate.
static Vector2 tile_pt(GuiState *g, float tx, float ty) {
    return (Vector2){ pic_x(g) + tx * 8 * g->scale, pic_y(g) + ty * 8 * g->scale };
}

static bool mouse_tile(GuiState *g, int *tx, int *ty) {
    Vector2 m = GetMousePosition();
    int s = 8 * g->scale;
    if (m.x < pic_x(g) || m.x >= pic_x(g) + pic_w(g) || m.y < pic_y(g) || m.y >= pic_y(g) + pic_h(g)) return false;
    *tx = (int)(m.x - pic_x(g)) / s;
    *ty = (int)(m.y - pic_y(g)) / s;
    return true;
}

// Text cell under the mouse (the space right of the picture is the palette,
// not cells).
static bool mouse_cell(GuiState *g, int *col, int *row) {
    Vector2 m = GetMousePosition();
    if (m.x < 0 || m.y < 0) return false;
    *col = (int)m.x / cw(g);
    *row = (int)m.y / ch(g);
    if (in_map_band(*row) && *col >= GED_MAP_X + MAP_COLS) return false;
    return *col < GED_COLS && *row < GED_ROWS;
}

/* ---------------------------------------------------------------- helpers */

static const char *creature_name(SpriteType t, uint8_t dmg) {
    switch (t) {
        case MUMMY: return "Mummy";
        case BLUE_MAN: return "Blue Man";
        case WOLF: return "Wolf";
        case R2D2: return "R2D2";
        case DINOSAUR: return "Dinosaur";
        case RAT: return "Rat";
        case SHOTGUN_LADY: return "Shotgun Lady";
        case SHARK:
            if (dmg == 3) return "Mysterio";
            if (dmg == 4) return "Mary Jane";
            return "Shark";
        default: return "?";
    }
}

static bool sources_changed(long since) {
    for (size_t i = 0; i < sizeof WATCHED / sizeof WATCHED[0]; i++)
        if (GetFileModTime(WATCHED[i]) > since) return true;
    return false;
}

static void gtext(Font *font, bool have, const char *s, int x, int y, int size, Color col) {
    if (have) DrawTextEx(*font, s, (Vector2){x, y}, size, size / 10.0f, col);
    else DrawText(s, x, y, size, col);
}

static int text_w(Font *font, bool have, const char *s, int size) {
    return have ? (int)MeasureTextEx(*font, s, size, size / 10.0f).x : MeasureText(s, size);
}

// Monospace font whose glyph advance is `cell_w` pixels (DejaVu Sans Mono
// advances 0.602 em).
static bool load_mono(Font *font, int cell_w, int *px) {
    *px = (int)(cell_w / 0.602f + 0.5f);
    for (size_t i = 0; i < sizeof FONT_CANDIDATES / sizeof FONT_CANDIDATES[0]; i++)
        if (FileExists(FONT_CANDIDATES[i])) {
            *font = LoadFontEx(FONT_CANDIDATES[i], *px, NULL, 0);
            SetTextureFilter(font->texture, TEXTURE_FILTER_BILINEAR);
            return font->texture.id != 0;
        }
    return false;
}

static void load_fonts(GuiState *g) {
    if (g->font_loaded && g->font_scale != g->scale) { UnloadFont(g->font); g->font_loaded = false; }
    if (!g->font_loaded) {
        g->font_loaded = load_mono(&g->font, cw(g), &g->font_px);
        g->font_scale = g->scale;
    }
}

/* ------------------------------------------------------------------ input */

static int  panel_top(GuiState *g);
static int  world_room_at(GuiState *g, Vector2 m);
static void link_run(int x0, int y0, int x1, int y1, int *rx, int *ry, int *rw, int *rh);

// Modifier bits from the keys currently held.
static uint8_t mods(void) {
    uint8_t m = 0;
    if (IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT))   m |= GK_MOD_SHIFT;
    if (IsKeyDown(KEY_LEFT_ALT)     || IsKeyDown(KEY_RIGHT_ALT))     m |= GK_MOD_ALT;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) m |= GK_MOD_CTRL;
    return m;
}

static bool is_mod_key(int rk, uint8_t *bit) {
    switch (rk) {
        case KEY_LEFT_SHIFT: case KEY_RIGHT_SHIFT:     *bit = GK_MOD_SHIFT; return true;
        case KEY_LEFT_ALT: case KEY_RIGHT_ALT:         *bit = GK_MOD_ALT;   return true;
        case KEY_LEFT_CONTROL: case KEY_RIGHT_CONTROL: *bit = GK_MOD_CTRL;  return true;
        default: return false;
    }
}

// Decode raylib's queued key events into editor keys. Reading the event
// queues (not IsKeyPressed) means a tap during a long frame — a room save
// takes ~50 ms — is never lost. Modifiers are taken from the keys currently
// held OR from modifier presses seen in the queue during the last few
// frames: a quick Ctrl-D whose Ctrl press and release land inside one frame,
// or straddle a frame boundary with the D, would otherwise arrive as plain D.
static bool feed_keys(GuiState *g) {
    bool any = false;
    #define SEND(k) do { if (!any) { ged_new_input_batch(); any = true; } ged_key(k); } while (0)

    int keys[64], n = 0;
    for (int rk = GetKeyPressed(); rk != 0 && n < 64; rk = GetKeyPressed()) keys[n++] = rk;
    uint8_t held = mods(), seen_any = 0, bit;
    for (int i = 0; i < n; i++)
        if (is_mod_key(keys[i], &bit)) { seen_any |= bit; g->mod_latch[bit >> 1] = MOD_LATCH_FRAMES; }
    for (int b = 0; b < 3; b++)
        if (g->mod_latch[b] > 0) { held |= (uint8_t)(1 << b); g->mod_latch[b]--; }

    // printable characters: shift is already applied; Alt rides along as a modifier
    for (int c = GetCharPressed(); c != 0; c = GetCharPressed()) {
        if (c < 0x20 || c > 0x7e) continue;
        GKey k = { .code = GK_CHAR, .ch = (char)c, .mod = (uint8_t)((held | seen_any) & GK_MOD_ALT) };
        SEND(k);
    }
    // everything else: control combinations and special keys
    uint8_t seen = 0;
    for (int i = 0; i < n; i++) {
        int rk = keys[i];
        if (is_mod_key(rk, &bit)) { seen |= bit; continue; }
        uint8_t m = held | seen;
        GKey k = { .mod = m };
        bool ok = true;
        if (rk == KEY_F2) { g->text_map = !g->text_map; continue; }   // GUI-only: hex map toggle
        if (rk >= KEY_A && rk <= KEY_Z) {
            if (!(m & GK_MOD_CTRL)) continue;                          // plain letters arrived as chars
            k.code = GK_CTRL; k.ch = (char)('A' + rk - KEY_A); k.mod = (uint8_t)(m & GK_MOD_ALT);
        } else if ((rk == KEY_SLASH || rk == KEY_MINUS) && (m & GK_MOD_CTRL)) {
            k.code = GK_CTRL; k.ch = '_'; k.mod = 0;                   // Ctrl-? / Ctrl-_ : help
        } else switch (rk) {
            case KEY_ENTER: case KEY_KP_ENTER: k.code = GK_CTRL; k.ch = 'M'; k.mod = 0; break;
            case KEY_TAB:       k.code = GK_CTRL; k.ch = 'I'; k.mod = 0; break;
            case KEY_ESCAPE:    k.code = GK_ESC; k.mod = 0; break;
            case KEY_BACKSPACE: k.code = GK_BS; k.mod = 0; break;
            case KEY_DELETE:    k.code = GK_DELETE; k.mod = 0; break;
            case KEY_UP:        k.code = GK_UP; break;
            case KEY_DOWN:      k.code = GK_DOWN; break;
            case KEY_LEFT:      k.code = GK_LEFT; break;
            case KEY_RIGHT:     k.code = GK_RIGHT; break;
            case KEY_HOME:      k.code = GK_HOME; break;
            case KEY_END:       k.code = GK_END; break;
            case KEY_PAGE_UP:   k.code = GK_PAGE_UP; break;
            case KEY_PAGE_DOWN: k.code = GK_PAGE_DOWN; break;
            case KEY_INSERT:    k.code = GK_INSERT; break;
            case KEY_F1:  k.code = GK_F1;  break; case KEY_F3:  k.code = GK_F3;  break;
            case KEY_F4:  k.code = GK_F4;  break; case KEY_F5:  k.code = GK_F5;  break;
            case KEY_F6:  k.code = GK_F6;  break; case KEY_F7:  k.code = GK_F7;  break;
            case KEY_F8:  k.code = GK_F8;  break; case KEY_F9:  k.code = GK_F9;  break;
            case KEY_F10: k.code = GK_F10; break; case KEY_F11: k.code = GK_F11; break;
            case KEY_F12: k.code = GK_F12; break;
            default: ok = false; break;
        }
        if (ok) SEND(k);
    }
    for (int i = 0; i < MAX_REPLAY_PER_FRAME && ged_replay_step(); i++) any = true;
    #undef SEND
    return any;
}

// Mouse: left / right / Ctrl+left go to the editor exactly as terminal mouse
// events (cell coordinates); Shift+left or middle paints the selected tile.
static bool feed_mouse(GuiState *g) {
    bool any = false;
    Vector2 m = GetMousePosition();
    int col = 0, row = 0;
    bool in_text = mouse_cell(g, &col, &row);
    uint8_t mod = mods();
    bool paint_btn = IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) ||
                     (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && (mod & GK_MOD_SHIFT));
    int tx, ty;

    if (paint_btn && mouse_tile(g, &tx, &ty)) {
        ged_paint_begin();                        // one undo entry per stroke
        g->painting = true;
        g->paint_x = tx; g->paint_y = ty;
        ged_paint(tx, ty, g->sel_tile);
        g->paint_dirty = true; g->save_timer = 0.4f;
        return true;
    }
    if (g->painting) {
        bool held = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        if (!held) { g->painting = false; }
        else if (mouse_tile(g, &tx, &ty) && (tx != g->paint_x || ty != g->paint_y)) {
            g->paint_x = tx; g->paint_y = ty;
            ged_paint(tx, ty, g->sel_tile);
            g->paint_dirty = true; g->save_timer = 0.4f;
            any = true;
        }
        return any;
    }

    // palette: pick a tile
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int s = 8 * g->scale, px0 = palette_x(g), py0 = palette_y(g);
        if (m.x >= px0 && m.x < px0 + 8 * s && m.y >= py0 && m.y < py0 + 8 * s) {
            g->sel_tile = (uint8_t)(((int)(m.y - py0) / s) * 8 + (int)(m.x - px0) / s);
            return true;
        }
    }

    // wheel over the text under the picture scrolls it
    float wheel = GetMouseWheelMove();
    if (wheel != 0 && m.x < pic_w(g) && m.y >= panel_top(g)) {
        g->scroll -= (int)wheel * 3;
        if (g->scroll < 0) g->scroll = 0;
        return true;
    }

    // world map: hover names, left = go there, right = move the thing / retarget the chunk
    g->hover_room = world_room_at(g, m);
    if (g->hover_room >= 0 && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
        ged_new_input_batch();
        ged_map_click(g->hover_room, IsMouseButtonPressed(MOUSE_BUTTON_RIGHT));
        return true;
    }

    // switch links (a switch is open with Ctrl-S): right-click an object or a
    // switch links it, right-drag a run of tiles makes a block chunk
    if (ged_linking()) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && mouse_tile(g, &tx, &ty)) {
            g->rpress = true; g->rpress_tx = tx; g->rpress_ty = ty;
            return false;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && g->rpress) {
            g->rpress = false;
            if (!mouse_tile(g, &tx, &ty)) return false;
            ged_new_input_batch();
            int idx, kind = ged_item_at(g->rpress_tx, g->rpress_ty, &idx);
            if (tx == g->rpress_tx && ty == g->rpress_ty && kind == 1 && idx != ged_linking() - 1) ged_link_switch(idx);
            else if (tx == g->rpress_tx && ty == g->rpress_ty && kind == 2) ged_link_object(idx);
            else {
                int rx, ry, rw, rh;
                link_run(g->rpress_tx, g->rpress_ty, tx, ty, &rx, &ry, &rw, &rh);
                ged_link_block(rx, ry, rw, rh);
            }
            return true;
        }
        if (g->rpress) return false;
    }

    // Right click (and Ctrl+left): the editor acts on the press, as in the terminal.
    bool ctrl_left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && (mod & GK_MOD_CTRL);
    if ((IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || ctrl_left) && in_text) {
        GKey k = { .code = GK_MOUSE, .col = col, .row = row, .button = ctrl_left ? 0 : 2,
                   .mod = (uint8_t)(mod & (GK_MOD_ALT | GK_MOD_CTRL)) };
        ged_new_input_batch();
        ged_key(k);
        g->mouse_down = true;
        return true;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && g->mouse_down) {
        GKey k = { .code = GK_MOUSE, .col = col, .row = row, .button = 2, .release = true };
        ged_key(k);
        g->mouse_down = false;
        return true;
    }

    // Plain left: the editor moves the cursor on the press and drags the item
    // under it on release. With Spider-Man drawn at the cursor that yanks him
    // onto whatever is being dragged, so the press is held back until release:
    // a click moves the cursor there; a drag moves the item and leaves the
    // cursor where it was.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && in_text) {
        g->lpress = true; g->lpress_col = col; g->lpress_row = row;
        return false;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && g->lpress) {
        g->lpress = false;
        int cx, cy, room = ged_room();
        ged_cursor(&cx, &cy);
        bool drag = in_text && (col != g->lpress_col || row != g->lpress_row);
        GKey press = { .code = GK_MOUSE, .col = g->lpress_col, .row = g->lpress_row, .button = 0 };
        GKey release = { .code = GK_MOUSE, .col = drag ? col : g->lpress_col,
                         .row = drag ? row : g->lpress_row, .button = 0, .release = true };
        ged_new_input_batch();
        ged_key(press);
        ged_key(release);
        if (drag && ged_room() == room) ged_set_cursor(cx, cy);   // (a drag into a neighbour room follows the item)
        return true;
    }
    return any;
}

/* ---------------------------------------------------------------- drawing */

// ANSI colour n (1..8 = black..white) as used by the editor's text screen.
static Color ansi(uint8_t n, bool bold) {
    static const Color normal[9] = {
        {204, 204, 204, 255}, {0, 0, 0, 255}, {205, 49, 49, 255}, {13, 188, 121, 255},
        {229, 229, 16, 255}, {36, 114, 200, 255}, {188, 63, 188, 255}, {17, 168, 205, 255},
        {229, 229, 229, 255},
    };
    static const Color bright[9] = {
        {255, 255, 255, 255}, {102, 102, 102, 255}, {241, 76, 76, 255}, {35, 209, 139, 255},
        {245, 245, 67, 255}, {59, 142, 234, 255}, {214, 112, 214, 255}, {41, 184, 219, 255},
        {255, 255, 255, 255},
    };
    if (n > 8) n = 0;
    return bold ? bright[n] : normal[n];
}

static void draw_cell(const GCell *c, int px, int py, int cw, int ch, Font *font, bool have_font, int fpx) {
    if (c->bg) DrawRectangle(px, py, cw, ch, ansi(c->bg, false));
    if (c->ch && c->ch != ' ') {
        Color col = ansi(c->fg, c->attr & 1);
        int ty = py + (ch - fpx) / 2;
        if (have_font) DrawTextCodepoint(*font, c->ch, (Vector2){px, ty}, fpx, col);
        else DrawText(TextFormat("%c", c->ch), px, ty, fpx, col);
    }
}

// Bottom of the picture = top of the scrolling text panel.
static int panel_top(GuiState *g) { return pic_y(g) + pic_h(g); }
static int panel_rows_visible(GuiState *g) { int r = (GetScreenHeight() - panel_top(g)) / ch(g); return r < 1 ? 1 : r; }

// Last text row under the map with anything on it (>= first panel row).
static int panel_last_row(const GCell *cells, int stride) {
    int last = GED_MAP_Y + HEIGHT_TILES - 1;
    for (int y = GED_MAP_Y + HEIGHT_TILES; y < GED_ROWS - 1; y++)
        for (int x = 0; x < GED_COLS; x++) {
            const GCell *c = &cells[y * stride + x];
            if (c->bg || (c->ch && c->ch != ' ')) { last = y; break; }
        }
    return last;
}

static int panel_max_scroll(GuiState *g) {
    int stride;
    const GCell *cells = ged_cells(&stride);
    int content = panel_last_row(cells, stride) - (GED_MAP_Y + HEIGHT_TILES) + 1;
    int m = content - panel_rows_visible(g);
    return m < 0 ? 0 : m;
}

// Everything the editor drew except the help box (right of the hex map; it
// can run below the map band too — draw_help shows it as an overlay) and,
// unless F2, the hex map itself. Rows under the map scroll (`scroll`); the
// editor's last row (macro overlay) is pinned to the bottom of the window.
static void draw_text_screen(GuiState *g) {
    int stride;
    const GCell *cells = ged_cells(&stride);
    bool help = ged_help_shown(), text = g->text_map || ged_map_is_text();
    int first_panel = GED_MAP_Y + HEIGHT_TILES;
    int max_scroll = panel_max_scroll(g);
    if (g->scroll > max_scroll) g->scroll = max_scroll;
    BeginScissorMode(0, panel_top(g), pic_w(g), GetScreenHeight() - panel_top(g));
    for (int y = first_panel; y < GED_ROWS - 1; y++) {
        int py = panel_top(g) + (y - first_panel - g->scroll) * ch(g);
        if (py + ch(g) <= panel_top(g) || py >= GetScreenHeight()) continue;
        for (int x = 0; x < GED_COLS; x++) {
            if (x >= GED_MAP_X + MAP_COLS && help) continue;
            draw_cell(&cells[y * stride + x], x * cw(g), py, cw(g), ch(g), &g->font, g->font_loaded, g->font_px);
        }
    }
    EndScissorMode();
    if (max_scroll > 0) {                       // scrollbar along the picture's right edge
        int top = panel_top(g), hgt = GetScreenHeight() - top, x = pic_w(g) - SCROLLBAR_W;
        int total = max_scroll + panel_rows_visible(g);
        int th = hgt * panel_rows_visible(g) / total, ty = top + hgt * g->scroll / total;
        DrawRectangle(x, top, SCROLLBAR_W, hgt, (Color){40, 40, 40, 255});
        DrawRectangle(x + 1, ty, SCROLLBAR_W - 2, th < 12 ? 12 : th, (Color){170, 170, 170, 255});
    }
    for (int y = 0; y < first_panel; y++) {     // header + (F2 / room list) map band
        for (int x = 0; x < GED_COLS; x++) {
            if (x >= GED_MAP_X + MAP_COLS && (help || in_map_band(y))) continue;
            if (in_map_band(y) && !text) continue;
            draw_cell(&cells[y * stride + x], x * cw(g), y * ch(g), cw(g), ch(g), &g->font, g->font_loaded, g->font_px);
        }
    }
    int last = GED_ROWS - 1;                    // macro slot overlay: pinned to the window bottom
    for (int x = 0; x < GED_COLS; x++)
        draw_cell(&cells[last * stride + x], x * cw(g), GetScreenHeight() - ch(g), cw(g), ch(g), &g->font, g->font_loaded, g->font_px);
    if (text) {                                 // faint tile grid over the hex map
        Color grid = {255, 255, 255, 40};
        for (int tx = 0; tx <= WIDTH_TILES; tx++) DrawLineV(tile_pt(g, tx, 0), tile_pt(g, tx, HEIGHT_TILES), grid);
        for (int ty = 0; ty <= HEIGHT_TILES; ty++) DrawLineV(tile_pt(g, 0, ty), tile_pt(g, WIDTH_TILES, ty), grid);
    }
}

// The editor's help box lives right of the hex map in its text screen; show
// it centred over the picture instead, always on top, shrunk if the window is
// too small for it at the text size.
static void draw_help(GuiState *g) {
    int stride;
    const GCell *cells = ged_cells(&stride);
    int x0 = GED_COLS, x1 = -1, y0 = GED_ROWS, y1 = -1;
    for (int y = 0; y < GED_ROWS; y++)
        for (int x = GED_MAP_X + MAP_COLS; x < GED_COLS; x++) {
            const GCell *c = &cells[y * stride + x];
            if (!c->bg && !(c->ch && c->ch != ' ')) continue;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    if (x1 < 0) return;
    int cols = x1 - x0 + 1, rows = y1 - y0 + 1;
    int hch = ch(g);
    if (rows * hch > GetScreenHeight() - 2 * PAD) hch = (GetScreenHeight() - 2 * PAD) / rows;
    if (cols * hch / 2 > GetScreenWidth() - 2 * PAD) hch = 2 * (GetScreenWidth() - 2 * PAD) / cols;
    if (hch < 8) hch = 8;
    int hcw = hch / 2;
    Font *font = &g->font;
    bool have = g->font_loaded;
    int fpx = g->font_px;
    if (hcw != cw(g)) {
        if (g->help_font_loaded && g->help_font_cw != hcw) { UnloadFont(g->help_font); g->help_font_loaded = false; }
        if (!g->help_font_loaded) {
            g->help_font_loaded = load_mono(&g->help_font, hcw, &g->help_font_px);
            g->help_font_cw = hcw;
        }
        font = &g->help_font; have = g->help_font_loaded; fpx = g->help_font_px;
    }
    int w = cols * hcw, h = rows * hch;
    int ox = pic_x(g) + (pic_w(g) - w) / 2, oy = pic_y(g) + (pic_h(g) - h) / 2;
    if (ox < PAD) ox = PAD;
    if (oy < PAD) oy = PAD;
    DrawRectangle(ox - PAD, oy - PAD, w + 2 * PAD, h + 2 * PAD, (Color){0, 0, 0, 235});
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            draw_cell(&cells[y * stride + x], ox + (x - x0) * hcw, oy + (y - y0) * hch, hcw, hch, font, have, fpx);
}

static void draw_cursor(GuiState *g) {
    int cx, cy;
    ged_cursor(&cx, &cy);
    if (cx < 0 || cx >= WIDTH_TILES || cy < 0 || cy >= HEIGHT_TILES) return;
    Vector2 p = tile_pt(g, cx, cy);
    float s = 8.0f * g->scale;
    // Spider-Man standing with his feet on the cursor's row: how tall he is here
    if (g->spidey_loaded) {
        Vector2 sp = tile_pt(g, cx - 2, cy + 1);
        sp.x += 3 * g->scale;
        sp.y -= SPIDS_FRAME_H * g->scale;
        DrawTexturePro(g->spidey, (Rectangle){0, 0, SPIDS_FRAME_W, SPIDS_FRAME_H},
                       (Rectangle){sp.x, sp.y, SPIDS_FRAME_W * g->scale, SPIDS_FRAME_H * g->scale},
                       (Vector2){0, 0}, 0, (Color){255, 255, 255, 210});
    }
    Rectangle box = { p.x, p.y, s, s };
    DrawRectangleLinesEx(box, 2, (Color){255, 255, 255, 230});
    DrawRectangleLinesEx((Rectangle){box.x - 1, box.y - 1, box.width + 2, box.height + 2}, 1, BLACK);
}

static void draw_palette(GuiState *g) {
    int s = 8 * g->scale, x0 = palette_x(g), y0 = palette_y(g), fs = g->font_px;
    DrawTexturePro(g->pal_tex, (Rectangle){0, 0, TILESET_PX, TILESET_PX},
                   (Rectangle){x0, y0, TILESET_PX * g->scale, TILESET_PX * g->scale},
                   (Vector2){0, 0}, 0, WHITE);
    for (int i = 0; i <= 8; i++) {
        DrawLine(x0 + i * s, y0, x0 + i * s, y0 + 8 * s, (Color){80, 80, 80, 255});
        DrawLine(x0, y0 + i * s, x0 + 8 * s, y0 + i * s, (Color){80, 80, 80, 255});
    }
    if (g->text_map)                             // F2: the tile ids, as on the hex map
        for (int t = 0; t < 64; t++) {
            int tx = x0 + (t % 8) * s, ty = y0 + (t / 8) * s;
            DrawRectangle(tx, ty, s, ch(g), (Color){0, 0, 0, 150});
            gtext(&g->font, g->font_loaded, TextFormat("%02x", t), tx, ty, fs, RAYWHITE);
        }
    int sx = g->sel_tile % 8, sy = g->sel_tile / 8;
    DrawRectangleLinesEx((Rectangle){x0 + sx * s, y0 + sy * s, s, s}, 2, GOLD);
    int ly = y0 + TILESET_PX * g->scale + PAD, lh = ch(g);
    int link = ged_linking();
    const char *lines[] = {
        TextFormat("tileset %d  selected 0x%02x", g->pal_offset, g->sel_tile),
        "click a tile; Shift+drag or",
        "middle-drag in the room paints",
        "wheel scrolls the text below",
        "",
        link ? TextFormat("switch %d open: right-click an object", link - 1)
             : "world map: click = go there",
        link ? "or switch links it; right-drag tiles"
             : "right-click = move thing there",
        link ? "makes a block chunk (Esc closes)" : "Ctrl-? help   F2 hex map",
    };
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++)
        gtext(&g->font, g->font_loaded, lines[i], x0, ly + (int)i * lh, fs,
              i == 0 ? RAYWHITE : (link && i >= 5) ? ORANGE : GRAY);
    if (g->saved_flash > 0)
        gtext(&g->font, g->font_loaded, "saved", x0 + (TILESET_PX + 8) * g->scale, y0, fs, GREEN);
}

/* -------------------------------------------------------------- world map */

static int world_y(GuiState *g) { return palette_y(g) + TILESET_PX * g->scale + PAD + PALETTE_LINES * ch(g) + PAD; }

// Thumbnail size: 8 columns across the space right of the picture, shrunk if
// 8 rows would not fit the window.
static void world_geom(GuiState *g, int *tw, int *th) {
    int avail_w = GetScreenWidth() - palette_x(g) - PAD;
    int w = (avail_w - 7 * MAP_GAP) / 8;
    int avail_h = GetScreenHeight() - world_y(g) - PAD;
    int hmax = (avail_h - 7 * MAP_GAP) / 8;
    if (w * ROOM_PX_H / ROOM_PX_W > hmax) w = hmax * ROOM_PX_W / ROOM_PX_H;
    if (w < 16) w = 16;
    *tw = w; *th = w * ROOM_PX_H / ROOM_PX_W;
}

static Rectangle world_cell(GuiState *g, int room, int tw, int th) {
    return (Rectangle){ palette_x(g) + (room % 8) * (tw + MAP_GAP), world_y(g) + (room / 8) * (th + MAP_GAP), tw, th };
}

static int world_room_at(GuiState *g, Vector2 m) {
    int tw, th;
    world_geom(g, &tw, &th);
    for (int r = 0; r < 64; r++)
        if (CheckCollisionPointRec(m, world_cell(g, r, tw, th))) return r;
    return -1;
}

// Render room `room` into its slot of the atlas.
static void world_update_room(GuiState *g, RoomFile *rooms, int room, uint8_t *buf) {
    if (!rooms->rooms[room].valid) { memset(buf, 0, (size_t)ROOM_PX_W * ROOM_PX_H * 3); }
    else render_room_rgb(g->rctx, rooms, room, buf);
    UpdateTextureRec(g->atlas, (Rectangle){(room % 8) * ROOM_PX_W, (room / 8) * ROOM_PX_H, ROOM_PX_W, ROOM_PX_H}, buf);
}

static void world_build(GuiState *g, RoomFile *rooms, uint8_t *buf) {
    if (!g->atlas_loaded) {
        Image blank = GenImageColor(8 * ROOM_PX_W, 8 * ROOM_PX_H, BLACK);
        ImageFormat(&blank, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        g->atlas = LoadTextureFromImage(blank);
        UnloadImage(blank);
        SetTextureFilter(g->atlas, TEXTURE_FILTER_BILINEAR);
        g->atlas_loaded = true;
    }
    for (int r = 0; r < 64; r++) world_update_room(g, rooms, r, buf);
}

static void draw_world_map(GuiState *g) {
    RoomFile *rooms = ged_rooms();
    struct DecompressedRoom *cur = &rooms->rooms[ged_room()].data;
    int tw, th, fs = g->font_px;
    world_geom(g, &tw, &th);
    for (int r = 0; r < 64; r++) {
        Rectangle d = world_cell(g, r, tw, th);
        if (!rooms->rooms[r].valid) { DrawRectangleRec(d, (Color){25, 25, 30, 255}); continue; }
        DrawTexturePro(g->atlas, (Rectangle){(r % 8) * ROOM_PX_W, (r / 8) * ROOM_PX_H, ROOM_PX_W, ROOM_PX_H}, d, (Vector2){0, 0}, 0, WHITE);
        const char *side = r == cur->room_north ? "N" : r == cur->room_south ? "S" :
                           r == cur->room_west ? "W" : r == cur->room_east ? "E" : NULL;
        if (r == ged_room()) DrawRectangleLinesEx(d, 2, YELLOW);
        else if (side) {
            DrawRectangleLinesEx(d, 2, SKYBLUE);
            DrawRectangle((int)d.x, (int)d.y, cw(g) + 2, ch(g), (Color){0, 0, 0, 170});
            gtext(&g->font, g->font_loaded, side, (int)d.x + 1, (int)d.y, fs, SKYBLUE);
        }
        if (r == g->hover_room) DrawRectangleLinesEx(d, 2, WHITE);
    }
    if (g->hover_room >= 0 && rooms->rooms[g->hover_room].valid) {   // name of the hovered room
        char name[25] = {0};
        strncpy(name, rooms->rooms[g->hover_room].data.name, 24);
        for (int j = 23; j >= 0 && (name[j] == ' ' || name[j] == '\0'); j--) name[j] = '\0';
        const char *t = TextFormat("%02x  %s", g->hover_room, name);
        Rectangle d = world_cell(g, g->hover_room, tw, th);
        int w = text_w(&g->font, g->font_loaded, t, fs), x = (int)d.x, y = (int)d.y - ch(g) - 2;
        if (x + w > GetScreenWidth() - PAD) x = GetScreenWidth() - PAD - w;
        if (y < world_y(g) - ch(g)) y = (int)(d.y + d.height) + 2;
        DrawRectangle(x - 2, y - 1, w + 4, ch(g) + 2, (Color){0, 0, 0, 220});
        gtext(&g->font, g->font_loaded, t, x, y, fs, RAYWHITE);
    }
}

// Rubber band for a right-drag block link: the run of tiles from the press
// tile to the hovered one, along the longer axis.
static void link_run(int x0, int y0, int x1, int y1, int *rx, int *ry, int *rw, int *rh) {
    int dx = x1 - x0, dy = y1 - y0;
    if (abs(dx) >= abs(dy)) { *rx = dx < 0 ? x1 : x0; *ry = y0; *rw = abs(dx) + 1; *rh = 1; }
    else                    { *rx = x0; *ry = dy < 0 ? y1 : y0; *rw = 1; *rh = abs(dy) + 1; }
}

static void draw_link_drag(GuiState *g) {
    if (!g->rpress || !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) return;
    int tx, ty, rx, ry, rw, rh;
    if (!mouse_tile(g, &tx, &ty)) return;
    link_run(g->rpress_tx, g->rpress_ty, tx, ty, &rx, &ry, &rw, &rh);
    Vector2 p = tile_pt(g, rx, ry);
    float s = 8.0f * g->scale;
    DrawRectangleLinesEx((Rectangle){p.x, p.y, rw * s, rh * s}, 3, ORANGE);
}

// While a left drag is in progress: outline the tile it started on and point
// at the tile it would end on.
static void draw_drag(GuiState *g) {
    if (!g->lpress || !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) return;
    int tx, ty;
    if (!in_map_band(g->lpress_row) || !mouse_tile(g, &tx, &ty)) return;
    int sx = (g->lpress_col - GED_MAP_X) / 2, sy = g->lpress_row - GED_MAP_Y;
    if (tx == sx && ty == sy) return;
    float s = 8.0f * g->scale;
    Vector2 a = tile_pt(g, sx, sy), b = tile_pt(g, tx, ty);
    DrawRectangleLinesEx((Rectangle){a.x, a.y, s, s}, 2, (Color){255, 255, 255, 120});
    DrawLineEx((Vector2){a.x + s / 2, a.y + s / 2}, (Vector2){b.x + s / 2, b.y + s / 2}, 2, (Color){255, 255, 255, 160});
    DrawRectangleLinesEx((Rectangle){b.x, b.y, s, s}, 2, (Color){255, 255, 255, 200});
}

// Ghost of the selected tile at the hovered cell while the paint modifier is held.
static void draw_hover_preview(GuiState *g) {
    int tx, ty;
    if (!(mods() & GK_MOD_SHIFT) && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) return;
    if (!mouse_tile(g, &tx, &ty)) return;
    Vector2 p = tile_pt(g, tx, ty);
    int s = 8 * g->scale;
    Rectangle dst = { p.x, p.y, s, s };
    Rectangle src = { (g->sel_tile % 8) * 8, (g->sel_tile / 8) * 8, 8, 8 };
    DrawTexturePro(g->pal_tex, src, dst, (Vector2){0, 0}, 0, (Color){255, 255, 255, 150});
    DrawRectangleLinesEx(dst, 2, GOLD);
}

// Label with a dark backdrop; `emph` (goto mode) makes it bigger and brighter.
static void label(GuiState *g, const char *s, float x, float y, Color col, bool emph) {
    int fs = g->font_px + (emph ? 6 : 0);
    int w = text_w(&g->font, g->font_loaded, s, fs);
    DrawRectangle((int)x - 2, (int)y - 1, w + 4, fs + 2, (Color){0, 0, 0, emph ? 220 : 150});
    gtext(&g->font, g->font_loaded, s, (int)x, (int)y, fs, emph ? YELLOW : col);
}

// Object / switch outlines over the picture, following the editor's Ctrl-D
// display toggles. In goto-object / goto-switch mode the ids are emphasised.
static void draw_overlay(GuiState *g) {
    struct DecompressedRoom *r = &ged_rooms()->rooms[ged_room()].data;
    float s = 8.0f * g->scale;
    int kind = ged_goto_kind();
    bool emph_obj = kind == 1, emph_sw = kind == 2;
    Color C_SW = MAGENTA, C_BLK = SKYBLUE, C_OBJ = (Color){96,160,255,255},
          C_BIT = GREEN, C_TBLK = ORANGE, C_SPR = YELLOW;
    int fs = g->font_px;

    if (ged_show_objects() || emph_obj) {
        for (int i = 0; i < r->num_objects; i++) {
            struct RoomObject *o = r->objects + i;
            Vector2 p = tile_pt(g, o->x, o->y);
            float thick = emph_obj ? 3 : 1;
            if (o->type == BLOCK) {
                DrawRectangleLinesEx((Rectangle){p.x, p.y, o->block.width * s, o->block.height * s}, thick, emph_obj ? YELLOW : C_BLK);
                label(g, TextFormat("O%d", i), p.x + 2, p.y + 1, C_BLK, emph_obj);
            } else {
                DrawRectangleLinesEx((Rectangle){p.x, p.y, s, s}, thick, C_SPR);
                label(g, TextFormat("O%d %s d%d", i, creature_name(o->sprite.type, o->sprite.damage), o->sprite.damage),
                      p.x + 2, p.y - fs - 3, C_SPR, emph_obj);
            }
        }
    }
    if (!ged_show_switches() && !emph_sw) return;
    for (int i = 0; i < r->num_switches; i++) {
        struct SwitchObject *sw = r->switches + i;
        if (sw->chunks.length == 0) continue;
        Vector2 o = tile_pt(g, sw->chunks.data[0].x + 0.5f, sw->chunks.data[0].y + 0.5f);
        DrawCircleV(o, emph_sw ? 6 : 4, emph_sw ? YELLOW : C_SW);
        label(g, TextFormat("S%d", i), o.x + 6, o.y - fs / 2, C_SW, emph_sw);
        for (size_t c = 1; c < sw->chunks.length; c++) {
            struct SwitchChunk *ch = sw->chunks.data + c;
            if (ch->type == TOGGLE_BLOCK) {
                int w = ch->dir == HORIZONTAL ? ch->size : 1;
                int h = ch->dir == VERTICAL ? ch->size : 1;
                Vector2 t = tile_pt(g, ch->x, ch->y);
                DrawRectangleLinesEx((Rectangle){t.x, t.y, w * s, h * s}, 1, C_TBLK);
                DrawLineV(o, tile_pt(g, ch->x + 0.5f, ch->y + 0.5f), C_TBLK);
            } else if (ch->type == TOGGLE_OBJECT && ch->index < r->num_objects) {
                struct RoomObject *t = r->objects + ch->index;
                float cw = t->type == BLOCK ? t->block.width / 2.0f : 0.5f;
                float chh = t->type == BLOCK ? t->block.height / 2.0f : 0.5f;
                DrawLineV(o, tile_pt(g, t->x + cw, t->y + chh), C_OBJ);
            } else if (ch->type == TOGGLE_BIT) {
                Vector2 e = o;
                int rid = ch->room_idx;
                if (rid == r->room_north) e = tile_pt(g, sw->chunks.data[0].x + 0.5f, 0);
                else if (rid == r->room_south) e = tile_pt(g, sw->chunks.data[0].x + 0.5f, HEIGHT_TILES);
                else if (rid == r->room_west) e = tile_pt(g, 0, sw->chunks.data[0].y + 0.5f);
                else if (rid == r->room_east) e = tile_pt(g, WIDTH_TILES, sw->chunks.data[0].y + 0.5f);
                if (e.x != o.x || e.y != o.y) {
                    DrawLineV(o, e, C_BIT);
                    label(g, TextFormat("R%d", rid), e.x - 8, e.y - 7, C_BIT, false);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ loop */

void *gui_loop(const char *library, RoomFile *file, const char *data_dir, int scale, void *state) {
    long last_build = GetFileModTime(library);
    static uint8_t buf[ROOM_PX_W * ROOM_PX_H * 3];

    GuiState *g = state;
    if (g != NULL && g->structsize != sizeof *g) {
        fprintf(stderr, "gui: state size changed, resetting\n");
        if (g->tex_loaded) UnloadTexture(g->tex);
        if (g->pal_loaded) UnloadTexture(g->pal_tex);
        if (g->spidey_loaded) UnloadTexture(g->spidey);
        if (g->atlas_loaded) UnloadTexture(g->atlas);
        if (g->font_loaded) UnloadFont(g->font);
        if (g->help_font_loaded) UnloadFont(g->help_font);
        render_close(g->rctx);
        ged_close(g->es, file);
        free(g);
        g = NULL;
    }
    if (g == NULL) {
        g = calloc(1, sizeof *g);
        g->structsize = sizeof *g;
        g->rctx = render_open(data_dir);
        if (g->rctx == NULL) { free(g); return NULL; }
        g->fixed_scale = scale;
        g->scale = scale > 0 ? scale : DEFAULT_SCALE;
        SetConfigFlags(FLAG_WINDOW_RESIZABLE);
        InitWindow(window_w(g), window_h(g), "Spidey Editor — GUI");
        SetTargetFPS(60);
        SetExitKey(KEY_NULL);   // ESC is "cancel" in the editor; quit with q or the window button
        g->es = ged_open(file, NULL);
    } else {
        g->es = ged_open(file, g->es);   // re-adopt (or reset) the editor state after a reload
    }
    render_show_player(g->rctx, false);  // he is drawn at the cursor instead
    g->drawn_room = -1;
    g->room_dirty = true;
    g->hover_room = -1;
    load_fonts(g);
    world_build(g, ged_rooms(), buf);
    if (!g->spidey_loaded) {
        static uint8_t sbuf[SPIDS_FRAME_W * SPIDS_FRAME_H * 4];
        render_spids_rgba(g->rctx, render_player_frame(), sbuf);
        Image img = { .data = sbuf, .width = SPIDS_FRAME_W, .height = SPIDS_FRAME_H,
                      .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        g->spidey = LoadTextureFromImage(img);
        SetTextureFilter(g->spidey, TEXTURE_FILTER_POINT);
        g->spidey_loaded = true;
    }

    while (!WindowShouldClose() && !ged_quit_requested()) {
        float dt = GetFrameTime();
        g->src_check -= dt;                                // throttle the hot-reload stat()s
        if (g->src_check <= 0) {
            g->src_check = 0.5f;
            if (sources_changed(last_build)) {
                fprintf(stderr, "gui: source changed, rebuilding\n");
                ged_flush();                               // don't lose painted tiles
                if (system(GUI_BUILD_CMD) == 0) return g;  // host reloads
                fprintf(stderr, "gui: rebuild failed, not reloading\n");
                last_build = GetFileModTime(library);
            }
        }

        // the window manager decides the window size; fit the picture to it
        if (g->fixed_scale <= 0) {
            int want = fit_scale(GetScreenWidth(), GetScreenHeight());
            if (want != g->scale) { g->scale = want; load_fonts(g); }
        }

        if (feed_keys(g)) g->room_dirty = true;
        if (feed_mouse(g)) g->room_dirty = true;

        if (g->paint_dirty) {                              // debounced write of paint strokes
            g->save_timer -= dt;
            if (g->save_timer <= 0 && !g->painting) {
                if (ged_flush()) g->saved_flash = 1.2f;
                g->paint_dirty = false;
            }
        }

        ged_redraw();                                      // the editor lays out its text screen

        RoomFile *rooms = ged_rooms();
        int room = ged_room();
        if (g->room_dirty || g->drawn_room != room) {
            render_room_rgb(g->rctx, rooms, room, buf);
            Image img = { .data = buf, .width = ROOM_PX_W, .height = ROOM_PX_H,
                          .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8 };
            if (!g->tex_loaded) {
                g->tex = LoadTextureFromImage(img);
                SetTextureFilter(g->tex, TEXTURE_FILTER_POINT);
                g->tex_loaded = true;
            } else {
                UpdateTexture(g->tex, buf);
            }
            if (g->drawn_room != room && g->drawn_room >= 0)          // the room we left may have changed
                world_update_room(g, rooms, g->drawn_room, buf);
            world_update_room(g, rooms, room, buf);
            g->drawn_room = room;
            g->room_dirty = false;
        }

        int toff = rooms->rooms[room].data.tile_offset;   // rebuild palette on tileset change
        if (!g->pal_loaded || g->pal_offset != toff) {
            static uint8_t pbuf[TILESET_PX * TILESET_PX * 3];
            render_tileset_rgb(g->rctx, toff, pbuf);
            Image pimg = { .data = pbuf, .width = TILESET_PX, .height = TILESET_PX,
                           .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8 };
            if (!g->pal_loaded) {
                g->pal_tex = LoadTextureFromImage(pimg);
                SetTextureFilter(g->pal_tex, TEXTURE_FILTER_POINT);
                g->pal_loaded = true;
            } else UpdateTexture(g->pal_tex, pbuf);
            g->pal_offset = toff;
        }

        if (g->saved_flash > 0) g->saved_flash -= dt;
        BeginDrawing();
        ClearBackground(BLACK);
        draw_text_screen(g);
        if (!g->text_map && !ged_map_is_text()) {   // room-list states show the editor's text instead
            DrawTexturePro(g->tex, (Rectangle){0, 0, ROOM_PX_W, ROOM_PX_H},
                           (Rectangle){pic_x(g), pic_y(g), pic_w(g), pic_h(g)},
                           (Vector2){0, 0}, 0, WHITE);
            // clip to the picture: out-of-bounds switches (the editor keeps
            // them at y >= 22) would otherwise draw over the text panels
            BeginScissorMode(pic_x(g), pic_y(g), pic_w(g), pic_h(g));
            draw_overlay(g);
            draw_hover_preview(g);
            draw_drag(g);
            draw_link_drag(g);
            draw_cursor(g);
            EndScissorMode();
        }
        draw_palette(g);
        draw_world_map(g);
        if (ged_help_shown()) draw_help(g);
        EndDrawing();
    }

    ged_close(g->es, file);          // writes pending paints, hands the rooms back to main()
    if (g->tex_loaded) UnloadTexture(g->tex);
    if (g->pal_loaded) UnloadTexture(g->pal_tex);
    if (g->spidey_loaded) UnloadTexture(g->spidey);
    if (g->atlas_loaded) UnloadTexture(g->atlas);
    if (g->font_loaded) UnloadFont(g->font);
    if (g->help_font_loaded) UnloadFont(g->help_font);
    render_close(g->rctx);
    free(g);
    CloseWindow();
    return NULL;
}
