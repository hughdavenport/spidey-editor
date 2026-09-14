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
// the picture (click a tile, Shift+drag in the room paints it, middle-click a
// room tile picks it), with a pixel editor for the selected tile beside it;
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
#define PALETTE_LINES 4         // text lines under the palette before the world map
#define SWATCH_ROWS 2
enum { FOCUS_ROOM, FOCUS_PICKER, FOCUS_EDITOR, FOCUS_COLOURS, FOCUS_BUTTONS, FOCUS_WORLD, FOCUS_COUNT };
#define ATTR_SOLID 0x40
static const struct { uint8_t bits; const char *name; } ATTRS[4] = {
    {0x00, "pass"}, {0x40, "solid"}, {0x80, "magnet"}, {0xc0, "slip"},
};
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
    bool cga;               // editing the .CGA graphics (F3) rather than the .EGA ones
    const char *data_dir;   // where the graphics live (main()'s argument, for reopening)
    bool world_stale;       // rebuild the world-map atlas before the next frame
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
    uint8_t sel_attr;       // attribute bits painted with it (0x00 pass, 0x40 solid, 0x80 magnetic, 0xc0 slippery)
    bool block_painting;    // stroke in the block editor
    bool painting;          // paint stroke in progress (Shift+left / middle drag)
    int paint_x, paint_y;
    bool paint_dirty;       // painted cells not yet written to disk
    float save_timer;       // seconds until the pending paint write
    float saved_flash;      // seconds left to show "saved"
    float err_flash;        // seconds left to show err
    char err[96];           // a refused write, shown under the picker in red
    float src_check;        // seconds until the next hot-reload source check
    bool pix_op;            // this frame's mouse event went to the pixel editor (for `u` routing)
    int focus;              // which panel arrows / hjkl move: the room, the picker, the pixel editor, its colours, its buttons, or the world map
    int btn_sel;            // FOCUS_BUTTONS: the reset / tiles / sprites / backs / block button the keyboard is on
    int btn_row;            // FOCUS_BUTTONS: 0 = those, 1 = the pass / solid / magnet / slip row (tiles view)
    int block_hidden;       // block object whose panel was closed with "gfx" (-1 none)
    int ecx, ecy;           // keyboard cursor in the pixel / block editor grid
    bool eyedrop;           // next click anywhere picks that colour
    bool fill_armed;        // next click in the pixel editor flood-fills that region with sel_colour
    int world_sel;          // world map: room the keyboard selection is on
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
    // pixel editor: one image of one graphics source. The picker has three
    // views: tiles, "sprites" = SPIDS1 stacked over SPIDS2 (sel_sprite 0..127),
    // or "backs" = the SCENERY tiles, painted into the room's background block;
    // pix_src / sel_img are derived from the view and kept as the effective source.
    GfxSource pix_src;
    bool sprites_view;
    bool backs_view;
    bool backs_painting;        // background stroke in progress (Shift+drag in the backs view)
    bool backs_dirty;           // edited BACKS.SPL not yet written
    int sel_sprite;
    int sel_img[GFX_SOURCES];   // selected image per source (tiles: index within the current tileset)
    int sel_colour;             // colour to paint with
    bool pix_painting;          // pixel stroke in progress
    bool gfx_dirty[GFX_SOURCES];    // edited file not yet written
    float gfx_timer;
    Texture2D sheet_tex[GFX_SOURCES];   // sprite sheets as 8-across pickers (tiles use pal_tex)
    bool sheet_loaded[GFX_SOURCES];
    bool sheet_stale[GFX_SOURCES];
    struct { int src; int idx; uint8_t data[BACKS_TILES]; } pix_undo[64];   // image (GfxSource) or background block (src UNDO_BACKS) before each stroke / reset
    int pix_undo_n;
    unsigned seq, pix_seq, key_seq;   // ordering of pixel strokes vs editor keys, for `u`
    Font panel_font;        // text under the picture: a touch smaller so PANEL_COLS fit its width
    bool panel_font_loaded;
    int panel_font_cw;
    int panel_font_px;
} GuiState;
#define PANEL_COLS 84           // columns of panel text that fit across the picture
#define UNDO_BACKS (-1)         // pix_undo.src: a BACKS block, idx = block id
enum { VIEW_TILES, VIEW_SPRITES, VIEW_BACKS };
#define BACKS_COLS 16           // the scenery picker: 16 x 16 tiles in the picker square
#define MOD_LATCH_FRAMES 3

static const char *FONT_CANDIDATES[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};

/* --------------------------------------------------------------- geometry */

static int  panel_top(GuiState *g);
static int  pinned_top(GuiState *g);
static int  pch(GuiState *g);
static int  pcw(GuiState *g);
static int  oob_rows(int *r0);
static int  world_room_at(GuiState *g, Vector2 m);
static void link_run(int x0, int y0, int x1, int y1, int *rx, int *ry, int *rw, int *rh);
static Rectangle pixed_button(GuiState *g, int i);
static Rectangle attr_button(GuiState *g, int i);
static Rectangle pick_button(GuiState *g);
static Rectangle fill_button(GuiState *g);
static int  pixed_button_count(GuiState *g);
static void pixed_button_press(GuiState *g, int i);
static void pix_fill(GuiState *g, int idx, int x, int y);
static bool pixed_locked(GuiState *g);
static void focus_move(GuiState *g, int dx, int dy);
static void focus_apply(GuiState *g);
static bool block_panel(GuiState *g, int *bi, int *bx, int *by, int *bw, int *bh);
static void sync_sprite_sel(GuiState *g);
static void set_view(GuiState *g, int view);
static int  cur_backs(void);
static void backs_touched(GuiState *g);
static void backs_undo_push(GuiState *g, int bid);

// Which creature a sprite frame belongs to (frame ranges verified against the
// game), and the editor object that places it (SHARK's damage picks Mysterio
// / Mary Jane). Spider-Man's own frames place nothing.
static const struct { GfxSource src; int lo, hi; const char *name; int type, dmg; } CREATURES[] = {
    { GFX_SPIDS1,  0, 31, "Spider-Man",   -1, 0 },
    { GFX_SPIDS1, 32, 37, "mummy",        MUMMY, 1 },
    { GFX_SPIDS1, 38, 42, "blue man",     BLUE_MAN, 1 },
    { GFX_SPIDS1, 43, 45, "shark",        SHARK, 1 },
    { GFX_SPIDS1, 46, 49, "wolf",         WOLF, 1 },
    { GFX_SPIDS1, 61, 61, "wolf",         WOLF, 1 },       // one more wolf frame after the rat's
    { GFX_SPIDS1, 50, 52, "R2D2",         R2D2, 1 },
    { GFX_SPIDS1, 53, 57, "dinosaur",     DINOSAUR, 1 },
    { GFX_SPIDS1, 58, 60, "rat",          RAT, 1 },
    { GFX_SPIDS1, 62, 63, "Mary Jane",    SHARK, 4 },
    { GFX_SPIDS2,  0,  5, "shotgun lady", SHOTGUN_LADY, 1 },
    { GFX_SPIDS2,  6, 10, "Mysterio",     SHARK, 3 },
    { GFX_SPIDS2, 32, 63, "Spider-Man (rotated)", -1, 0 },
};
static int creature_of(GfxSource src, int frame) {
    for (size_t i = 0; i < sizeof CREATURES / sizeof CREATURES[0]; i++)
        if (CREATURES[i].src == src && frame >= CREATURES[i].lo && frame <= CREATURES[i].hi) return (int)i;
    return -1;
}
static int block_zoom(GuiState *g, int w, int h);
static Rectangle block_button(GuiState *g, int i, int h, int z);
static void pix_undo_push(GuiState *g, GfxSource src, int idx);
static bool pix_undo_pop(GuiState *g);
static bool gfx_save_all(GuiState *g);
static void gfx_touched(GuiState *g, GfxSource src);
static void spidey_build(GuiState *g);
static void set_gfx_mode(GuiState *g, bool cga);
static int  colour_under_mouse(GuiState *g);

static int cw(GuiState *g) { return 4 * g->scale; }        // text cell width
static int ch(GuiState *g) { return 8 * g->scale; }        // text cell height (= one tile)
static int pic_w(GuiState *g) { return ROOM_PX_W * g->scale; }
static int pic_h(GuiState *g) { return ROOM_PX_H * g->scale; }
static int pic_x(GuiState *g) { return GED_MAP_X * cw(g); }
static int pic_y(GuiState *g) { return GED_MAP_Y * ch(g); }
static bool in_map_band(int y) { return y >= GED_MAP_Y && y < GED_MAP_Y + HEIGHT_TILES; }
static int palette_x(GuiState *g) { return pic_x(g) + pic_w(g) + PAD; }
static int palette_y(GuiState *g) { return pic_y(g); }
// pixel editor: the selected image zoomed to about the picker's height, right
// of the picker; colour swatches, labels and buttons under it
static int pixed_x(GuiState *g) { return palette_x(g) + TILESET_PX * g->scale + 2 * PAD; }
static void pixed_dims(GuiState *g, int *w, int *h) { render_gfx_size(g->pix_src, w, h); }
static int pixed_px(GuiState *g) { int w, h; pixed_dims(g, &w, &h); int p = TILESET_PX * g->scale / h; return p < 2 ? 2 : p; }
static int pixed_w(GuiState *g) { int w, h; pixed_dims(g, &w, &h); return w * pixed_px(g); }
static int pixed_h(GuiState *g) { int w, h; pixed_dims(g, &w, &h); return h * pixed_px(g); }
static int swatch_px(GuiState *g) { return pixed_w(g) / 8; }
static int swatch_y(GuiState *g) { return palette_y(g) + pixed_h(g) + PAD; }
static int pixed_text_y(GuiState *g) { return swatch_y(g) + SWATCH_ROWS * swatch_px(g) + PAD; }
static int pixed_buttons_y(GuiState *g) { return pixed_text_y(g) + 5 * ch(g) + 2; }   // 3 status lines + 2 creature lines
static int pixed_bottom(GuiState *g) { return pixed_buttons_y(g) + 2 * ch(g) + 16; }
// the picker's labels (and the world map) start below picker and editor alike
static int labels_y(GuiState *g) {
    int a = palette_y(g) + TILESET_PX * g->scale, b = pixed_bottom(g);
    return (a > b ? a : b) + PAD;
}
// the image being edited: tiles are picked within the current tileset
static int pixed_index(GuiState *g) {
    return g->pix_src == GFX_TILES ? g->sel_tile + g->pal_offset * 16 : g->sel_img[g->pix_src];
}
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

// Text cell under the mouse. Header and map band are plain cells; below the
// picture the rows are the scrolled panel (out-of-bounds list skipped) and
// then the pinned out-of-bounds list. The space right of the picture is the
// palette / world map, not cells.
static bool mouse_cell(GuiState *g, int *col, int *row) {
    Vector2 m = GetMousePosition();
    if (m.x < 0 || m.y < 0) return false;
    int px = (int)m.x, py = (int)m.y;
    if (py < panel_top(g) || ged_map_is_text()) {
        *col = px / cw(g);
        *row = py / ch(g);
        if (in_map_band(*row) && *col >= GED_MAP_X + MAP_COLS) return false;
        return *col < GED_COLS && *row < GED_ROWS;
    }
    if (px >= pic_w(g)) return false;
    *col = px / pcw(g);
    int r0, n = oob_rows(&r0);
    if (n && py >= pinned_top(g) && py < pinned_top(g) + n * pch(g)) {
        *row = r0 + (py - pinned_top(g)) / pch(g);
        return true;
    }
    if (py >= pinned_top(g)) return false;
    int want = (py - panel_top(g)) / pch(g) + g->scroll, v = 0;
    for (int y = GED_MAP_Y + HEIGHT_TILES; y < GED_ROWS - 1; y++) {
        if (n && y >= r0 && y < r0 + n) continue;
        if (v++ == want) { *row = y; return true; }
    }
    return false;
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

// Modifier bits from the keys currently held.
static uint8_t mods(void) {
    uint8_t m = 0;
    if (IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT))   m |= GK_MOD_SHIFT;
    if (IsKeyDown(KEY_LEFT_ALT)     || IsKeyDown(KEY_RIGHT_ALT))     m |= GK_MOD_ALT;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) m |= GK_MOD_CTRL;
    return m;
}

// Keep pix_src / sel_img in step with the sprites view's selection.
static void sync_sprite_sel(GuiState *g) {
    g->sel_sprite = ((g->sel_sprite % 128) + 128) % 128;
    g->pix_src = g->sel_sprite < 64 ? GFX_SPIDS1 : GFX_SPIDS2;
    g->sel_img[g->pix_src] = g->sel_sprite % 64;
}

// Switch the picker between tiles / sprites / backgrounds.
static void set_view(GuiState *g, int view) {
    g->sprites_view = view == VIEW_SPRITES;
    g->backs_view = view == VIEW_BACKS;
    if (view == VIEW_TILES) g->pix_src = GFX_TILES;
    else if (view == VIEW_BACKS) g->pix_src = GFX_SCENERY;
    else sync_sprite_sel(g);
}

// The background block the current room shows.
static int cur_backs(void) { return ged_rooms()->rooms[ged_room()].data.background % BACKS_BLOCKS; }

// Is the block panel (rather than the pixel editor) showing right now?
static bool block_panel(GuiState *g, int *bi, int *bx, int *by, int *bw, int *bh) {
    return ged_block_at_cursor(bi, bx, by, bw, bh) && *bi != g->block_hidden;
}

// Arrow / hjkl movement for the panel that has keyboard focus.
static void focus_move(GuiState *g, int dx, int dy) {
    if (g->focus == FOCUS_PICKER) {
        if (g->backs_view) g->sel_img[GFX_SCENERY] = (g->sel_img[GFX_SCENERY] + dx + BACKS_COLS * dy + 256) % 256;
        else if (!g->sprites_view) g->sel_tile = (uint8_t)((g->sel_tile + dx + 8 * dy + 64) % 64);
        else { g->sel_sprite += dx + 8 * dy; sync_sprite_sel(g); }
    } else if (g->focus == FOCUS_WORLD) {
        g->world_sel = ((g->world_sel + dx + 8 * dy) % 64 + 64) % 64;
    } else if (g->focus == FOCUS_COLOURS) {           // the swatches (8 x 2, or 4 in CGA): moving selects
        int n = render_colours(g->rctx);
        g->sel_colour = ((g->sel_colour + dx + (n > 8 ? 8 : 0) * dy) % n + n) % n;
    } else if (g->focus == FOCUS_BUTTONS) {           // two rows of buttons: left / right along a row, up / down between them
        bool attrs = !g->sprites_view && !g->backs_view;  // the attribute row exists in the tiles view only
        if (dy && attrs) g->btn_row = !g->btn_row;
        if (!attrs) g->btn_row = 0;
        int n = g->btn_row ? 4 : pixed_button_count(g);
        g->btn_sel = ((g->btn_sel + dx) % n + n) % n;
    } else if (g->focus == FOCUS_EDITOR) {
        int bi, bx, by, w, h;
        if (!block_panel(g, &bi, &bx, &by, &w, &h)) pixed_dims(g, &w, &h);
        g->ecx = ((g->ecx + dx) % w + w) % w;
        g->ecy = ((g->ecy + dy) % h + h) % h;
    }
}

// Enter / Space on the focused panel: world map = go to the selected room;
// picker = put the selected tile (or the selected frame's creature) at the
// room cursor. Focus stays on the panel.
static void focus_apply(GuiState *g) {
    ged_new_input_batch();
    if (g->focus == FOCUS_WORLD) {
        if (ged_map_click(g->world_sel, false)) g->focus = FOCUS_ROOM;   // answered a room prompt: back to the editor for the next one
    } else if (g->focus == FOCUS_BUTTONS) {
        if (g->btn_row && !g->sprites_view && !g->backs_view) g->sel_attr = ATTRS[g->btn_sel % 4].bits;
        else pixed_button_press(g, g->btn_sel % pixed_button_count(g));
    } else if (g->focus == FOCUS_EDITOR) {         // paint at the editor cursor
        int bi, bx, by, bw, bh;
        if (block_panel(g, &bi, &bx, &by, &bw, &bh)) {
            if (ged_block_tile(bi, g->ecx, g->ecy) != (g->sel_tile | g->sel_attr)) {
                ged_paint_begin();
                ged_block_set_tile(bi, g->ecx, g->ecy, (uint8_t)(g->sel_tile | g->sel_attr));
                g->paint_dirty = true; g->save_timer = 0.4f;
            }
        } else if (!pixed_locked(g)) {
            int idx = pixed_index(g);
            if (g->fill_armed) { pix_fill(g, idx, g->ecx, g->ecy); g->pix_op = true; }
            else if (render_gfx_pixel(g->rctx, g->pix_src, idx, g->ecx, g->ecy) != g->sel_colour) {
                pix_undo_push(g, g->pix_src, idx);
                render_gfx_set_pixel(g->rctx, g->pix_src, idx, g->ecx, g->ecy, g->sel_colour);
                gfx_touched(g, g->pix_src);
                g->pix_op = true;
            }
        }
    } else if (g->focus == FOCUS_PICKER) {
        int cx, cy;
        ged_cursor(&cx, &cy);
        if (g->backs_view) {
            int bid = cur_backs();
            if (render_backs_tile(g->rctx, bid, cx, cy) != g->sel_img[GFX_SCENERY]) {
                backs_undo_push(g, bid);
                render_backs_set_tile(g->rctx, bid, cx, cy, g->sel_img[GFX_SCENERY]);
                backs_touched(g);
                g->pix_op = true;
            }
        } else if (g->pix_src == GFX_TILES) {
            ged_paint_begin();
            ged_paint(cx, cy, (uint8_t)(g->sel_tile | g->sel_attr));
            ged_flush();
        } else {
            int c = creature_of(g->pix_src, g->sel_img[g->pix_src]);
            if (c >= 0 && CREATURES[c].type >= 0) ged_place_sprite(CREATURES[c].type, CREATURES[c].dmg);
        }
    }
    g->room_dirty = true;
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
        if (g->focus != FOCUS_ROOM && (c == 'h' || c == 'j' || c == 'k' || c == 'l')) {
            focus_move(g, c == 'h' ? -1 : c == 'l' ? 1 : 0, c == 'k' ? -1 : c == 'j' ? 1 : 0);
            any = true;
            continue;
        }
        if (g->focus != FOCUS_ROOM && c == ' ') { focus_apply(g); any = true; continue; }
        if (g->focus == FOCUS_EDITOR && c == 'f') {        // pixel editor focused: toggle fill mode
            int bi, bx, by, bw, bh;
            if (!block_panel(g, &bi, &bx, &by, &bw, &bh)) { g->fill_armed = !g->fill_armed; g->eyedrop = false; any = true; continue; }
        }
        // `u` undoes the last pixel stroke when that was the most recent edit
        if (c == 'u' && g->pix_undo_n && g->pix_seq > g->key_seq && ged_linking() == 0) { pix_undo_pop(g); any = true; continue; }
        g->key_seq = ++g->seq;
        g->focus = FOCUS_ROOM;          // a key for the editor: the room is what cursors move again
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
        if (rk == KEY_F3) { set_gfx_mode(g, !g->cga); any = true; continue; }   // GUI-only: EGA / CGA files
        if (rk == KEY_TAB && !(m & (GK_MOD_CTRL | GK_MOD_ALT))) {      // GUI-only: cycle keyboard focus
            int step = (m & GK_MOD_SHIFT) ? FOCUS_COUNT - 1 : 1, bi, bx, by, bw, bh;
            g->focus = (g->focus + step) % FOCUS_COUNT;
            while ((g->focus == FOCUS_COLOURS || g->focus == FOCUS_BUTTONS) && block_panel(g, &bi, &bx, &by, &bw, &bh))   // hidden while the block panel is up
                g->focus = (g->focus + step) % FOCUS_COUNT;
            any = true; continue;
        }
        if (g->focus != FOCUS_ROOM && !(m & (GK_MOD_CTRL | GK_MOD_ALT))) {   // the focused panel takes these
            if (rk == KEY_LEFT || rk == KEY_RIGHT || rk == KEY_UP || rk == KEY_DOWN) {
                focus_move(g, rk == KEY_LEFT ? -1 : rk == KEY_RIGHT ? 1 : 0, rk == KEY_UP ? -1 : rk == KEY_DOWN ? 1 : 0);
                any = true; continue;
            }
            if (rk == KEY_ESCAPE) g->focus = FOCUS_ROOM;   // and the editor gets it too
            if (rk == KEY_ENTER || rk == KEY_KP_ENTER) { focus_apply(g); any = true; continue; }
        }
        if (rk >= KEY_A && rk <= KEY_Z) {
            if (!(m & GK_MOD_CTRL)) continue;                          // plain letters arrived as chars
            k.code = GK_CTRL; k.ch = (char)('A' + rk - KEY_A); k.mod = (uint8_t)(m & GK_MOD_ALT);
            if (k.ch == 'P') gfx_save_all(g);                          // play: the game must see the edits
            g->key_seq = ++g->seq;
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
        if (ok) { g->focus = FOCUS_ROOM; SEND(k); }
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
    int col = 0, row = 0, tx, ty;
    bool in_text = mouse_cell(g, &col, &row);
    uint8_t mod = mods();
    bool paint_btn = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && (mod & GK_MOD_SHIFT);

    // eyedropper: the click after "pick" reads the colour under the mouse, anywhere
    if (g->eyedrop && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int c = colour_under_mouse(g);
        if (c >= 0) g->sel_colour = c;
        g->eyedrop = false;
        g->pix_op = true;
        return true;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, pick_button(g)) && !ged_block_at_cursor(&tx, &ty, &tx, &ty, &tx)) {
        g->eyedrop = !g->eyedrop; g->fill_armed = false;
        g->pix_op = true;
        return true;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, fill_button(g)) && !ged_block_at_cursor(&tx, &ty, &tx, &ty, &tx)) {
        g->fill_armed = !g->fill_armed; g->eyedrop = false;
        g->pix_op = true;
        return true;
    }

    // middle click on the room: pick that tile into the palette (reverse
    // picker); in the backs view it picks the background tile there
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) && mouse_tile(g, &tx, &ty)) {
        if (g->backs_view) { g->sel_img[GFX_SCENERY] = render_backs_tile(g->rctx, cur_backs(), tx, ty); return true; }
        g->sel_tile = (uint8_t)(ged_tile_at(tx, ty) & 0x3f);
        g->sel_attr = (uint8_t)(ged_tile_at(tx, ty) & 0xc0);
        set_view(g, VIEW_TILES);
        return true;
    }

    // backs view: Shift+drag paints scenery tiles into the room's background
    // block (shared by every room with that background); one undo entry per stroke
    if (paint_btn && g->backs_view && mouse_tile(g, &tx, &ty)) {
        backs_undo_push(g, cur_backs());
        g->backs_painting = true;
        g->paint_x = -1; g->paint_y = -1;
        g->pix_op = true;
    }
    if (g->backs_painting) {
        g->pix_op = true;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { g->backs_painting = false; return false; }
        if (mouse_tile(g, &tx, &ty) && (tx != g->paint_x || ty != g->paint_y)) {
            g->paint_x = tx; g->paint_y = ty;
            if (render_backs_tile(g->rctx, cur_backs(), tx, ty) != g->sel_img[GFX_SCENERY]) {
                render_backs_set_tile(g->rctx, cur_backs(), tx, ty, g->sel_img[GFX_SCENERY]);
                backs_touched(g);
                return true;
            }
        }
        return false;
    }

    if (paint_btn && g->sprites_view && mouse_tile(g, &tx, &ty)) {   // a sprite sheet is up: place its creature
        int c = creature_of(g->pix_src, g->sel_img[g->pix_src]);
        if (c >= 0 && CREATURES[c].type >= 0) {
            ged_new_input_batch();
            ged_set_cursor(tx, ty);
            ged_place_sprite(CREATURES[c].type, CREATURES[c].dmg);
        }
        return true;
    }
    if (paint_btn && mouse_tile(g, &tx, &ty)) {
        ged_paint_begin();                        // one undo entry per stroke
        g->painting = true;
        g->paint_x = tx; g->paint_y = ty;
        ged_paint(tx, ty, (uint8_t)(g->sel_tile | g->sel_attr));
        g->paint_dirty = true; g->save_timer = 0.4f;
        return true;
    }
    if (g->painting) {
        bool held = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        if (!held) { g->painting = false; }
        else if (mouse_tile(g, &tx, &ty) && (tx != g->paint_x || ty != g->paint_y)) {
            g->paint_x = tx; g->paint_y = ty;
            ged_paint(tx, ty, (uint8_t)(g->sel_tile | g->sel_attr));
            g->paint_dirty = true; g->save_timer = 0.4f;
            any = true;
        }
        return any;
    }

    // picker: pick a tile, a scenery tile, or a sprite frame
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int side = TILESET_PX * g->scale, px0 = palette_x(g), py0 = palette_y(g);
        if (g->backs_view) {
            int c = side / BACKS_COLS;
            if (m.x >= px0 && m.x < px0 + side && m.y >= py0 && m.y < py0 + side) {
                g->sel_img[GFX_SCENERY] = ((int)(m.y - py0) / c) * BACKS_COLS + (int)(m.x - px0) / c;
                g->focus = FOCUS_PICKER;
                return true;
            }
        } else if (!g->sprites_view) {
            int s = 8 * g->scale;
            if (m.x >= px0 && m.x < px0 + 8 * s && m.y >= py0 && m.y < py0 + 8 * s) {
                g->sel_tile = (uint8_t)(((int)(m.y - py0) / s) * 8 + (int)(m.x - px0) / s);
                g->focus = FOCUS_PICKER;
                return true;
            }
        } else {
            int w, h;
            render_gfx_size(GFX_SPIDS1, &w, &h);
            float cellw = side / 8.0f, cellh = cellw * h / w;
            if (m.x >= px0 && m.x < px0 + side && m.y >= py0 && m.y < py0 + cellh * 16) {
                g->sel_sprite = (int)((m.y - py0) / cellh) * 8 + (int)((m.x - px0) / cellw);
                sync_sprite_sel(g);
                g->focus = FOCUS_PICKER;
                return true;
            }
        }
    }

    // wheel over the picture or the palette: background up/down; horizontal (or Shift+wheel): tileset
    Vector2 wv = GetMouseWheelMoveV();
    bool over_pic = m.x >= pic_x(g) && m.x < pic_x(g) + pic_w(g) && m.y >= pic_y(g) && m.y < panel_top(g);
    bool over_pal = m.x >= palette_x(g) && m.x < palette_x(g) + TILESET_PX * g->scale &&
                    m.y >= palette_y(g) && m.y < palette_y(g) + TILESET_PX * g->scale;
    if ((wv.x != 0 || wv.y != 0) && (over_pic || over_pal)) {
        int dt = wv.x > 0 ? 1 : wv.x < 0 ? -1 : 0, db = wv.y > 0 ? 1 : wv.y < 0 ? -1 : 0;
        if (mod & GK_MOD_SHIFT) { dt = db; db = 0; }
        ged_new_input_batch();
        ged_adjust_room(db, dt);
        return true;
    }
    // wheel over the text under the picture scrolls it
    float wheel = wv.y;
    if (wheel != 0 && m.x < pic_w(g) && m.y >= panel_top(g) && m.y < pinned_top(g)) {
        g->scroll -= (int)wheel * 3;
        if (g->scroll < 0) g->scroll = 0;
        return true;
    }

    // attribute buttons under the source buttons (tiles view)
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !g->sprites_view && !g->backs_view)
        for (int i = 0; i < 4; i++)
            if (CheckCollisionPointRec(m, attr_button(g, i))) { g->sel_attr = ATTRS[i].bits; return true; }

    // block editor (while the cursor is on a block object it replaces the pixel editor)
    {
        int bi, bx, by, bw, bh;
        if (ged_block_at_cursor(&bi, &bx, &by, &bw, &bh) && bi != g->block_hidden) {
            int z = block_zoom(g, bw, bh), x0 = pixed_x(g), y0 = palette_y(g);
            bool in_grid = m.x >= x0 && m.x < x0 + bw * z && m.y >= y0 && m.y < y0 + bh * z;
            if (in_grid) {
                int cx = (int)(m.x - x0) / z, cy = (int)(m.y - y0) / z;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { g->focus = FOCUS_EDITOR; g->ecx = cx; g->ecy = cy; }
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {                       // pick: the cursor stays put
                    int t = ged_block_tile(bi, cx, cy);
                    g->sel_tile = (uint8_t)(t & 0x3f); g->sel_attr = (uint8_t)(t & 0xc0); set_view(g, VIEW_TILES);
                    return true;
                }
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { g->block_painting = true; ged_paint_begin(); }
                if (g->block_painting && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                        ged_block_tile(bi, cx, cy) != (g->sel_tile | g->sel_attr)) {
                    ged_block_set_tile(bi, cx, cy, (uint8_t)(g->sel_tile | g->sel_attr));
                    g->paint_dirty = true; g->save_timer = 0.4f;
                    return true;
                }
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) g->block_painting = false;
            if (g->block_painting) return false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                static const int dxy[4][3] = { {1, 0, 1}, {1, 0, 0}, {0, 1, 1}, {0, 1, 0} };   // w+ w- h+ h-
                for (int i = 0; i < 5; i++)
                    if (CheckCollisionPointRec(m, block_button(g, i, bh, z))) {
                        if (i == 4) { g->block_hidden = bi; return true; }          // gfx: back to the pixel editor
                        ged_new_input_batch();
                        ged_block_resize(dxy[i][0], dxy[i][1], dxy[i][2]);
                        return true;
                    }
            }
            if (in_grid) return false;
        }
    }

    // pixel editor: grid, swatches, buttons
    {
        int px = pixed_px(g), x0 = pixed_x(g), y0 = palette_y(g), sp = swatch_px(g), sy = swatch_y(g), w, h;
        pixed_dims(g, &w, &h);
        bool in_grid = m.x >= x0 && m.x < x0 + w * px && m.y >= y0 && m.y < y0 + h * px;
        bool in_sw = m.x >= x0 && m.x < x0 + 8 * sp && m.y >= sy && m.y < sy + SWATCH_ROWS * sp;
        if (in_sw && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int c = ((int)(m.y - sy) / sp) * 8 + (int)(m.x - x0) / sp;
            if (c < render_colours(g->rctx)) g->sel_colour = c;
            g->pix_op = true;
            return true;
        }
        int idx = pixed_index(g);
        if (in_grid) {
            g->pix_op = true;
            int tx = (int)(m.x - x0) / px, ty = (int)(m.y - y0) / px;
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { g->sel_colour = render_gfx_pixel(g->rctx, g->pix_src, idx, tx, ty); return true; }   // pick: the cursor stays put
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { g->focus = FOCUS_EDITOR; g->ecx = tx; g->ecy = ty; }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && g->fill_armed) {   // fill stays on: click "fill" again to leave it
                if (!pixed_locked(g)) pix_fill(g, idx, tx, ty);
                return true;
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !pixed_locked(g)) { g->pix_painting = true; pix_undo_push(g, g->pix_src, idx); }
            if (g->pix_painting && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                    render_gfx_pixel(g->rctx, g->pix_src, idx, tx, ty) != g->sel_colour) {
                render_gfx_set_pixel(g->rctx, g->pix_src, idx, tx, ty, g->sel_colour);
                gfx_touched(g, g->pix_src);
                return true;
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) g->pix_painting = false;
        if (g->pix_painting) return false;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            for (int i = 0; i < pixed_button_count(g); i++)
                if (CheckCollisionPointRec(m, pixed_button(g, i))) { pixed_button_press(g, i); return true; }
    }

    // world map: hover names, left = go there, right = move the thing / retarget the chunk
    g->hover_room = world_room_at(g, m);
    if (g->hover_room >= 0 && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
        ged_new_input_batch();
        bool answered = ged_map_click(g->hover_room, IsMouseButtonPressed(MOUSE_BUTTON_RIGHT));
        g->focus = answered ? FOCUS_ROOM : FOCUS_WORLD;   // a neighbour / goto answer: the editor keeps the keys
        g->world_sel = g->hover_room;
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
        g->focus = FOCUS_ROOM;
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
        if (drag && ged_room() == room) { ged_set_cursor(cx, cy); ged_clear_selection(); }   // (a drag into a neighbour room follows the item)
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
static int pcw(GuiState *g) { int w = pic_w(g) / PANEL_COLS; return w < 4 ? 4 : w; }   // panel cell size
static int pch(GuiState *g) { return 2 * pcw(g); }

static void ensure_panel_font(GuiState *g) {
    if (g->panel_font_loaded && g->panel_font_cw != pcw(g)) { UnloadFont(g->panel_font); g->panel_font_loaded = false; }
    if (!g->panel_font_loaded) {
        g->panel_font_loaded = load_mono(&g->panel_font, pcw(g), &g->panel_font_px);
        g->panel_font_cw = pcw(g);
    }
}

// The out-of-bounds switch list is pinned above the window's last row (the
// editor's macro overlay); the rest of the panel scrolls between the picture
// and it.
static int oob_rows(int *r0) { int r1; ged_oob_rows(r0, &r1); return r1 > *r0 ? r1 - *r0 : 0; }
static int pinned_top(GuiState *g) { int r0; return GetScreenHeight() - pch(g) - oob_rows(&r0) * pch(g); }
static int panel_rows_visible(GuiState *g) { int r = (pinned_top(g) - panel_top(g)) / pch(g); return r < 1 ? 1 : r; }

// Rows the scrolling part has (everything under the map except the pinned list).
static int panel_content_rows(const GCell *cells, int stride) {
    int r0, n = oob_rows(&r0), last = GED_MAP_Y + HEIGHT_TILES - 1, count = 0;
    for (int y = GED_MAP_Y + HEIGHT_TILES; y < GED_ROWS - 1; y++) {
        if (n && y >= r0 && y < r0 + n) continue;
        count++;
        for (int x = 0; x < GED_COLS; x++) {
            const GCell *c = &cells[y * stride + x];
            if (c->bg || (c->ch && c->ch != ' ')) { last = count; break; }
        }
    }
    return last < 0 ? 0 : last;
}

static int panel_max_scroll(GuiState *g) {
    int stride;
    const GCell *cells = ged_cells(&stride);
    int m = panel_content_rows(cells, stride) - panel_rows_visible(g);
    return m < 0 ? 0 : m;
}

// Everything the editor drew except the help box (right of the hex map; it
// can run below the map band too — draw_help shows it as an overlay) and,
// unless F2, the hex map itself. Rows under the map scroll (`scroll`) except
// the out-of-bounds switch list, pinned at the bottom with clickable S# badges;
// the editor's last row (macro overlay) is pinned below that.
static void draw_text_screen(GuiState *g) {
    int stride;
    const GCell *cells = ged_cells(&stride);
    bool help = ged_help_shown(), text = g->text_map || ged_map_is_text();
    if (ged_map_is_text()) {                    // room list: one continuous text screen, one font
        for (int y = 0; y < GED_ROWS; y++)
            for (int x = 0; x < GED_COLS; x++) {
                if (x >= GED_MAP_X + MAP_COLS && (help || in_map_band(y))) continue;
                if (y * ch(g) >= GetScreenHeight()) break;
                draw_cell(&cells[y * stride + x], x * cw(g), y * ch(g), cw(g), ch(g), &g->font, g->font_loaded, g->font_px);
            }
        return;
    }
    int first_panel = GED_MAP_Y + HEIGHT_TILES;
    int max_scroll = panel_max_scroll(g);
    if (g->scroll > max_scroll) g->scroll = max_scroll;
    ensure_panel_font(g);
    int r0, n = oob_rows(&r0), v = 0;
    BeginScissorMode(0, panel_top(g), pic_w(g), pinned_top(g) - panel_top(g));
    for (int y = first_panel; y < GED_ROWS - 1; y++) {
        if (n && y >= r0 && y < r0 + n) continue;
        int py = panel_top(g) + (v++ - g->scroll) * pch(g);
        if (py + pch(g) <= panel_top(g) || py >= pinned_top(g)) continue;
        for (int x = 0; x < GED_COLS; x++) {
            if (x >= GED_MAP_X + MAP_COLS && help) continue;
            if (ged_is_oob_marker(x, y)) continue;   // its S# badge is in the pinned list instead
            draw_cell(&cells[y * stride + x], x * pcw(g), py, pcw(g), pch(g), &g->panel_font, g->panel_font_loaded, g->panel_font_px);
        }
    }
    EndScissorMode();
    if (max_scroll > 0) {                       // scrollbar along the picture's right edge
        int top = panel_top(g), hgt = pinned_top(g) - top, x = pic_w(g) - SCROLLBAR_W;
        int total = max_scroll + panel_rows_visible(g);
        int th = hgt * panel_rows_visible(g) / total, ty = top + hgt * g->scroll / total;
        DrawRectangle(x, top, SCROLLBAR_W, hgt, (Color){40, 40, 40, 255});
        DrawRectangle(x + 1, ty, SCROLLBAR_W - 2, th < 12 ? 12 : th, (Color){170, 170, 170, 255});
    }
    if (n) {                                    // pinned out-of-bounds switch list
        int top = pinned_top(g);
        DrawLine(0, top - 1, pic_w(g), top - 1, (Color){90, 90, 90, 255});
        for (int i = 0; i < n; i++) {
            int y = r0 + i, py = top + i * pch(g), sw = ged_oob_switch_at_row(y);
            int shift = sw >= 0 ? 3 : 0;        // room for the badge
            for (int x = 0; x < GED_COLS; x++)
                draw_cell(&cells[y * stride + x], (x + shift) * pcw(g), py, pcw(g), pch(g), &g->panel_font, g->panel_font_loaded, g->panel_font_px);
            if (sw >= 0) {
                DrawRectangle(0, py, 3 * pcw(g) - 2, pch(g), (Color){60, 20, 60, 255});
                gtext(&g->panel_font, g->panel_font_loaded, TextFormat("S%d", sw), 2, py, g->panel_font_px, MAGENTA);
            }
        }
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
        draw_cell(&cells[last * stride + x], x * pcw(g), GetScreenHeight() - pch(g), pcw(g), pch(g),
                  &g->panel_font, g->panel_font_loaded, g->panel_font_px);
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
    // The editor's help box is the white-backed block: in the sidebar right
    // of the map when it fits there, else centred over the whole screen. Only
    // runs of white cells count, so the hex map's highlighted cells do not.
    int x0 = GED_COLS, x1 = -1, y0 = GED_ROWS, y1 = -1;
    for (int y = 0; y < GED_ROWS; y++) {
        int run = 0;
        for (int x = 0; x <= GED_COLS; x++) {
            bool white = x < GED_COLS && cells[y * stride + x].bg == 8;
            if (white) { run++; continue; }
            if (run >= 10) {
                if (x - run < x0) x0 = x - run;
                if (x - 1 > x1) x1 = x - 1;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
            run = 0;
        }
    }
    if (x1 < 0) return;
    int cols = x1 - x0 + 1, rows = y1 - y0 + 1;
    static const char *gui_help[] = {
        "GUI  (mouse and keys on top of the editor's)",
        "F2 hex map     F3 switch between the .EGA and .CGA graphics     q quit     wheel over text: scroll",
        "picker: click selects; Shift+drag in the room paints it (a sprite sheet: Shift+click places",
        "        that creature); middle-click on the room picks the tile",
        "backs button: the picker shows the scenery tiles; Shift+drag paints the room's background",
        "        (BACKS.SPL, shared by every room with that background number); middle-click picks",
        "wheel over room or picker: background; Shift+wheel / horizontal wheel: tileset",
        "world map: click = go there (an empty slot becomes a new room); right-click = move the thing under the cursor there",
        "switch open (Ctrl-S): right-click an object or switch links it; right-drag tiles = block chunk",
        "focus: Tab cycles room / picker / editor / colours / buttons / world map (or click one); arrows, hjkl move it;",
        "       Enter or Space applies (picker: put tile or creature at the room cursor; editor: paint); Esc or any editor",
        "       key (r, s, o, ...) = back to the room",
        "pixel editor: click paints, right-click picks, u undoes a stroke, reset restores the original,",
        "        pick = then click any colour on screen (room, picker, world map); fill = a mode: clicks on",
        "        the image (or Space at the editor cursor) flood that region with the colour; click fill again",
        "        (or f while the pixel editor has focus) to leave",
        "block under cursor: its tiles are shown beside the picker; click paints, w/h buttons resize",
        "info text: click a switch / chunk / object / room field line to edit it",
    };
    int nextra = (int)(sizeof gui_help / sizeof gui_help[0]);
    for (int i = 0; i < nextra; i++) { int l = (int)strlen(gui_help[i]) + 2; if (l > cols) cols = l; }
    int total = rows + nextra + 1;
    int hch = ch(g);
    if (total * hch > GetScreenHeight() - 2 * PAD) hch = (GetScreenHeight() - 2 * PAD) / total;
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
    int w = cols * hcw, h = total * hch;
    int ox = pic_x(g) + (pic_w(g) - w) / 2, oy = pic_y(g) + (pic_h(g) - h) / 2;
    if (ox < PAD) ox = PAD;
    if (oy < PAD) oy = PAD;
    DrawRectangle(ox - PAD, oy - PAD, w + 2 * PAD, h + 2 * PAD, (Color){0, 0, 0, 235});
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            draw_cell(&cells[y * stride + x], ox + (x - x0) * hcw, oy + (y - y0) * hch, hcw, hch, font, have, fpx);
    for (int i = 0; i < nextra; i++) {
        int yy = oy + (rows + 1 + i) * hch;
        DrawRectangle(ox, yy, w, hch, (Color){40, 40, 48, 255});
        gtext(font, have, gui_help[i], ox + hcw, yy + (hch - fpx) / 2, fpx, i == 0 ? GOLD : RAYWHITE);
    }
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

// Build the 8-across picker texture of a sprite sheet (RGBA, key transparent).
static int sheet_cols(GfxSource src) { return src == GFX_SCENERY ? BACKS_COLS : 8; }
static void sheet_build(GuiState *g, GfxSource src) {
    int w, h, n = render_gfx_count(src);
    render_gfx_size(src, &w, &h);
    int cols = sheet_cols(src), rows = (n + cols - 1) / cols;
    Image img = GenImageColor(cols * w, rows * h, BLANK);
    static uint8_t one[SPIDS_FRAME_W * SPIDS_FRAME_H * 4];
    for (int i = 0; i < n; i++) {
        render_gfx_rgba(g->rctx, src, i, one);
        Image f = { .data = one, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        ImageDraw(&img, f, (Rectangle){0, 0, w, h}, (Rectangle){(i % cols) * w, (i / cols) * h, w, h}, WHITE);
    }
    if (g->sheet_loaded[src]) UnloadTexture(g->sheet_tex[src]);
    g->sheet_tex[src] = LoadTextureFromImage(img);
    SetTextureFilter(g->sheet_tex[src], TEXTURE_FILTER_POINT);
    g->sheet_loaded[src] = true;
    g->sheet_stale[src] = false;
    UnloadImage(img);
}

// Rooms showing background `bid`, as "01 02 0a" (at most a few).
static const char *backs_users(int bid) {
    static char buf[64];
    RoomFile *rooms = ged_rooms();
    int n = 0, len = 0;
    for (int r = 0; r < 64; r++) {
        if (!rooms->rooms[r].valid || rooms->rooms[r].data.background % BACKS_BLOCKS != bid) continue;
        if (n < 8) len += snprintf(buf + len, sizeof buf - (size_t)len, "%s%02x", n ? " " : "", r);
        n++;
    }
    if (n > 8) snprintf(buf + len, sizeof buf - (size_t)len, " +%d", n - 8);
    return buf;
}

// The picker: the current tileset, the selected sprite sheet (8 across), or
// the scenery tiles (16 across).
static void draw_palette(GuiState *g) {
    int s = 8 * g->scale, x0 = palette_x(g), y0 = palette_y(g), fs = g->font_px, side = TILESET_PX * g->scale;
    if (g->backs_view) {
        int c = side / BACKS_COLS, sel = g->sel_img[GFX_SCENERY];
        DrawRectangle(x0, y0, side, side, (Color){35, 45, 35, 255});
        if (g->sheet_loaded[GFX_SCENERY])
            DrawTexturePro(g->sheet_tex[GFX_SCENERY], (Rectangle){0, 0, g->sheet_tex[GFX_SCENERY].width, g->sheet_tex[GFX_SCENERY].height},
                           (Rectangle){x0, y0, side, side}, (Vector2){0, 0}, 0, WHITE);
        for (int i = 0; i <= BACKS_COLS; i++) {
            DrawLine(x0 + i * c, y0, x0 + i * c, y0 + side, (Color){80, 80, 80, 255});
            DrawLine(x0, y0 + i * c, x0 + side, y0 + i * c, (Color){80, 80, 80, 255});
        }
        DrawRectangleLinesEx((Rectangle){x0 + (sel % BACKS_COLS) * c, y0 + (sel / BACKS_COLS) * c, c, c}, 2, GOLD);
        if (g->focus == FOCUS_PICKER) DrawRectangleLinesEx((Rectangle){x0 - 2, y0 - 2, side + 4, side + 4}, 2, GOLD);
    } else if (g->pix_src == GFX_TILES) {
        DrawTexturePro(g->pal_tex, (Rectangle){0, 0, TILESET_PX, TILESET_PX},
                       (Rectangle){x0, y0, side, side}, (Vector2){0, 0}, 0, WHITE);
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
        if (g->focus == FOCUS_PICKER) DrawRectangleLinesEx((Rectangle){x0 - 2, y0 - 2, side + 4, side + 4}, 2, GOLD);
    } else {
        int w, h;
        render_gfx_size(GFX_SPIDS1, &w, &h);
        float cellw = side / 8.0f, cellh = cellw * h / w;
        for (int k = 0; k < 2; k++) {                      // SPIDS1 on top, SPIDS2 below
            GfxSource src = k == 0 ? GFX_SPIDS1 : GFX_SPIDS2;
            int rows = (render_gfx_count(src) + 7) / 8, ty = y0 + (int)(k * 8 * cellh);
            DrawRectangle(x0, ty, side, (int)(cellh * rows), k == 0 ? (Color){35, 35, 45, 255} : (Color){45, 35, 35, 255});
            if (g->sheet_loaded[src])
                DrawTexturePro(g->sheet_tex[src], (Rectangle){0, 0, g->sheet_tex[src].width, g->sheet_tex[src].height},
                               (Rectangle){x0, ty, side, cellh * rows}, (Vector2){0, 0}, 0, WHITE);
            for (int i = 0; i <= 8; i++) DrawLine(x0 + (int)(i * cellw), ty, x0 + (int)(i * cellw), ty + (int)(cellh * rows), (Color){80, 80, 80, 255});
            for (int i = 0; i <= rows; i++) DrawLine(x0, ty + (int)(i * cellh), x0 + side, ty + (int)(i * cellh), (Color){80, 80, 80, 255});
        }
        int sel = g->sel_sprite;
        DrawRectangleLinesEx((Rectangle){x0 + (sel % 8) * cellw, y0 + (sel / 8) * cellh, cellw, cellh}, 2, GOLD);
        if (g->focus == FOCUS_PICKER) DrawRectangleLinesEx((Rectangle){x0 - 2, y0 - 2, side + 4, cellh * 16 + 4}, 2, GOLD);
    }
    int ly = labels_y(g), lh = ch(g);
    const char *lines[] = {
        g->backs_view    ? TextFormat("background %d  scenery tile 0x%02x", cur_backs(), g->sel_img[GFX_SCENERY])
        : !g->sprites_view ? TextFormat("tileset %d  selected 0x%02x", g->pal_offset, g->sel_tile | g->sel_attr)
                         : TextFormat("%s  frame 0x%02x", render_gfx_name(g->rctx, g->pix_src), g->sel_img[g->pix_src]),
        g->backs_view    ? TextFormat("shown by rooms %s", backs_users(cur_backs())) : "",
        g->cga ? "CGA graphics   Ctrl-? help (mouse and GUI keys too)" : "EGA graphics   Ctrl-? help (mouse and GUI keys too)",
    };
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++)
        gtext(&g->font, g->font_loaded, lines[i], x0, ly + (int)i * lh, fs, i == 0 ? RAYWHITE : GRAY);

    if (g->err_flash > 0)
        gtext(&g->font, g->font_loaded, g->err, x0, ly + (int)(sizeof lines / sizeof lines[0]) * lh, fs, RED);
    else if (g->saved_flash > 0)
        gtext(&g->font, g->font_loaded, "saved", x0, ly + (int)(sizeof lines / sizeof lines[0]) * lh, fs, GREEN);
}

// Buttons under the editor: reset, then one per view, then "block" while a
// block object's panel has been closed with "gfx".
static Rectangle pixed_button(GuiState *g, int i) {
    int bw = 9 * cw(g), gap = PAD;
    return (Rectangle){ pixed_x(g) + i * (bw + gap), pixed_buttons_y(g), bw, ch(g) + 4 };
}
static int pixed_button_count(GuiState *g) { return g->block_hidden >= 0 ? 5 : 4; }

static void pixed_button_press(GuiState *g, int i) {
    g->pix_op = true;
    if (i == 0) {                                   // reset: the image as shipped
        int idx = pixed_index(g);
        uint8_t orig[GFX_MAX_BYTES];
        if (render_gfx_original(g->rctx, g->pix_src, idx, orig)) {
            pix_undo_push(g, g->pix_src, idx);
            render_gfx_put(g->rctx, g->pix_src, idx, orig);
            gfx_touched(g, g->pix_src);
        }
    } else if (i <= 3) set_view(g, i == 1 ? VIEW_TILES : i == 2 ? VIEW_SPRITES : VIEW_BACKS);
    else if (g->block_hidden >= 0) g->block_hidden = -1;   // block: show its panel again
}

// Remember the image as it is now, so the coming stroke (or reset) can be undone.
static void pix_undo_push(GuiState *g, GfxSource src, int idx) {
    if (g->pix_undo_n == 64) { memmove(g->pix_undo, g->pix_undo + 1, 63 * sizeof g->pix_undo[0]); g->pix_undo_n--; }
    g->pix_undo[g->pix_undo_n].src = (int)src;
    g->pix_undo[g->pix_undo_n].idx = idx;
    if (render_gfx_get(g->rctx, src, idx, g->pix_undo[g->pix_undo_n].data)) g->pix_undo_n++;
    g->pix_seq = ++g->seq;
}

// Remember a background block before a stroke into it (same undo stack).
static void backs_undo_push(GuiState *g, int bid) {
    if (g->pix_undo_n == 64) { memmove(g->pix_undo, g->pix_undo + 1, 63 * sizeof g->pix_undo[0]); g->pix_undo_n--; }
    g->pix_undo[g->pix_undo_n].src = UNDO_BACKS;
    g->pix_undo[g->pix_undo_n].idx = bid;
    if (render_backs_get(g->rctx, bid, g->pix_undo[g->pix_undo_n].data)) g->pix_undo_n++;
    g->pix_seq = ++g->seq;
}

static void gui_error(GuiState *g, const char *msg) {
    snprintf(g->err, sizeof g->err, "%s", msg);
    g->err_flash = 4.0f;
    fprintf(stderr, "gui: %s\n", msg);
}

static void backs_touched(GuiState *g) {
    g->backs_dirty = true; g->gfx_timer = 0.6f;
    g->room_dirty = true;
}

// Switch between the .EGA and .CGA graphics files: pending edits are written,
// the other set is loaded and every picture built from them is redone.
static void set_gfx_mode(GuiState *g, bool cga) {
    if (cga == g->cga) return;
    gfx_save_all(g);
    RenderCtx *next = render_open(g->data_dir, cga);
    if (next == NULL) { gui_error(g, cga ? "no .CGA graphics in the data dir" : "no .EGA graphics in the data dir"); return; }
    render_close(g->rctx);
    g->rctx = next;
    g->cga = cga;
    render_show_player(g->rctx, false);
    g->pix_undo_n = 0; g->pix_seq = 0;                   // the undo entries belong to the other files
    g->pal_offset = -1;                                   // palette texture
    for (int i = 0; i < GFX_SOURCES; i++) g->sheet_stale[i] = true;
    spidey_build(g);
    g->world_stale = true;
    if (g->sel_colour >= render_colours(g->rctx)) g->sel_colour = render_colours(g->rctx) - 1;
    g->room_dirty = true;
}

static void gfx_touched(GuiState *g, GfxSource src) {
    g->gfx_dirty[src] = true; g->gfx_timer = 0.6f;
    if (src == GFX_TILES) g->pal_offset = -1;              // rebuild the palette texture
    else g->sheet_stale[src] = true;
    g->room_dirty = true;
}

static bool pix_undo_pop(GuiState *g) {
    if (g->pix_undo_n == 0) return false;
    g->pix_undo_n--;
    int src = g->pix_undo[g->pix_undo_n].src;
    if (src == UNDO_BACKS) {
        render_backs_put(g->rctx, g->pix_undo[g->pix_undo_n].idx, g->pix_undo[g->pix_undo_n].data);
        backs_touched(g);
    } else {
        render_gfx_put(g->rctx, (GfxSource)src, g->pix_undo[g->pix_undo_n].idx, g->pix_undo[g->pix_undo_n].data);
        gfx_touched(g, (GfxSource)src);
    }
    g->pix_seq = g->pix_undo_n ? ++g->seq : 0;
    return true;
}

// Write every edited graphics file; the Spider-Man marker is rebuilt in case
// his frame changed. true if anything was written.
static bool gfx_save_all(GuiState *g) {
    bool any = false;
    for (int i = 0; i < GFX_SOURCES; i++) {
        if (!g->gfx_dirty[i]) continue;
        g->gfx_dirty[i] = false;
        if (render_gfx_save(g->rctx, (GfxSource)i)) any = true;
        else gui_error(g, TextFormat("could not write %s", render_gfx_name(g->rctx, (GfxSource)i)));
        if (i == GFX_SPIDS1) spidey_build(g);
    }
    if (g->backs_dirty) {
        g->backs_dirty = false;
        if (render_backs_save(g->rctx)) any = true;
        else {
            // refused: the file would pass its size limit. Undo background
            // strokes, newest first, until what is left fits again.
            bool fits = false;
            while (!fits && g->pix_undo_n > 0 && g->pix_undo[g->pix_undo_n - 1].src == UNDO_BACKS) {
                pix_undo_pop(g);
                fits = render_backs_save(g->rctx);
            }
            g->backs_dirty = false;
            gui_error(g, "NOT SAVED: BACKS.SPL over its size limit, background stroke undone");
        }
    }
    return any;
}

// "pick" button right of the swatches: eyedropper mode; "fill" beside it.
static Rectangle pick_button(GuiState *g) {
    return (Rectangle){ pixed_x(g) + 8 * swatch_px(g) + PAD, swatch_y(g), 6 * cw(g), SWATCH_ROWS * swatch_px(g) };
}
static Rectangle fill_button(GuiState *g) {
    Rectangle r = pick_button(g);
    r.x += r.width + PAD;
    return r;
}

// Flood-fill the 4-connected run of pixels sharing (x,y)'s colour in image
// idx of the current source with sel_colour; one undo entry.
static void pix_fill(GuiState *g, int idx, int x, int y) {
    int w, h;
    pixed_dims(g, &w, &h);
    int from = render_gfx_pixel(g->rctx, g->pix_src, idx, x, y);
    if (from == g->sel_colour) return;
    pix_undo_push(g, g->pix_src, idx);
    int stack[SPIDS_FRAME_W * SPIDS_FRAME_H][2], n = 0;      // the largest image; each pixel is pushed at most once
    stack[n][0] = x; stack[n][1] = y; n++;
    render_gfx_set_pixel(g->rctx, g->pix_src, idx, x, y, g->sel_colour);
    while (n > 0) {
        n--;
        int px = stack[n][0], py = stack[n][1];
        static const int d[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
        for (int i = 0; i < 4; i++) {
            int nx = px + d[i][0], ny = py + d[i][1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            if (render_gfx_pixel(g->rctx, g->pix_src, idx, nx, ny) != from) continue;
            render_gfx_set_pixel(g->rctx, g->pix_src, idx, nx, ny, g->sel_colour);
            stack[n][0] = nx; stack[n][1] = ny; n++;
        }
    }
    gfx_touched(g, g->pix_src);
}

// Nearest palette colour to what is on screen under the mouse (previous frame).
static int colour_under_mouse(GuiState *g) {
    Image shot = LoadImageFromScreen();
    Vector2 m = GetMousePosition();
    int mx = (int)m.x, my = (int)m.y, best = 0;
    if (mx < 0 || my < 0 || mx >= shot.width || my >= shot.height) { UnloadImage(shot); return -1; }
    Color c = GetImageColor(shot, mx, my);
    UnloadImage(shot);
    const uint8_t (*pal)[3] = render_palette(g->rctx);
    long bestd = -1;
    for (int i = 0; i < render_colours(g->rctx); i++) {
        long dr = c.r - pal[i][0], dg = c.g - pal[i][1], db = c.b - pal[i][2], d = dr * dr + dg * dg + db * db;
        if (bestd < 0 || d < bestd) { bestd = d; best = i; }
    }
    return best;
}

// Block editor geometry: the block's tiles zoomed to about the picker's height.
static int block_zoom(GuiState *g, int w, int h) {
    (void)w; (void)h;
    return 8 * g->scale * 3 / 2;             // fixed: resizing must not rescale the tiles
}
static Rectangle block_button(GuiState *g, int i, int h, int z) {
    int bw = 5 * cw(g);
    return (Rectangle){ pixed_x(g) + i * (bw + 4), palette_y(g) + h * z + PAD, bw, ch(g) + 4 };
}

// The block object under the cursor: its tile grid, click paints the picked
// tile, right click picks; buttons grow / shrink it.
static void draw_block_editor(GuiState *g, int idx, int bx, int by, int w, int h) {
    int z = block_zoom(g, w, h), x0 = pixed_x(g), y0 = palette_y(g), fs = g->font_px;
    Rectangle src_cell = {0, 0, 8, 8};
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int t = ged_block_tile(idx, x, y) & 0x3f;
            src_cell.x = (t % 8) * 8; src_cell.y = (t / 8) * 8;
            DrawTexturePro(g->pal_tex, src_cell, (Rectangle){x0 + x * z, y0 + y * z, z, z}, (Vector2){0, 0}, 0, WHITE);
        }
    for (int i = 0; i <= w; i++) DrawLine(x0 + i * z, y0, x0 + i * z, y0 + h * z, (Color){90, 90, 90, 255});
    for (int i = 0; i <= h; i++) DrawLine(x0, y0 + i * z, x0 + w * z, y0 + i * z, (Color){90, 90, 90, 255});
    if (g->focus == FOCUS_EDITOR) {
        if (g->ecx >= w) g->ecx = w - 1;
        if (g->ecy >= h) g->ecy = h - 1;
        DrawRectangleLinesEx((Rectangle){x0 + g->ecx * z, y0 + g->ecy * z, z, z}, 2, WHITE);
        DrawRectangleLinesEx((Rectangle){x0 - 2, y0 - 2, w * z + 4, h * z + 4}, 2, GOLD);
    }
    const char *names[] = { "w+", "w-", "h+", "h-", "gfx" };
    for (int i = 0; i < 5; i++) {
        Rectangle rb = block_button(g, i, h, z);
        DrawRectangleRec(rb, (Color){50, 50, 60, 255});
        DrawRectangleLinesEx(rb, 1, GRAY);
        int tw = text_w(&g->font, g->font_loaded, names[i], fs);
        gtext(&g->font, g->font_loaded, names[i], (int)rb.x + ((int)rb.width - tw) / 2, (int)rb.y + 2, fs, RAYWHITE);
    }
    int ty = (int)block_button(g, 0, h, z).y + ch(g) + PAD;
    gtext(&g->font, g->font_loaded, TextFormat("block O%d at %d,%d  %dx%d", idx, bx, by, w, h), x0, ty, fs, RAYWHITE);
    gtext(&g->font, g->font_loaded, "click paints the picked tile,", x0, ty + ch(g), fs, GRAY);
    gtext(&g->font, g->font_loaded, "right-click picks; w/h resize; gfx = pixel editor", x0, ty + 2 * ch(g), fs, GRAY);
}

// Tile 0 of every tileset is "invisible": the game never draws it (the
// background shows through), so editing its pixels would change nothing.
static bool pixed_locked(GuiState *g) {
    return (g->pix_src == GFX_TILES && g->sel_tile == 0) || (g->pix_src == GFX_SCENERY && g->sel_img[GFX_SCENERY] == 0);
}

// Zoomed view of the selected image; left click paints sel_colour, right
// click picks a pixel's colour; the 16 palette swatches sit under it. The
// transparent colour of sprites is drawn as a checker.
static void draw_gfx_editor(GuiState *g, bool block_available) {
    int idx = pixed_index(g), px = pixed_px(g), x0 = pixed_x(g), y0 = palette_y(g), w, h;
    pixed_dims(g, &w, &h);
    int key = render_gfx_key(g->rctx, g->pix_src);
    const uint8_t (*pal)[3] = render_palette(g->rctx);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int c = render_gfx_pixel(g->rctx, g->pix_src, idx, x, y);
            Color col = c == key ? ((x + y) & 1 ? (Color){70, 70, 70, 255} : (Color){50, 50, 50, 255})
                                 : (Color){pal[c][0], pal[c][1], pal[c][2], 255};
            DrawRectangle(x0 + x * px, y0 + y * px, px, px, col);
        }
    for (int i = 0; i <= w; i++) DrawLine(x0 + i * px, y0, x0 + i * px, y0 + h * px, (Color){70, 70, 70, 255});
    for (int i = 0; i <= h; i++) DrawLine(x0, y0 + i * px, x0 + w * px, y0 + i * px, (Color){70, 70, 70, 255});
    if (g->focus == FOCUS_EDITOR) {
        if (g->ecx >= w) g->ecx = w - 1;
        if (g->ecy >= h) g->ecy = h - 1;
        DrawRectangleLinesEx((Rectangle){x0 + g->ecx * px, y0 + g->ecy * px, px, px}, 2, WHITE);
        DrawRectangleLinesEx((Rectangle){x0 - 2, y0 - 2, w * px + 4, h * px + 4}, 2, GOLD);
    }
    int sp = swatch_px(g), sy = swatch_y(g);
    for (int c = 0; c < render_colours(g->rctx); c++) {
        int sx = x0 + (c % 8) * sp, yy = sy + (c / 8) * sp;
        if (c == key) {                          // the game draws this colour as transparent in sprites
            DrawRectangle(sx, yy, sp, sp, (Color){50, 50, 50, 255});
            DrawRectangle(sx, yy, sp / 2, sp / 2, (Color){70, 70, 70, 255});
            DrawRectangle(sx + sp / 2, yy + sp / 2, sp - sp / 2, sp - sp / 2, (Color){70, 70, 70, 255});
            gtext(&g->font, g->font_loaded, "T", sx + 2, yy, g->font_px, RAYWHITE);
        } else {
            DrawRectangle(sx, yy, sp, sp, (Color){pal[c][0], pal[c][1], pal[c][2], 255});
        }
        if (c == g->sel_colour) DrawRectangleLinesEx((Rectangle){sx, yy, sp, sp}, 2, WHITE);
    }
    if (g->focus == FOCUS_COLOURS) DrawRectangleLinesEx((Rectangle){x0 - 2, sy - 2, 8 * sp + 4, SWATCH_ROWS * sp + 4}, 2, GOLD);
    int ty = pixed_text_y(g), fs = g->font_px;
    bool dirty = g->backs_dirty;
    for (int i = 0; i < GFX_SOURCES; i++) dirty |= g->gfx_dirty[i];
    gtext(&g->font, g->font_loaded,
          g->pix_src == GFX_TILES ? TextFormat("tile 0x%02x  colour %d", idx, g->sel_colour)
          : g->pix_src == GFX_SCENERY ? TextFormat("scenery tile 0x%02x  colour %d", idx, g->sel_colour)
          : g->sel_colour == key  ? TextFormat("%s frame 0x%02x  colour %d = transparent", render_gfx_name(g->rctx, g->pix_src), idx, g->sel_colour)
                                  : TextFormat("%s frame 0x%02x  colour %d", render_gfx_name(g->rctx, g->pix_src), idx, g->sel_colour),
          x0, ty, fs, RAYWHITE);
    gtext(&g->font, g->font_loaded,
          pixed_locked(g) ? "tile 0 is always blank" : "click paints, right-click picks",
          x0, ty + ch(g), fs, pixed_locked(g) ? ORANGE : GRAY);
    gtext(&g->font, g->font_loaded, dirty ? "u undoes a stroke; saving..." : "u undoes a stroke",
          x0, ty + 2 * ch(g), fs, dirty ? ORANGE : GRAY);
    {
        Rectangle rb = pick_button(g);
        DrawRectangleRec(rb, g->eyedrop ? (Color){90, 70, 20, 255} : (Color){50, 50, 60, 255});
        DrawRectangleLinesEx(rb, 1, g->eyedrop ? GOLD : GRAY);
        int tw = text_w(&g->font, g->font_loaded, "pick", fs);
        gtext(&g->font, g->font_loaded, "pick", (int)rb.x + ((int)rb.width - tw) / 2, (int)rb.y + ((int)rb.height - fs) / 2, fs, RAYWHITE);
        rb = fill_button(g);
        DrawRectangleRec(rb, g->fill_armed ? (Color){90, 70, 20, 255} : (Color){50, 50, 60, 255});
        DrawRectangleLinesEx(rb, 1, g->fill_armed ? GOLD : GRAY);
        tw = text_w(&g->font, g->font_loaded, "fill", fs);
        gtext(&g->font, g->font_loaded, "fill", (int)rb.x + ((int)rb.width - tw) / 2, (int)rb.y + ((int)rb.height - fs) / 2, fs, RAYWHITE);
    }
    if (g->pix_src != GFX_TILES) {                       // whose frame this is, on two lines
        int c = creature_of(g->pix_src, idx);
        if (c >= 0) {
            gtext(&g->font, g->font_loaded, TextFormat("%s (frames 0x%02x-0x%02x)", CREATURES[c].name, CREATURES[c].lo, CREATURES[c].hi),
                  x0, ty + 3 * ch(g), fs, SKYBLUE);
            if (CREATURES[c].type >= 0)
                gtext(&g->font, g->font_loaded, "Shift+click in the room places one", x0, ty + 4 * ch(g), fs, SKYBLUE);
        }
    }
    if (!g->sprites_view && !g->backs_view)
        for (int i = 0; i < 4; i++) {                    // attribute of painted tiles
            Rectangle rb = attr_button(g, i);
            bool on = ATTRS[i].bits == g->sel_attr;
            bool kbd = g->focus == FOCUS_BUTTONS && g->btn_row == 1 && i == g->btn_sel % 4;
            DrawRectangleRec(rb, on ? (Color){90, 70, 20, 255} : (Color){50, 50, 60, 255});
            DrawRectangleLinesEx(rb, kbd ? 2 : 1, kbd || on ? GOLD : GRAY);
            int tw = text_w(&g->font, g->font_loaded, ATTRS[i].name, fs);
            gtext(&g->font, g->font_loaded, ATTRS[i].name, (int)rb.x + ((int)rb.width - tw) / 2, (int)rb.y + 2, fs, RAYWHITE);
        }
    const char *names[] = { "reset", "tiles", "sprites", "backs", "block" };
    (void)block_available;
    for (int i = 0; i < pixed_button_count(g); i++) {
        Rectangle rb = pixed_button(g, i);
        bool on = (i == 1 && !g->sprites_view && !g->backs_view) || (i == 2 && g->sprites_view) || (i == 3 && g->backs_view);
        bool kbd = g->focus == FOCUS_BUTTONS && g->btn_row == 0 && i == g->btn_sel;
        DrawRectangleRec(rb, on ? (Color){90, 70, 20, 255} : (Color){50, 50, 60, 255});
        DrawRectangleLinesEx(rb, kbd ? 2 : 1, kbd ? GOLD : on ? GOLD : GRAY);
        int tw = text_w(&g->font, g->font_loaded, names[i], fs);
        gtext(&g->font, g->font_loaded, names[i], (int)rb.x + ((int)rb.width - tw) / 2, (int)rb.y + 2, fs, RAYWHITE);
    }
}

// Standing Spider-Man texture (drawn at the cursor), from SPIDS1 as it is now.
static void spidey_build(GuiState *g) {
    static uint8_t sbuf[SPIDS_FRAME_W * SPIDS_FRAME_H * 4];
    render_gfx_rgba(g->rctx, GFX_SPIDS1, render_player_frame(), sbuf);
    Image img = { .data = sbuf, .width = SPIDS_FRAME_W, .height = SPIDS_FRAME_H,
                  .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    if (g->spidey_loaded) UnloadTexture(g->spidey);
    g->spidey = LoadTextureFromImage(img);
    SetTextureFilter(g->spidey, TEXTURE_FILTER_POINT);
    g->spidey_loaded = true;
}

/* -------------------------------------------------------------- world map */

static int world_y(GuiState *g) { return labels_y(g) + PALETTE_LINES * ch(g) + PAD; }
// attribute buttons (what painted tiles are): a row under the source buttons
static Rectangle attr_button(GuiState *g, int i) {
    int bw = 7 * cw(g);
    return (Rectangle){ pixed_x(g) + i * (bw + 4), pixed_buttons_y(g) + ch(g) + 8, bw, ch(g) + 4 };
}

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
        if (!rooms->rooms[r].valid) {                    // an empty slot: a click or Enter creates the room
            DrawRectangleRec(d, (Color){25, 25, 30, 255});
        } else {
            DrawTexturePro(g->atlas, (Rectangle){(r % 8) * ROOM_PX_W, (r / 8) * ROOM_PX_H, ROOM_PX_W, ROOM_PX_H}, d, (Vector2){0, 0}, 0, WHITE);
            const char *side = r == cur->room_north ? "N" : r == cur->room_south ? "S" :
                               r == cur->room_west ? "W" : r == cur->room_east ? "E" : NULL;
            if (r == ged_room()) DrawRectangleLinesEx(d, 2, YELLOW);
            else if (side) {
                DrawRectangleLinesEx(d, 2, SKYBLUE);
                DrawRectangle((int)d.x, (int)d.y, cw(g) + 2, ch(g), (Color){0, 0, 0, 170});
                gtext(&g->font, g->font_loaded, side, (int)d.x + 1, (int)d.y, fs, SKYBLUE);
            }
        }
        if (r == g->hover_room) DrawRectangleLinesEx(d, 2, WHITE);
        if (g->focus == FOCUS_WORLD && r == g->world_sel) DrawRectangleLinesEx((Rectangle){d.x - 2, d.y - 2, d.width + 4, d.height + 4}, 2, GOLD);
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

// Ghost of what Shift+click would put at the hovered cell: the selected tile,
// or, in the sprites view, the selected creature's frame drawn the way the
// room shows it (an object at (x,y) stands with its feet on tile (x+1,y+1)).
static void draw_hover_preview(GuiState *g) {
    int tx, ty;
    if (!(mods() & GK_MOD_SHIFT)) return;
    if (!mouse_tile(g, &tx, &ty)) return;
    Vector2 p = tile_pt(g, tx, ty);
    int s = 8 * g->scale;
    if (g->sprites_view) {
        int c = creature_of(g->pix_src, g->sel_img[g->pix_src]);
        if (c < 0 || CREATURES[c].type < 0 || !g->sheet_loaded[g->pix_src]) return;
        int f = g->sel_img[g->pix_src];
        Rectangle src = { (f % 8) * SPIDS_FRAME_W, (f / 8) * SPIDS_FRAME_H, SPIDS_FRAME_W, SPIDS_FRAME_H };
        Vector2 feet = tile_pt(g, tx + 1 - 2, ty + 2);          // box left = (feet_x - 2) tiles + 3 px
        Rectangle dst = { feet.x + 3 * g->scale, feet.y - SPIDS_FRAME_H * g->scale,
                          SPIDS_FRAME_W * g->scale, SPIDS_FRAME_H * g->scale };
        DrawTexturePro(g->sheet_tex[g->pix_src], src, dst, (Vector2){0, 0}, 0, (Color){255, 255, 255, 170});
        DrawRectangleLinesEx((Rectangle){p.x, p.y, s, s}, 2, YELLOW);
        return;
    }
    Rectangle dst = { p.x, p.y, s, s };
    if (g->backs_view) {
        if (!g->sheet_loaded[GFX_SCENERY]) return;
        int t = g->sel_img[GFX_SCENERY];
        Rectangle src = { (t % BACKS_COLS) * 8, (t / BACKS_COLS) * 8, 8, 8 };
        DrawTexturePro(g->sheet_tex[GFX_SCENERY], src, dst, (Vector2){0, 0}, 0, (Color){255, 255, 255, 150});
        DrawRectangleLinesEx(dst, 2, GREEN);
        return;
    }
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
    // Which links to draw thick: every chunk of the switch under the cursor (or
    // the one selected, e.g. an out-of-bounds switch chosen with s7), and every
    // chunk that targets the object or tile under the cursor.
    int cx, cy, cur_idx, cur_kind, sel_sw = ged_current_switch();
    ged_cursor(&cx, &cy);
    cur_kind = ged_item_at(cx, cy, &cur_idx);
    if (cur_kind == 1) sel_sw = cur_idx;
    for (int i = 0; i < r->num_switches; i++) {
        struct SwitchObject *sw = r->switches + i;
        if (sw->chunks.length == 0) continue;
        Vector2 o = tile_pt(g, sw->chunks.data[0].x + 0.5f, sw->chunks.data[0].y + 0.5f);
        bool hot_sw = i == sel_sw;
        DrawCircleV(o, emph_sw || hot_sw ? 6 : 4, emph_sw || hot_sw ? YELLOW : C_SW);
        label(g, TextFormat("S%d", i), o.x + 6, o.y - fs / 2, C_SW, emph_sw || hot_sw);
        for (size_t c = 1; c < sw->chunks.length; c++) {
            struct SwitchChunk *ch = sw->chunks.data + c;
            bool hot = hot_sw;
            if (ch->type == TOGGLE_BLOCK) {
                int w = ch->dir == HORIZONTAL ? ch->size : 1;
                int h = ch->dir == VERTICAL ? ch->size : 1;
                if (cx >= ch->x && cx < ch->x + w && cy >= ch->y && cy < ch->y + h) hot = true;
                Vector2 t = tile_pt(g, ch->x, ch->y);
                DrawRectangleLinesEx((Rectangle){t.x, t.y, w * s, h * s}, hot ? 3 : 1, hot ? YELLOW : C_TBLK);
                Vector2 e = tile_pt(g, ch->x + 0.5f, ch->y + 0.5f);
                if (hot) DrawLineEx(o, e, 3, YELLOW); else DrawLineV(o, e, C_TBLK);
            } else if (ch->type == TOGGLE_OBJECT && ch->index < r->num_objects) {
                struct RoomObject *t = r->objects + ch->index;
                if (cur_kind == 2 && cur_idx == ch->index) hot = true;
                float cw = t->type == BLOCK ? t->block.width / 2.0f : 0.5f;
                float chh = t->type == BLOCK ? t->block.height / 2.0f : 0.5f;
                Vector2 e = tile_pt(g, t->x + cw, t->y + chh);
                if (hot) DrawLineEx(o, e, 3, YELLOW); else DrawLineV(o, e, C_OBJ);
            } else if (ch->type == TOGGLE_BIT) {
                Vector2 e = o;
                int rid = ch->room_idx;
                if (rid == r->room_north) e = tile_pt(g, sw->chunks.data[0].x + 0.5f, 0);
                else if (rid == r->room_south) e = tile_pt(g, sw->chunks.data[0].x + 0.5f, HEIGHT_TILES);
                else if (rid == r->room_west) e = tile_pt(g, 0, sw->chunks.data[0].y + 0.5f);
                else if (rid == r->room_east) e = tile_pt(g, WIDTH_TILES, sw->chunks.data[0].y + 0.5f);
                else if (rid == ged_room() && ch->switch_idx < r->num_switches) {   // a switch in this room
                    struct SwitchObject *ts = r->switches + ch->switch_idx;
                    if (ts->chunks.length) e = tile_pt(g, ts->chunks.data[0].x + 0.5f, ts->chunks.data[0].y + 0.5f);
                    if (cur_kind == 1 && cur_idx == (int)ch->switch_idx) hot = true;
                }
                if (e.x != o.x || e.y != o.y) {
                    if (hot) DrawLineEx(o, e, 3, YELLOW); else DrawLineV(o, e, C_BIT);
                    if (rid != ged_room()) label(g, TextFormat("R%d", rid), e.x - 8, e.y - 7, C_BIT, false);
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
        for (int i = 0; i < GFX_SOURCES; i++) if (g->sheet_loaded[i]) UnloadTexture(g->sheet_tex[i]);
        if (g->atlas_loaded) UnloadTexture(g->atlas);
        if (g->font_loaded) UnloadFont(g->font);
        if (g->help_font_loaded) UnloadFont(g->help_font);
        if (g->panel_font_loaded) UnloadFont(g->panel_font);
        render_close(g->rctx);
        ged_close(g->es, file);
        free(g);
        g = NULL;
    }
    if (g == NULL) {
        g = calloc(1, sizeof *g);
        g->structsize = sizeof *g;
        g->rctx = render_open(data_dir, false);
        if (g->rctx == NULL) { free(g); return NULL; }
        g->fixed_scale = scale;
        g->sel_attr = ATTR_SOLID;
        g->block_hidden = -1;
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
    g->data_dir = data_dir;
    g->drawn_room = -1;
    g->room_dirty = true;
    g->hover_room = -1;
    load_fonts(g);
    world_build(g, ged_rooms(), buf);
    if (!g->spidey_loaded) spidey_build(g);

    while (!WindowShouldClose() && !ged_quit_requested()) {
        float dt = GetFrameTime();
        g->src_check -= dt;                                // throttle the hot-reload stat()s
        if (g->src_check <= 0 && getenv("SPIDEY_NO_HOTRELOAD") == NULL) {   // --release: never watch / rebuild
            g->src_check = 0.5f;
            if (sources_changed(last_build)) {
                fprintf(stderr, "gui: source changed, rebuilding\n");
                ged_flush();                               // don't lose painted tiles
                gfx_save_all(g);
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
        if (g->world_stale) { world_build(g, ged_rooms(), buf); g->world_stale = false; }
        g->pix_op = false;
        if (feed_mouse(g)) {
            g->room_dirty = true;
            if (!g->pix_op) g->key_seq = ++g->seq;      // a room edit by mouse: `u` goes to the editor
        }

        bool gfx_dirty = g->backs_dirty;
        for (int i = 0; i < GFX_SOURCES; i++) gfx_dirty |= g->gfx_dirty[i];
        if (gfx_dirty && !g->pix_painting && !g->backs_painting) {   // debounced write of pixel / background edits
            g->gfx_timer -= dt;
            if (g->gfx_timer <= 0) {
                if (gfx_save_all(g)) g->saved_flash = 1.2f;
                world_build(g, ged_rooms(), buf);          // every room may use the edited image
                g->room_dirty = true;
            }
        }
        if (g->paint_dirty) {                              // debounced write of paint strokes
            g->save_timer -= dt;
            if (g->save_timer <= 0 && !g->painting) {
                if (ged_flush()) g->saved_flash = 1.2f;
                g->room_dirty = true;                  // a refused write undid the stroke: show the room as saved
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

        // textures must not be (re)built inside BeginDrawing: it corrupts the batch
        if (g->sprites_view)
            for (int k = GFX_SPIDS1; k <= GFX_SPIDS2; k++)
                if (!g->sheet_loaded[k] || g->sheet_stale[k]) sheet_build(g, (GfxSource)k);
        if (g->backs_view && (!g->sheet_loaded[GFX_SCENERY] || g->sheet_stale[GFX_SCENERY])) sheet_build(g, GFX_SCENERY);

        if (g->saved_flash > 0) g->saved_flash -= dt;
        if (g->err_flash > 0) g->err_flash -= dt;
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
        {
            int bi, bx, by, bw, bh;
            bool on_block = ged_block_at_cursor(&bi, &bx, &by, &bw, &bh);
            if (!on_block) g->block_hidden = -1;
            if (on_block && bi != g->block_hidden) draw_block_editor(g, bi, bx, by, bw, bh);
            else draw_gfx_editor(g, on_block);
        }
        draw_world_map(g);
        if (ged_help_shown()) draw_help(g);
        EndDrawing();
    }

    gfx_save_all(g);
    ged_close(g->es, file);          // writes pending paints, hands the rooms back to main()
    if (g->tex_loaded) UnloadTexture(g->tex);
    if (g->pal_loaded) UnloadTexture(g->pal_tex);
    if (g->spidey_loaded) UnloadTexture(g->spidey);
    for (int i = 0; i < GFX_SOURCES; i++) if (g->sheet_loaded[i]) UnloadTexture(g->sheet_tex[i]);
    if (g->atlas_loaded) UnloadTexture(g->atlas);
    if (g->font_loaded) UnloadFont(g->font);
    if (g->help_font_loaded) UnloadFont(g->help_font);
    if (g->panel_font_loaded) UnloadFont(g->panel_font);
    render_close(g->rctx);
    free(g);
    CloseWindow();
    return NULL;
}
