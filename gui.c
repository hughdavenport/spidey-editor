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
#include <math.h>
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
#define SWATCH_ROWS 2
enum { FOCUS_ROOM, FOCUS_PICKER, FOCUS_EDITOR, FOCUS_COLOURS, FOCUS_TOOLS, FOCUS_ATTRS, FOCUS_BUTTONS, FOCUS_WORLD, FOCUS_COUNT };
#define ATTR_SOLID 0x40
#define BLOCK_BUTTONS 6         // block panel: w+ w- / h+ h- / import export (gfx is the grid's row-4 slot)
#define BUTTON_ROWS 6           // two per row: reset tiles / sprites backs / font screens / import export / block / room
static const struct { uint8_t bits; const char *name; } ATTRS[4] = {
    {0x00, "pass"}, {0x40, "solid"}, {0x80, "magnet"}, {0xc0, "slip"},
};
// RGB for each attribute class (Ctrl-D t's room tint, and the legend under
// the pass/solid/magnet/slip buttons); each use picks its own alpha. pass and
// solid are swapped from their natural order (Hugh: "swap the pass and solid
// colour's") - blue is pass-through, grey is solid.
static const Color ATTR_COLOUR[4] = { {0, 120, 255, 255}, {200, 200, 200, 255}, {200, 60, 255, 255}, {0, 230, 230, 255} };
// Alpha for Ctrl-D t's per-class tint, shared by the room picture and the block
// object tile editor (see draw_overlay and draw_block_editor).
static const uint8_t TILE_TINT_ALPHA[4] = {60, 110, 120, 120};
static const Color HURT_COLOUR = {255, 40, 40, 255};   // Ctrl-D t: hurting tiles (tile_hurts, room.h), whatever their class
#define HURT_TINT_ALPHA 90
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
    bool world_pending;     // ... after the next graphics save (a file the rooms are drawn from changed)
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
    float saved_flash;      // seconds left to show "saved <files>"
    char saved_what[160];   // the file names the last save wrote (ROOMS.SPL, BLOCKS.EGA, BACKS.SPL, ...)
    unsigned seen_saves;    // ged_save_count() last frame: any editor write (keyboard edits included) flashes "saved"
    float err_flash;        // seconds left to show err
    char err[96];           // a refused write, shown under the picker in red
    float src_check;        // seconds until the next hot-reload source check
    bool pix_op;            // this frame's mouse event went to the pixel editor (for `u` routing)
    int focus;              // which panel arrows / hjkl move: the room, the picker, the pixel editor, its colours, its buttons, or the world map
    int btn_sel;            // FOCUS_BUTTONS: the reset / tiles / sprites / backs / block button the keyboard is on
    int btn_row;            // FOCUS_BUTTONS: the grid row the keyboard is on
    int attr_sel;           // FOCUS_ATTRS: the pass / solid / magnet / slip button the keyboard is on
    int tool_sel;           // FOCUS_TOOLS: 0 = pick, 1 = fill
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
                            // (the out-of-bounds switch list is part of this same panel/scroll)
                            // - purely manual: nothing auto-scrolls it (Hugh: "remove the
                            // autoscroll of the info panel"), just clamped to stay in range.
    Texture2D atlas;        // world map: every room rendered, 8x8 grid of ROOM_PX_W x ROOM_PX_H
    bool atlas_loaded;
    int hover_room;         // world-map thumbnail under the mouse, or -1
    bool wpress;            // left press on a world-map thumbnail waiting to become a click or a drag-move
    int wpress_room;
    int hov_room, hov_sw, hov_chunk;   // the switch link the mouse is within reach of (hov_sw -1 = none): drawn a little fatter
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
    bool font_view;             // picker shows the CHARSET font
    bool screens_view;          // picker lists the full-screen pictures; the room area shows the chosen one
    GfxSource screen_src;       // the screen last chosen in that list
    Texture2D scr_tex;          // that screen, for the big view and the thumbnail
    bool scr_loaded, scr_stale;
    GfxSource scr_src;          // which screen scr_tex holds
    Color err_col;              // colour of the err message (red for refusals, green for notes)
    int sel_sprite;
    int sel_img[GFX_SOURCES];   // selected image per source (tiles: index within the current tileset)
    int sel_colour;             // colour to paint with
    bool pix_painting;          // pixel stroke in progress
    bool gfx_dirty[GFX_SOURCES];    // edited file not yet written
    float gfx_timer;
    Texture2D sheet_tex[GFX_SOURCES];   // sprite sheets as 8-across pickers (tiles use pal_tex)
    bool sheet_loaded[GFX_SOURCES];
    bool sheet_stale[GFX_SOURCES];
    struct { int src; int idx; uint8_t data[GFX_MAX_BYTES]; } pix_undo[64];   // image (GfxSource) or background block (src UNDO_BACKS) before each stroke / reset
    int pix_undo_n;
    unsigned seq, pix_seq, key_seq;   // ordering of pixel strokes vs editor keys, for `u`
    Font panel_font;        // text under the picture: a touch smaller so PANEL_COLS fit its width
    bool panel_font_loaded;
    int panel_font_cw;
    int panel_font_px;
} GuiState;
#define PANEL_COLS 84           // columns of panel text that fit across the picture
#define UNDO_BACKS (-1)         // pix_undo.src: a BACKS block, idx = block id
enum { VIEW_TILES, VIEW_SPRITES, VIEW_BACKS, VIEW_FONT, VIEW_SCREENS };
// The full-screen pictures, in the order the screens picker lists them.
static const GfxSource SCREENS[] = { GFX_LOADSCRN, GFX_WINSCRN, GFX_PAGE1, GFX_PAGE2, GFX_PAGE3, GFX_PAGE4, GFX_HISCORE, GFX_SPIDFIGR, GFX_SKELFIGR };
#define NSCREENS ((int)(sizeof SCREENS / sizeof SCREENS[0]))
#define BACKS_COLS 16           // the scenery picker: 16 x 16 tiles at the tile zoom (see picker_w)
#define FONT_COLS 8             // the font picker: 8 x 16 tiles at the tile zoom, like the tileset
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
static int  world_room_at(GuiState *g, Vector2 m);
static void world_geom(GuiState *g, int *tw, int *th);
static Rectangle world_cell(GuiState *g, int room, int tw, int th);
static void world_click_tile(GuiState *g, int room, Vector2 m, int *tx, int *ty);
static void draw_world_links(GuiState *g);
static bool link_click(GuiState *g, Vector2 m);
static void link_hover(GuiState *g, Vector2 m);
static bool link_hovered(GuiState *g, int room, int sw, int chunk);
static Vector2 switch_anchor(GuiState *g, struct SwitchObject *sw);
static Vector2 arrival_edge(GuiState *g, int ex);
static void label(GuiState *g, const char *s, float x, float y, Color col, bool emph);
static void draw_source_grid(GuiState *g, bool block_up);
static void link_run(int x0, int y0, int x1, int y1, int *rx, int *ry, int *rw, int *rh);
static Rectangle pixed_button(GuiState *g, int i);
static Rectangle attr_button(GuiState *g, int i, int row_offset);
static bool grid_row_valid(GuiState *g, int row);
static Rectangle grid_button(GuiState *g, int row, int col);
static Rectangle pick_button(GuiState *g);
static Rectangle fill_button(GuiState *g);
static int  pixed_button_count(GuiState *g);
static void pixed_button_press(GuiState *g, int i);
static void room_export_button(GuiState *g);
static void room_import_button(GuiState *g);
static void pix_fill(GuiState *g, int idx, int x, int y);
static bool pixed_locked(GuiState *g);
static void focus_move(GuiState *g, int dx, int dy);
static void focus_apply(GuiState *g);
static bool block_panel(GuiState *g, int *bi, int *bx, int *by, int *bw, int *bh);
static void sync_sprite_sel(GuiState *g);
static void set_view(GuiState *g, int view);
static bool tiles_view(GuiState *g);
static void clamp_colour(GuiState *g);
static void screen_geom(GuiState *g, int *z, int *ox, int *oy, int *w, int *h);
static bool mouse_screen_px(GuiState *g, int *x, int *y);
static void screen_tex_build(GuiState *g);
static void draw_screen_mode(GuiState *g);
static bool screen_mode_mouse(GuiState *g);
static void smode_button_press(GuiState *g, int i);
static void draw_swatches(GuiState *g, int x0, int sy, int sp);
static void draw_transparent_swatch(int x, int y, int size);
static int  swatch_pick(GuiState *g, int x0, int sy, int sp, Vector2 m);
static void import_png(GuiState *g, const char *path);
static void export_png(GuiState *g);
static void block_export_png(GuiState *g, int idx, int w, int h);
static void block_import_png(GuiState *g, int idx, int w, int h, const char *path);
static void gui_note(GuiState *g, const char *msg);
static void gui_error(GuiState *g, const char *msg);
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
// The picker is the tileset's width, except the scenery picker: 16 across at the
// tile zoom, so twice as wide, and the pixel editor moves right to make room.
static int picker_w(GuiState *g) { return (g->backs_view ? 2 : 1) * TILESET_PX * g->scale; }
static int pixed_x(GuiState *g) { return palette_x(g) + picker_w(g) + 2 * PAD; }
static void pixed_dims(GuiState *g, int *w, int *h) { render_gfx_size(g->pix_src, w, h); }
static int pixed_px(GuiState *g) { int w, h; pixed_dims(g, &w, &h); int p = TILESET_PX * g->scale / h; return p < 2 ? 2 : p; }
static int pixed_w(GuiState *g) { int w, h; pixed_dims(g, &w, &h); return w * pixed_px(g); }
static int pixed_h(GuiState *g) { int w, h; pixed_dims(g, &w, &h); return h * pixed_px(g); }
static int swatch_px(GuiState *g) { return pixed_w(g) / 8; }
static int swatch_y(GuiState *g) { return palette_y(g) + pixed_h(g) + PAD; }
static int pixed_text_y(GuiState *g) { return swatch_y(g) + SWATCH_ROWS * swatch_px(g) + PAD; }
static int pixed_bottom(GuiState *g) { return pixed_text_y(g) + 9 * ch(g); }   // status, creature / hint, the picker's labels, saved
// The buttons sit under the picker, two per row, from below where the tallest
// picker ends (the sprite sheets, 16 rows of frames, or the font, 16 rows of
// tiles), so they never move.
static int buttons_y0(GuiState *g) {
    int sprites = (int)(16 * (TILESET_PX * g->scale / 8.0f) * SPIDS_FRAME_H / SPIDS_FRAME_W), font = 16 * 8 * g->scale;
    return palette_y(g) + (sprites > font ? sprites : font) + PAD;
}
static int buttons_bottom(GuiState *g) { return buttons_y0(g) + BUTTON_ROWS * (ch(g) + 8); }   // the attribute rows are above, not below
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
// picture the rows are the scrolled panel (the out-of-bounds switch list is
// part of this same flow now, not a separate pinned region). The space right
// of the picture is the palette / world map, not cells.
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
    if (py >= pinned_top(g)) return false;
    int want = (py - panel_top(g)) / pch(g) + g->scroll, v = 0;
    for (int y = GED_MAP_Y + HEIGHT_TILES; y < GED_ROWS - 1; y++) {
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
    if ((view == VIEW_BACKS) != g->backs_view) g->room_dirty = true;   // the picture drops / regains its objects
    g->sprites_view = view == VIEW_SPRITES;
    g->backs_view = view == VIEW_BACKS;
    g->font_view = view == VIEW_FONT;
    g->screens_view = view == VIEW_SCREENS;
    if (view == VIEW_TILES) g->pix_src = GFX_TILES;
    else if (view == VIEW_BACKS) g->pix_src = GFX_SCENERY;
    else if (view == VIEW_FONT) g->pix_src = GFX_CHARSET;
    else if (view == VIEW_SCREENS) {
        if (!render_gfx_is_screen(g->screen_src)) g->screen_src = GFX_LOADSCRN;
        g->pix_src = g->screen_src;
        g->scr_stale = true;
        g->focus = FOCUS_PICKER;                  // the list first; Tab: list, colours, buttons, picture
    } else sync_sprite_sel(g);
    if (view != VIEW_SCREENS && g->focus == FOCUS_EDITOR) g->focus = FOCUS_ROOM;
    clamp_colour(g);
}
static bool tiles_view(GuiState *g) { return !g->sprites_view && !g->backs_view && !g->font_view && !g->screens_view; }

// The paint colour must exist for the current image (the CGA sprites' 5th,
// "transparent" colour does not elsewhere).
static void clamp_colour(GuiState *g) {
    int n = render_colours(g->rctx), key = render_gfx_key(g->rctx, g->pix_src);
    if (g->sel_colour >= n && g->sel_colour != key) g->sel_colour = n - 1;
}

static int screen_index(GfxSource s) { for (int i = 0; i < NSCREENS; i++) if (SCREENS[i] == s) return i; return 0; }

// Screen mode: the whole window is the chosen picture at the largest integer
// zoom that fits left of a side panel (list, swatches, buttons); nothing of
// the room editor is shown until Esc / "room".
#define SMODE_PANEL_COLS 32
static int smode_panel_x(GuiState *g) { return GetScreenWidth() - SMODE_PANEL_COLS * cw(g); }
static void screen_geom(GuiState *g, int *z, int *ox, int *oy, int *w, int *h) {
    render_gfx_size(g->pix_src, w, h);
    int aw = smode_panel_x(g) - 2 * PAD, ah = GetScreenHeight() - 2 * PAD;
    int zx = aw / *w, zy = ah / *h;
    *z = zx < zy ? zx : zy;
    if (*z < 1) *z = 1;
    *ox = PAD + (aw - *w * *z) / 2;
    *oy = PAD + (ah - *h * *z) / 2;
}
// Panel layout, top to bottom: title, the screens list, swatches, buttons, notes.
static Rectangle smode_list_row(GuiState *g, int i) {
    return (Rectangle){ smode_panel_x(g) + PAD, PAD + 2 * ch(g) + i * (ch(g) + 2), SMODE_PANEL_COLS * cw(g) - 2 * PAD, ch(g) };
}
static int smode_swatch_px(GuiState *g) { return 3 * cw(g); }
static int smode_swatch_y(GuiState *g) { return (int)smode_list_row(g, NSCREENS).y + PAD; }
// pick fill / import export / room, 9 cells wide
static Rectangle smode_button(GuiState *g, int i) {
    int bw = 9 * cw(g), row = i / 2, col = i % 2;
    return (Rectangle){ smode_panel_x(g) + PAD + col * (bw + PAD), smode_swatch_y(g) + SWATCH_ROWS * smode_swatch_px(g) + PAD + row * (ch(g) + 8), bw, ch(g) + 4 };
}
static int smode_text_y(GuiState *g) { return (int)smode_button(g, 4).y + ch(g) + 4 + PAD; }
static bool mouse_screen_px(GuiState *g, int *x, int *y) {
    int z, ox, oy, w, h;
    screen_geom(g, &z, &ox, &oy, &w, &h);
    Vector2 m = GetMousePosition();
    if (m.x < ox || m.y < oy || m.x >= ox + w * z || m.y >= oy + h * z) return false;
    *x = (int)(m.x - ox) / z; *y = (int)(m.y - oy) / z;
    return true;
}

// The background block the current room shows.
static int cur_backs(void) { return ged_rooms()->rooms[ged_room()].data.background % BACKS_BLOCKS; }

// Is the block panel (rather than the pixel editor) showing right now?
static bool block_panel(GuiState *g, int *bi, int *bx, int *by, int *bw, int *bh) {
    // Background mode has no blocks (they are not even drawn), so the cursor resting on one
    // must not bring the block panel up - that would dim the view buttons and snap the picker
    // back to the tiles, undoing a Ctrl-B the moment it was pressed.
    if (g->backs_view) return false;
    return ged_block_at_cursor(bi, bx, by, bw, bh) && *bi != g->block_hidden;
}

// Arrow / hjkl movement for the panel that has keyboard focus.
static void focus_move(GuiState *g, int dx, int dy) {
    if (g->focus == FOCUS_PICKER) {
        if (g->font_view) g->sel_img[GFX_CHARSET] = (g->sel_img[GFX_CHARSET] + dx + FONT_COLS * dy + 128) % 128;
        else if (g->screens_view) {
            int i = ((screen_index(g->pix_src) + dx + dy) % NSCREENS + NSCREENS) % NSCREENS;
            g->screen_src = g->pix_src = SCREENS[i]; g->scr_stale = true;
        }
        else if (g->backs_view) g->sel_img[GFX_SCENERY] = (g->sel_img[GFX_SCENERY] + dx + BACKS_COLS * dy + 256) % 256;
        else if (!g->sprites_view) g->sel_tile = (uint8_t)((g->sel_tile + dx + 8 * dy + 64) % 64);
        else { g->sel_sprite += dx + 8 * dy; sync_sprite_sel(g); }
    } else if (g->focus == FOCUS_WORLD) {
        g->world_sel = ((g->world_sel + dx + 8 * dy) % 64 + 64) % 64;
    } else if (g->focus == FOCUS_COLOURS) {           // the swatches (8 x 2, or 4 in CGA, + transparent for CGA sprites): moving selects
        int n = render_colours(g->rctx), key = render_gfx_key(g->rctx, g->pix_src);
        if (key >= n) n = key + 1;
        g->sel_colour = ((g->sel_colour + dx + (n > 8 ? 8 : 0) * dy) % n + n) % n;
    } else if (g->focus == FOCUS_BUTTONS && g->screens_view) {   // screen mode: pick fill / import export / room
        g->btn_sel = ((g->btn_sel + dx + 2 * dy) % 5 + 5) % 5;
    } else if (g->focus == FOCUS_ATTRS) {             // 2 x 2
        g->attr_sel = ((g->attr_sel % 2 + dx) % 2 + 2) % 2 + 2 * (((g->attr_sel / 2 + dy) % 2 + 2) % 2);
    } else if (g->focus == FOCUS_TOOLS) {             // pick | fill
        if (dx || dy) g->tool_sel = !g->tool_sel;
    } else if (g->focus == FOCUS_BUTTONS) {           // the button grid: left / right = column, up / down = next row that has buttons
        int step = dy > 0 ? 1 : dy < 0 ? -1 : 0;
        if (step) for (int tries = 0; tries < BUTTON_ROWS; tries++) {
            g->btn_row = ((g->btn_row + step) % BUTTON_ROWS + BUTTON_ROWS) % BUTTON_ROWS;
            if (grid_row_valid(g, g->btn_row)) break;
        }
        if (!grid_row_valid(g, g->btn_row)) g->btn_row = 0;
        g->btn_sel = ((g->btn_sel + dx) % 2 + 2) % 2;
        if (g->btn_row == 4) g->btn_sel = 0;              // "block" stands alone in its row
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
        ged_map_click(g->world_sel, false, -1, -1);   // by keyboard: go there, and stay on the map to keep browsing
    } else if (g->focus == FOCUS_PICKER && g->screens_view) {
        g->focus = FOCUS_EDITOR;                      // the list's choice is showing: over to the picture
    } else if (g->focus == FOCUS_BUTTONS && g->screens_view) {
        smode_button_press(g, g->btn_sel % 5);
    } else if (g->focus == FOCUS_ATTRS) {
        g->sel_attr = ATTRS[g->attr_sel % 4].bits;
    } else if (g->focus == FOCUS_TOOLS) {
        if (g->tool_sel) { g->fill_armed = !g->fill_armed; g->eyedrop = false; }
        else { g->eyedrop = !g->eyedrop; g->fill_armed = false; }
        g->pix_op = true;
    } else if (g->focus == FOCUS_BUTTONS) {
        if (g->btn_row == 5) { if (g->btn_sel % 2 == 0) room_import_button(g); else room_export_button(g); }
        else if (g->btn_row * 2 + g->btn_sel % 2 < pixed_button_count(g)) pixed_button_press(g, g->btn_row * 2 + g->btn_sel % 2);
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
        if (g->font_view || g->screens_view) {
            /* nothing to put in the room from these */
        } else if (g->backs_view) {
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
        if (g->screens_view && c != 'h' && c != 'j' && c != 'k' && c != 'l' && c != ' ' && c != 'f' && c != 'u') continue;   // screen mode: the room is not there to edit
        if (g->focus != FOCUS_ROOM && (c == 'h' || c == 'j' || c == 'k' || c == 'l')) {
            focus_move(g, c == 'h' ? -1 : c == 'l' ? 1 : 0, c == 'k' ? -1 : c == 'j' ? 1 : 0);
            any = true;
            continue;
        }
        if (g->focus != FOCUS_ROOM && c == ' ') { focus_apply(g); any = true; continue; }
        if ((g->focus == FOCUS_EDITOR || g->screens_view) && c == 'f') {   // pixel editor focused (or screen mode): toggle fill mode
            int bi, bx, by, bw, bh;
            if (g->screens_view || !block_panel(g, &bi, &bx, &by, &bw, &bh)) { g->fill_armed = !g->fill_armed; g->eyedrop = false; any = true; continue; }
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
        if (rk == KEY_ESCAPE && g->backs_view) { set_view(g, VIEW_TILES); any = true; continue; }   // background mode: Esc = back to the tiles
        if (rk == KEY_B && (m & GK_MOD_CTRL)) {                          // GUI-only: Ctrl-B swaps the picker tiles <-> background;
            if (g->backs_view) set_view(g, VIEW_TILES);                  // the panel follows, and so does the hex map while F2 is on
            else if (tiles_view(g)) set_view(g, VIEW_BACKS);             // (sprites / font / screens: nothing)
            any = true; continue;
        }
        if (rk == KEY_F3) { set_gfx_mode(g, !g->cga); any = true; continue; }   // GUI-only: EGA / CGA files
        if (g->screens_view) {                                          // screen mode: Esc leaves, Tab cycles, arrows / Enter edit, help still works
            if (rk == KEY_ESCAPE) { set_view(g, VIEW_TILES); any = true; continue; }
            if (rk == KEY_TAB) {
                static const int cycle[] = { FOCUS_PICKER, FOCUS_COLOURS, FOCUS_BUTTONS, FOCUS_EDITOR };
                int at = 0;
                for (int c = 0; c < 4; c++) if (cycle[c] == g->focus) at = c;
                g->focus = cycle[(at + ((m & GK_MOD_SHIFT) ? 3 : 1)) % 4];
                any = true; continue;
            }
            if (rk == KEY_LEFT || rk == KEY_RIGHT || rk == KEY_UP || rk == KEY_DOWN) {
                focus_move(g, rk == KEY_LEFT ? -1 : rk == KEY_RIGHT ? 1 : 0, rk == KEY_UP ? -1 : rk == KEY_DOWN ? 1 : 0);
                any = true; continue;
            }
            if (rk == KEY_ENTER || rk == KEY_KP_ENTER) { focus_apply(g); any = true; continue; }
            if (!((rk == KEY_SLASH || rk == KEY_MINUS) && (m & GK_MOD_CTRL))) continue;
        }
        if (rk == KEY_TAB && !(m & (GK_MOD_CTRL | GK_MOD_ALT))) {      // GUI-only: cycle keyboard focus
            int step = (m & GK_MOD_SHIFT) ? FOCUS_COUNT - 1 : 1, bi, bx, by, bw, bh;
            g->focus = (g->focus + step) % FOCUS_COUNT;
            while (((g->focus == FOCUS_COLOURS || g->focus == FOCUS_TOOLS || g->focus == FOCUS_BUTTONS || g->focus == FOCUS_ATTRS) && block_panel(g, &bi, &bx, &by, &bw, &bh))   // hidden while the block panel is up
                   || (g->focus == FOCUS_ATTRS && !tiles_view(g)))                                                     // attributes exist in the tiles view only
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

    if (g->screens_view) return screen_mode_mouse(g);

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
        g->focus = FOCUS_TOOLS; g->tool_sel = 0;
        g->pix_op = true;
        return true;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, fill_button(g)) && !ged_block_at_cursor(&tx, &ty, &tx, &ty, &tx)) {
        g->fill_armed = !g->fill_armed; g->eyedrop = false;
        g->focus = FOCUS_TOOLS; g->tool_sel = 1;
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

    // picker: pick a tile, a scenery tile, a font tile, a screen, or a sprite frame
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int side = TILESET_PX * g->scale, px0 = palette_x(g), py0 = palette_y(g);
        if (g->font_view) {
            int c = side / FONT_COLS;
            if (m.x >= px0 && m.x < px0 + side && m.y >= py0 && m.y < py0 + 16 * c) {
                g->sel_img[GFX_CHARSET] = ((int)(m.y - py0) / c) * FONT_COLS + (int)(m.x - px0) / c;
                g->focus = FOCUS_PICKER;
                return true;
            }
        } else if (g->backs_view) {
            int pw = picker_w(g), c = pw / BACKS_COLS;
            if (m.x >= px0 && m.x < px0 + pw && m.y >= py0 && m.y < py0 + pw) {
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

    // wheel over the picture: background up/down; horizontal (or Shift+wheel): tileset.
    // Over the tile picker any wheel changes the tileset; only the room changes the background.
    Vector2 wv = GetMouseWheelMoveV();
    bool over_pic = m.x >= pic_x(g) && m.x < pic_x(g) + pic_w(g) && m.y >= pic_y(g) && m.y < panel_top(g);
    bool over_pal = g->pix_src == GFX_TILES && !g->backs_view && !g->font_view &&
                    m.x >= palette_x(g) && m.x < palette_x(g) + TILESET_PX * g->scale &&
                    m.y >= palette_y(g) && m.y < palette_y(g) + TILESET_PX * g->scale;
    if ((wv.x != 0 || wv.y != 0) && (over_pic || over_pal)) {
        int dt = wv.x > 0 ? 1 : wv.x < 0 ? -1 : 0, db = wv.y > 0 ? 1 : wv.y < 0 ? -1 : 0;
        if (over_pal || (mod & GK_MOD_SHIFT)) { if (db) dt = db; db = 0; }
        ged_new_input_batch();
        ged_adjust_room(db, dt);
        return true;
    }
    // wheel over the text under the picture scrolls it (the out-of-bounds switch list
    // included - it's part of the same scroll now, not a separate pinned region)
    float wheel = wv.y;
    if (wheel != 0 && m.x < pic_w(g) && m.y >= panel_top(g) && m.y < pinned_top(g)) {
        g->scroll -= (int)wheel * 3;
        if (g->scroll < 0) g->scroll = 0;
        return true;
    }

    // attribute buttons under the source buttons (tiles view). attr_button() and
    // block_button() share the same rectangles (the block panel takes over that space
    // when a block is at the cursor), so this must not fire while that panel is up -
    // otherwise a click on "w+" etc. lands here first and w+/w-/h+/h- never see it.
    int block_bi, block_bx, block_by, block_bw, block_bh;
    bool block_panel_up = ged_block_at_cursor(&block_bi, &block_bx, &block_by, &block_bw, &block_bh) && block_bi != g->block_hidden;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && tiles_view(g) && !block_panel_up)
        for (int i = 0; i < 4; i++)
            if (CheckCollisionPointRec(m, attr_button(g, i, 0))) {
                g->sel_attr = ATTRS[i].bits;
                g->focus = FOCUS_ATTRS; g->attr_sel = i;   // the keyboard cursor follows the click
                return true;
            }

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
                for (int i = 0; i < BLOCK_BUTTONS; i++)
                    if (CheckCollisionPointRec(m, block_button(g, i, bh, z))) {
                        if (i == 4) { block_import_png(g, bi, bw, bh, NULL); return true; }
                        if (i == 5) { block_export_png(g, bi, bw, bh); return true; }
                        ged_new_input_batch();
                        ged_block_resize(dxy[i][0], dxy[i][1], dxy[i][2]);
                        return true;
                    }
                for (int i = 0; i < 4; i++)   // pass/solid/magnet/slip, below the block buttons
                    if (CheckCollisionPointRec(m, attr_button(g, i, 3))) {
                        g->sel_attr = ATTRS[i].bits;
                        g->focus = FOCUS_ATTRS; g->attr_sel = i;
                        return true;
                    }
                if (CheckCollisionPointRec(m, grid_button(g, 5, 0))) { g->block_hidden = bi; return true; }   // gfx: the pixel editor; "block" there brings this back
                if (m.x >= palette_x(g) && m.x < pixed_x(g) && m.y >= buttons_y0(g) && m.y < buttons_bottom(g)) return true;   // the rest of the grid is dimmed while the block panel is up
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
            int c = swatch_pick(g, x0, sy, sp, m);
            if (c >= 0) g->sel_colour = c;
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
                if (CheckCollisionPointRec(m, pixed_button(g, i))) {
                    g->focus = FOCUS_BUTTONS; g->btn_row = i / 2; g->btn_sel = i % 2;   // the keyboard cursor follows the click
                    pixed_button_press(g, i);
                    return true;
                }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(m, grid_button(g, 5, 0))) { g->focus = FOCUS_BUTTONS; g->btn_row = 5; g->btn_sel = 0; room_import_button(g); return true; }
            if (CheckCollisionPointRec(m, grid_button(g, 5, 1))) { g->focus = FOCUS_BUTTONS; g->btn_row = 5; g->btn_sel = 1; room_export_button(g); return true; }
        }
    }

    // a click on a switch link line (not on a thumbnail) selects that chunk
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !(mod & (GK_MOD_SHIFT | GK_MOD_CTRL)) && world_room_at(g, m) < 0 && link_click(g, m)) return true;

    // world map: hover names, right = move the thing / retarget the chunk (fires on press, as
    // before); left is held back until release so a plain click can be told from a drag onto
    // another thumbnail (same reasoning as the room picture's lpress, above).
    g->hover_room = world_room_at(g, m);
    if (g->hover_room >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        ged_new_input_batch();
        ged_map_click(g->hover_room, true, -1, -1);
        g->focus = FOCUS_WORLD;
        g->world_sel = g->hover_room;
        return true;
    }
    if (g->hover_room >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        g->wpress = true; g->wpress_room = g->hover_room;
        return true;
    }
    if (g->wpress && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        g->wpress = false;
        int release_room = world_room_at(g, m);
        ged_new_input_batch();
        if (release_room < 0) {
            // dropped off any thumbnail: cancel, same as a drag-and-drop released over nothing
        } else if (release_room != g->wpress_room) {
            // the moved-from/-to room's thumbnail (and, if either was the room on screen, the
            // picture itself - room_move() may have cancelled out of it via editor_cancel_to_plain)
            // both need redrawing from the new data, not just re-lit from the old texture.
            if (ged_room_move(g->wpress_room, release_room)) { g->world_stale = true; g->room_dirty = true; }
            g->focus = FOCUS_WORLD;
            g->world_sel = release_room;
        } else {
            // plain click: go there, cursor left where that room last had it; Ctrl-click:
            // also land the cursor where in the thumbnail the click fell
            int tx = -1, ty = -1;
            if (mod & GK_MOD_CTRL) world_click_tile(g, g->wpress_room, m, &tx, &ty);
            ged_map_click(g->wpress_room, false, tx, ty);
            g->focus = FOCUS_WORLD;
            g->world_sel = g->wpress_room;
        }
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
        // A sprite's MOVERTAB x/y offset can draw it well outside its logical tile - a plain
        // click there should still select it, the same as clicking its logical tile does
        // (Hugh: "allow a click on the sprite at that location to select that sprite even if
        // far away"). Pixel-precise, not text-cell: this is the one click path that needs to
        // know exactly where the picture put the sprite, not just which tile was clicked.
        int sprite_hit = -1;
        if (!drag && !ged_map_is_text()) {
            Vector2 m = GetMousePosition();
            if (m.x >= pic_x(g) && m.x < pic_x(g) + pic_w(g) && m.y >= pic_y(g) && m.y < pic_y(g) + pic_h(g)) {
                int px = (int)(m.x - pic_x(g)) / g->scale, py = (int)(m.y - pic_y(g)) / g->scale;
                sprite_hit = ged_sprite_hit(px, py);
            }
        }
        GKey press = { .code = GK_MOUSE, .col = g->lpress_col, .row = g->lpress_row, .button = 0 };
        GKey release = { .code = GK_MOUSE, .col = drag ? col : g->lpress_col,
                         .row = drag ? row : g->lpress_row, .button = 0, .release = true };
        ged_new_input_batch();
        if (!ged_map_is_text() && in_map_band(g->lpress_row)) ged_cancel();   // a click on the room, not on a link: the user wants out of whatever mode
        ged_key(press);
        ged_key(release);
        // The cursor moves to the click itself (just above), same as any other plain click -
        // opening the MOVERTAB editor here must not additionally warp it to the object's
        // logical tile the way ged_goto_object would (Hugh: "that also warps which i wouldn't
        // want").
        if (sprite_hit >= 0) { ged_edit_movertab(sprite_hit); return true; }
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

#define GCELL_ATTR_UNDER 2   // matches editor_types.h's ATTR_UNDER (GCell mirrors Cell; see gui_bridge.h)

static void draw_cell(const GCell *c, int px, int py, int cw, int ch, Font *font, bool have_font, int fpx) {
    if (c->bg) DrawRectangle(px, py, cw, ch, ansi(c->bg, false));
    if (c->ch && c->ch != ' ') {
        Color col = ansi(c->fg, c->attr & 1);
        int ty = py + (ch - fpx) / 2;
        if (have_font) DrawTextCodepoint(*font, c->ch, (Vector2){px, ty}, fpx, col);
        else DrawText(TextFormat("%c", c->ch), px, ty, fpx, col);
        if (c->attr & GCELL_ATTR_UNDER) DrawRectangle(px, py + ch - 1, cw, 1, col);
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

// The panel under the picture scrolls as one piece now, including the out-of-
// bounds switch list (Hugh: "can the out of bounds switches get shown in the
// main section, and all scrollable?") - only the editor's last row (macro
// overlay) stays pinned to the window bottom.
static int pinned_top(GuiState *g) { return GetScreenHeight() - pch(g); }
static int panel_rows_visible(GuiState *g) { int r = (pinned_top(g) - panel_top(g)) / pch(g); return r < 1 ? 1 : r; }

// Rows the scrolling panel has (everything under the map, out-of-bounds switches included).
static int panel_content_rows(const GCell *cells, int stride) {
    int last = GED_MAP_Y + HEIGHT_TILES - 1, count = 0;
    for (int y = GED_MAP_Y + HEIGHT_TILES; y < GED_ROWS - 1; y++) {
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
// unless F2, the hex map itself. Rows under the map all scroll together
// (`scroll`), including the out-of-bounds switch list with its clickable
// S# badges; only the editor's last row (macro overlay) is pinned to the
// window bottom.
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
    int v = 0;
    BeginScissorMode(0, panel_top(g), pic_w(g), pinned_top(g) - panel_top(g));
    for (int y = first_panel; y < GED_ROWS - 1; y++) {
        int py = panel_top(g) + (v++ - g->scroll) * pch(g);
        if (py + pch(g) <= panel_top(g) || py >= pinned_top(g)) continue;
        int sw = ged_oob_switch_at_row(y);
        int shift = sw >= 0 ? 3 : 0;        // room for the out-of-bounds switch's S# badge
        for (int x = 0; x < GED_COLS; x++) {
            if (x >= GED_MAP_X + MAP_COLS && help) continue;
            draw_cell(&cells[y * stride + x], (x + shift) * pcw(g), py, pcw(g), pch(g), &g->panel_font, g->panel_font_loaded, g->panel_font_px);
        }
        if (sw >= 0) {
            DrawRectangle(0, py, 3 * pcw(g) - 2, pch(g), (Color){60, 20, 60, 255});
            gtext(&g->panel_font, g->panel_font_loaded, TextFormat("S%X", sw), 2, py, g->panel_font_px, MAGENTA);
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
        "F2 hex map (with the backs picker: the background block's hex, two hex digits edit a tile)",
        "Ctrl-B toggle background view (picker and panel; the hex map too while F2 is on; nothing while sprites / font / screens is up)",
        "F3 switch between the .EGA and .CGA graphics     q quit     wheel over text: scroll",
        "picker: click selects; Shift+drag in the room paints it (a sprite sheet: Shift+click places",
        "        that creature); middle-click on the room picks the tile",
        "backs button: the picker shows the scenery tiles; Shift+drag paints the room's background",
        "        (BACKS.SPL, shared by every room with that background number); middle-click picks",
        "wheel over room: background; Shift+wheel / horizontal wheel, or wheel over the tile picker: tileset",
        "Ctrl-D t tile colours: blue pass-through, none solid, violet magnetic, cyan slippery, red hurts (also the",
        "        red borders in the tile picker and the hex map; a hurting tile heals in a room numbered 7 mod 8)",
        "world map: click = go there (an empty slot becomes a new room); Ctrl-click = go there and put the cursor",
        "        where you clicked; right-click = move the thing under the cursor there",
        "switch open (Ctrl-S): right-click an object or switch links it; right-drag tiles = block chunk",
        "focus: Tab cycles room / picker / editor / colours / pick+fill / attributes / buttons / world map (or click one);",
        "       arrows, hjkl move it; Enter or Space applies (picker: put tile or creature at the room cursor; editor: paint); Esc or any editor",
        "       key (r, s, o, ...) = back to the room",
        "pixel editor: click paints, right-click picks, u undoes a stroke, reset restores the original,",
        "        pick = then click any colour on screen (room, picker, world map); fill = a mode: clicks on",
        "        the image (or Space at the editor cursor) flood that region with the colour; click fill again",
        "        (or f while the pixel editor has focus) to leave",
        "font / screens buttons: the CHARSET font, and the title, story, ending, high-score and health-figure",
        "        pictures (painted on the big picture); import / export = <name>.png in the data dir, or drop a PNG",
        "        on the window: it is scaled to the image and snapped to the palette",
        "block under cursor: its tiles are shown beside the picker; click paints, w/h buttons resize, import / export",
        "        = BLOCK-<room>-<n>.png; the grid's 5th slot reads gfx (show the pixel editor instead) or block (back)",
        "info text: click a switch / chunk / object / room field line to edit it; click a link line to select its",
        "        chunk; clicking it again goes to what it toggles (object, tile run, or the other room's switch) and again",
        "        back to the chunk; a plain click on the room leaves any mode",
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
    // the overlay's colour legend, right of the editor's box
    static const struct { Color col; const char *what; bool checker; } legend[] = {
        { {0, 0, 0, 0},      "Overlay colours", false },
        { MAGENTA,           "switch marker S#", false },
        { ORANGE,            "block toggle: the tile run it flips", false },
        { {255, 150, 0, 255}, "memory chunk on an exit: arrows to its off / on rooms", false },
        { {96, 160, 255, 255}, "object toggle: line to the object", false },
        { SKYBLUE,           "block object outline; map: neighbour room", false },
        { GREEN,             "switch toggle; other room: arrow to its map room", false },
        { {150, 80, 220, 255}, "a switch elsewhere toggling one here: arrow back", false },
        { YELLOW,            "sprite object; goto emphasis; map: this room", false },
        { GOLD,              "keyboard focus", false },
        { WHITE,             "mouse hover on the map; thick = the hot switch", false },
        // Ctrl-D t tile classes, one row per class with its actual tint colour next to it
        // (Hugh: "show each class in the list of colors with the same colors next to them
        // (incl the transparent)") - solid gets the sprite editor's transparent checker since
        // it's the one class that isn't actually tinted (see draw_overlay's tile-tint loop).
        { {0, 120, 255, 255}, "Ctrl-D t tile classes: pass-through", false },
        { {200, 200, 200, 255}, "  solid (no tint - shown as-is)", true },
        { {200, 60, 255, 255}, "  magnetic (no web)", false },
        { {0, 230, 230, 255}, "  slippery", false },
    };
    int lx = ox + (x1 - x0 + 3) * hcw, nleg = (int)(sizeof legend / sizeof legend[0]);
    if (lx + 52 * hcw <= ox + w && nleg <= rows)
        for (int i = 0; i < nleg; i++) {
            int yy = oy + i * hch;
            bool has_swatch = legend[i].col.a || legend[i].checker;
            if (legend[i].checker) draw_transparent_swatch(lx, yy + 2, hch - 4);
            else if (legend[i].col.a) DrawRectangle(lx, yy + 2, hch - 4, hch - 4, legend[i].col);
            gtext(font, have, legend[i].what, lx + (has_swatch ? hch : 0), yy + (hch - fpx) / 2, fpx, i == 0 ? GOLD : RAYWHITE);
        }
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
    // (Hugh: "when on shows spidey at cursor and when off doesnt show spidey, just
    // the cursor square" - Ctrl-D c, default on).
    if (g->spidey_loaded && ged_show_spidey()) {
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
static int sheet_cols(GfxSource src) { return src == GFX_SCENERY ? BACKS_COLS : src == GFX_CHARSET ? FONT_COLS : 8; }

// The chosen screen as a texture (the big view and the thumbnail draw it).
static void screen_tex_build(GuiState *g) {
    static uint8_t buf[320 * 200 * 4];
    int w, h;
    render_gfx_size(g->pix_src, &w, &h);
    render_gfx_rgba(g->rctx, g->pix_src, 0, buf);
    Image img = { .data = buf, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    if (g->scr_loaded) UnloadTexture(g->scr_tex);
    g->scr_tex = LoadTextureFromImage(img);
    SetTextureFilter(g->scr_tex, TEXTURE_FILTER_POINT);
    g->scr_loaded = true; g->scr_src = g->pix_src; g->scr_stale = false;
}

// The dark 2x2 checker the pixel editor uses for a sprite's transparent colour (see
// draw_swatches) - reused wherever a swatch needs to say "no colour here" instead of a flat one.
static void draw_transparent_swatch(int x, int y, int size) {
    DrawRectangle(x, y, size, size, (Color){50, 50, 50, 255});
    DrawRectangle(x, y, size / 2, size / 2, (Color){70, 70, 70, 255});
    DrawRectangle(x + size / 2, y + size / 2, size - size / 2, size - size / 2, (Color){70, 70, 70, 255});
}

// The palette swatches (8 across, 2 rows; CGA sprites add a 5th "transparent"
// one), the selected colour outlined; shared by the pixel editor and screen mode.
static void draw_swatches(GuiState *g, int x0, int sy, int sp) {
    int key = render_gfx_key(g->rctx, g->pix_src);
    const uint8_t (*pal)[3] = render_palette(g->rctx);
    int nsw = key >= render_colours(g->rctx) ? key + 1 : render_colours(g->rctx);
    for (int c = 0; c < nsw; c++) {
        int sx = x0 + (c % 8) * sp, yy = sy + (c / 8) * sp;
        if (c == key) {                          // the game draws this colour as transparent in sprites
            draw_transparent_swatch(sx, yy, sp);
            gtext(&g->font, g->font_loaded, "T", sx + 2, yy, g->font_px, RAYWHITE);
        } else {
            DrawRectangle(sx, yy, sp, sp, (Color){pal[c][0], pal[c][1], pal[c][2], 255});
        }
        if (c == g->sel_colour) DrawRectangleLinesEx((Rectangle){sx, yy, sp, sp}, 2, WHITE);
    }
    if (g->focus == FOCUS_COLOURS) DrawRectangleLinesEx((Rectangle){x0 - 2, sy - 2, 8 * sp + 4, SWATCH_ROWS * sp + 4}, 2, GOLD);
}
// The swatch under the mouse, or -1.
static int swatch_pick(GuiState *g, int x0, int sy, int sp, Vector2 m) {
    if (m.x < x0 || m.x >= x0 + 8 * sp || m.y < sy || m.y >= sy + SWATCH_ROWS * sp) return -1;
    int c = ((int)(m.y - sy) / sp) * 8 + (int)(m.x - x0) / sp;
    int key = render_gfx_key(g->rctx, g->pix_src);
    return c < render_colours(g->rctx) || c == key ? c : -1;
}

static const char *SMODE_BUTTONS[] = { "pick", "fill", "import", "export", "room" };

// Screen mode: the picture, and the side panel.
static void draw_screen_mode(GuiState *g) {
    int z, ox, oy, w, h, fs = g->font_px, px0 = smode_panel_x(g);
    screen_geom(g, &z, &ox, &oy, &w, &h);
    if (!render_gfx_available(g->rctx, g->pix_src))
        gtext(&g->font, g->font_loaded, TextFormat("%s is not in the data dir", render_gfx_name(g->rctx, g->pix_src)), PAD, PAD, fs, ORANGE);
    else if (g->scr_loaded && g->scr_src == g->pix_src)
        DrawTexturePro(g->scr_tex, (Rectangle){0, 0, w, h}, (Rectangle){ox, oy, w * z, h * z}, (Vector2){0, 0}, 0, WHITE);
    DrawRectangleLinesEx((Rectangle){ox - 1, oy - 1, w * z + 2, h * z + 2}, 1, GRAY);
    if (g->ecx >= w) g->ecx = w - 1;
    if (g->ecy >= h) g->ecy = h - 1;
    DrawRectangleLinesEx((Rectangle){ox + g->ecx * z - 1, oy + g->ecy * z - 1, z + 2, z + 2}, 1, WHITE);

    DrawRectangle(px0, 0, GetScreenWidth() - px0, GetScreenHeight(), (Color){22, 22, 28, 255});
    gtext(&g->font, g->font_loaded, TextFormat("%s graphics  %dx%d  zoom %d", g->cga ? "CGA" : "EGA", w, h, z), px0 + PAD, PAD, fs, RAYWHITE);
    if (g->focus == FOCUS_EDITOR) DrawRectangleLinesEx((Rectangle){ox - 3, oy - 3, w * z + 6, h * z + 6}, 2, GOLD);
    if (g->focus == FOCUS_PICKER) {
        Rectangle r0 = smode_list_row(g, 0), rn = smode_list_row(g, NSCREENS - 1);
        DrawRectangleLinesEx((Rectangle){r0.x - 2, r0.y - 2, r0.width + 4, rn.y + rn.height - r0.y + 4}, 2, GOLD);
    }
    for (int i = 0; i < NSCREENS; i++) {
        Rectangle r = smode_list_row(g, i);
        bool on = SCREENS[i] == g->pix_src, avail = render_gfx_available(g->rctx, SCREENS[i]);
        int sw, sh;
        render_gfx_size(SCREENS[i], &sw, &sh);
        char name[32];
        snprintf(name, sizeof name, "%s", render_gfx_name(g->rctx, SCREENS[i]));
        *strrchr(name, '.') = '\0';
        DrawRectangleRec(r, on ? (Color){90, 70, 20, 255} : (Color){40, 40, 48, 255});
        gtext(&g->font, g->font_loaded, TextFormat("%-9s %dx%d", name, sw, sh), (int)r.x + 4, (int)r.y + ((int)r.height - fs) / 2, fs, avail ? RAYWHITE : GRAY);
    }
    draw_swatches(g, px0 + PAD, smode_swatch_y(g), smode_swatch_px(g));
    for (int i = 0; i < 5; i++) {
        Rectangle rb = smode_button(g, i);
        bool on = (i == 0 && g->eyedrop) || (i == 1 && g->fill_armed);
        bool kbd = g->focus == FOCUS_BUTTONS && i == g->btn_sel % 5;
        DrawRectangleRec(rb, on ? (Color){90, 70, 20, 255} : (Color){50, 50, 60, 255});
        DrawRectangleLinesEx(rb, kbd ? 2 : 1, kbd || on ? GOLD : GRAY);
        int tw = text_w(&g->font, g->font_loaded, SMODE_BUTTONS[i], fs);
        gtext(&g->font, g->font_loaded, SMODE_BUTTONS[i], (int)rb.x + ((int)rb.width - tw) / 2, (int)rb.y + 2, fs, RAYWHITE);
    }
    int ty = smode_text_y(g), lh = ch(g);
    bool dirty = false;
    for (int i = 0; i < GFX_SOURCES; i++) dirty |= g->gfx_dirty[i];
    int key = render_gfx_key(g->rctx, g->pix_src);
    gtext(&g->font, g->font_loaded, g->sel_colour == key ? TextFormat("colour %d = transparent", g->sel_colour) : TextFormat("colour %d", g->sel_colour), px0 + PAD, ty, fs, RAYWHITE);
    gtext(&g->font, g->font_loaded, "click / drag paints", px0 + PAD, ty + lh, fs, GRAY);
    gtext(&g->font, g->font_loaded, "right-click picks a colour", px0 + PAD, ty + 2 * lh, fs, GRAY);
    gtext(&g->font, g->font_loaded, "Tab: list/colours/buttons/picture", px0 + PAD, ty + 3 * lh, fs, GRAY);
    gtext(&g->font, g->font_loaded, TextFormat("u undo   F3 %s   Esc back", g->cga ? "EGA" : "CGA"), px0 + PAD, ty + 4 * lh, fs, GRAY);
    gtext(&g->font, g->font_loaded, dirty ? "saving..." : g->saved_flash > 0 ? TextFormat("saved %s", g->saved_what) : "", px0 + PAD, ty + 5 * lh, fs, dirty ? ORANGE : GREEN);
    if (g->err_flash > 0) gtext(&g->font, g->font_loaded, g->err, px0 + PAD, ty + 6 * lh, fs, g->err_col);
}

// Screen mode mouse: paint on the picture, or use the panel; nothing else exists.
static bool screen_mode_mouse(GuiState *g) {
    Vector2 m = GetMousePosition();
    int sx, sy;
    g->pix_op = true;
    if (g->eyedrop && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int c = colour_under_mouse(g);
        if (c >= 0) g->sel_colour = c;
        g->eyedrop = false;
        return true;
    }
    if (mouse_screen_px(g, &sx, &sy) && render_gfx_available(g->rctx, g->pix_src)) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { g->sel_colour = render_gfx_pixel(g->rctx, g->pix_src, 0, sx, sy); return true; }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            g->ecx = sx; g->ecy = sy; g->focus = FOCUS_EDITOR;
            if (g->fill_armed) { pix_fill(g, 0, sx, sy); return true; }
            g->pix_painting = true;
            pix_undo_push(g, g->pix_src, 0);
        }
        if (g->pix_painting && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && render_gfx_pixel(g->rctx, g->pix_src, 0, sx, sy) != g->sel_colour) {
            render_gfx_set_pixel(g->rctx, g->pix_src, 0, sx, sy, g->sel_colour);
            gfx_touched(g, g->pix_src);
            return true;
        }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) g->pix_painting = false;
    if (g->pix_painting || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    for (int i = 0; i < NSCREENS; i++)
        if (CheckCollisionPointRec(m, smode_list_row(g, i))) {
            g->screen_src = g->pix_src = SCREENS[i];
            g->scr_stale = true;
            g->focus = FOCUS_PICKER;
            clamp_colour(g);
            return true;
        }
    int c = swatch_pick(g, smode_panel_x(g) + PAD, smode_swatch_y(g), smode_swatch_px(g), m);
    if (c >= 0) { g->sel_colour = c; g->focus = FOCUS_COLOURS; return true; }
    for (int i = 0; i < 5; i++)
        if (CheckCollisionPointRec(m, smode_button(g, i))) { g->focus = FOCUS_BUTTONS; g->btn_sel = i; smode_button_press(g, i); return true; }
    return false;
}

static void smode_button_press(GuiState *g, int i) {
    g->pix_op = true;
    if (i == 0) { g->eyedrop = !g->eyedrop; g->fill_armed = false; }
    else if (i == 1) { g->fill_armed = !g->fill_armed; g->eyedrop = false; }
    else if (i == 2) import_png(g, NULL);
    else if (i == 3) export_png(g);
    else set_view(g, VIEW_TILES);
}
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
    if (g->backs_view) {                              // SCENERY: 16 x 16 tiles at the tile zoom
        int pw = picker_w(g), c = pw / BACKS_COLS, sel = g->sel_img[GFX_SCENERY];
        DrawRectangle(x0, y0, pw, pw, (Color){35, 45, 35, 255});
        if (g->sheet_loaded[GFX_SCENERY])
            DrawTexturePro(g->sheet_tex[GFX_SCENERY], (Rectangle){0, 0, g->sheet_tex[GFX_SCENERY].width, g->sheet_tex[GFX_SCENERY].height},
                           (Rectangle){x0, y0, pw, pw}, (Vector2){0, 0}, 0, WHITE);
        for (int i = 0; i <= BACKS_COLS; i++) {
            DrawLine(x0 + i * c, y0, x0 + i * c, y0 + pw, (Color){80, 80, 80, 255});
            DrawLine(x0, y0 + i * c, x0 + pw, y0 + i * c, (Color){80, 80, 80, 255});
        }
        DrawRectangleLinesEx((Rectangle){x0 + (sel % BACKS_COLS) * c, y0 + (sel / BACKS_COLS) * c, c, c}, 2, GOLD);
        if (g->focus == FOCUS_PICKER) DrawRectangleLinesEx((Rectangle){x0 - 2, y0 - 2, pw + 4, pw + 4}, 2, GOLD);
    } else if (g->font_view) {                        // CHARSET: 8 x 16 tiles, drawn at the tile zoom
        int c = side / FONT_COLS, sel = g->sel_img[GFX_CHARSET], rows = 16;
        DrawRectangle(x0, y0, side, c * rows, (Color){45, 45, 35, 255});
        if (!render_gfx_available(g->rctx, GFX_CHARSET))
            gtext(&g->font, g->font_loaded, TextFormat("%s is not in the data dir", render_gfx_name(g->rctx, GFX_CHARSET)), x0 + 4, y0 + 4, fs, ORANGE);
        if (g->sheet_loaded[GFX_CHARSET])
            DrawTexturePro(g->sheet_tex[GFX_CHARSET], (Rectangle){0, 0, g->sheet_tex[GFX_CHARSET].width, g->sheet_tex[GFX_CHARSET].height},
                           (Rectangle){x0, y0, side, c * rows}, (Vector2){0, 0}, 0, WHITE);
        for (int i = 0; i <= FONT_COLS; i++) DrawLine(x0 + i * c, y0, x0 + i * c, y0 + c * rows, (Color){80, 80, 80, 255});
        for (int i = 0; i <= rows; i++) DrawLine(x0, y0 + i * c, x0 + side, y0 + i * c, (Color){80, 80, 80, 255});
        DrawRectangleLinesEx((Rectangle){x0 + (sel % FONT_COLS) * c, y0 + (sel / FONT_COLS) * c, c, c}, 2, GOLD);
        if (g->focus == FOCUS_PICKER) DrawRectangleLinesEx((Rectangle){x0 - 2, y0 - 2, side + 4, c * rows + 4}, 2, GOLD);
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
        // Ctrl-D t: a red border round each run of hurting tiles (tile_hurts) in a picker row -
        // unless the attribute being painted is slippery, which never hurts
        if (ged_show_tiles() && g->sel_attr != 0xC0)
            for (int row = 0; row < 8; row++)
                for (int c = 0; c < 8; c++) {
                    if (!tile_hurts((uint8_t)(row * 8 + c))) continue;
                    int run = 1;
                    while (c + run < 8 && tile_hurts((uint8_t)(row * 8 + c + run))) run++;
                    DrawRectangleLinesEx((Rectangle){x0 + c * s, y0 + row * s, run * s, s}, 2, HURT_COLOUR);
                    c += run - 1;
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
    int ly = pixed_text_y(g) + 5 * ch(g), lh = ch(g), lx = pixed_x(g);   // under the pixel editor's own lines
    const char *lines[] = {
        g->backs_view    ? TextFormat("background %d  scenery tile 0x%02x", cur_backs(), g->sel_img[GFX_SCENERY])
        : g->font_view   ? TextFormat("%s  tile 0x%02x", render_gfx_name(g->rctx, GFX_CHARSET), g->sel_img[GFX_CHARSET])
        : tiles_view(g)  ? TextFormat("tileset %d  selected 0x%02x", g->pal_offset, g->sel_tile | g->sel_attr)
                         : TextFormat("%s  frame 0x%02x", render_gfx_name(g->rctx, g->pix_src), g->sel_img[g->pix_src]),
        g->backs_view    ? TextFormat("shown by rooms %s", backs_users(cur_backs())) : "",
        g->cga ? "CGA graphics   Ctrl-? help" : "EGA graphics   Ctrl-? help",
    };
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++)
        gtext(&g->font, g->font_loaded, lines[i], lx, ly + (int)i * lh, fs, i == 0 ? RAYWHITE : GRAY);

    if (g->err_flash > 0)
        gtext(&g->font, g->font_loaded, g->err, lx, ly + (int)(sizeof lines / sizeof lines[0]) * lh, fs, g->err_col);
    else if (g->saved_flash > 0)
        gtext(&g->font, g->font_loaded, TextFormat("saved %s", g->saved_what), lx, ly + (int)(sizeof lines / sizeof lines[0]) * lh, fs, GREEN);
}

// Buttons under the editor: reset, then one per view, then "block" while a
// block object's panel has been closed with "gfx".
// Button of the grid under the picker: two columns, rows 0-3 the sources
// (reset tiles / sprites backs / font screens / import export), row 4 "block"
// while a block panel is hidden.
static Rectangle grid_button(GuiState *g, int row, int col) {
    int bw = (TILESET_PX * g->scale - PAD) / 2;
    return (Rectangle){ palette_x(g) + col * (bw + PAD), buttons_y0(g) + row * (ch(g) + 8), bw, ch(g) + 4 };
}
static Rectangle pixed_button(GuiState *g, int i) { return grid_button(g, i / 2, i % 2); }
static int pixed_button_count(GuiState *g) { return g->block_hidden >= 0 ? 9 : 8; }

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
    } else if (i <= 5) set_view(g, i == 1 ? VIEW_TILES : i == 2 ? VIEW_SPRITES : i == 3 ? VIEW_BACKS : i == 4 ? VIEW_FONT : VIEW_SCREENS);
    else if (i == 6) import_png(g, NULL);
    else if (i == 7) export_png(g);
    else if (g->block_hidden >= 0) g->block_hidden = -1;   // block: show its panel again
}

/* ----------------------------------------------------------- PNG import / export */

// A block object's picture: <data dir>/BLOCK-<room>-<object>.png.
static const char *block_png_path(GuiState *g, int idx) {
    static char p[1400];
    snprintf(p, sizeof p, "%s/BLOCK-%02x-%d.png", g->data_dir ? g->data_dir : ".", ged_room(), idx);
    return p;
}

// The current room's full editable state (tiles, objects, switches, metadata) as
// dumpRoom's `display`-format text: <data dir>/ROOM-<id>.txt (see ged_room_export_text /
// ged_room_import_text, room_export_text / room_import_text in room.h).
static const char *room_txt_path(GuiState *g) {
    static char p[1400];
    snprintf(p, sizeof p, "%s/ROOM-%02x.txt", g->data_dir ? g->data_dir : ".", ged_room());
    return p;
}

static void room_export_button(GuiState *g) {
    const char *p = room_txt_path(g);
    if (ged_room_export_text(p)) gui_note(g, TextFormat("exported %s", p));
    else gui_error(g, TextFormat("could not write %s", p));
}

static void room_import_button(GuiState *g) {
    const char *p = room_txt_path(g);
    if (!FileExists(p)) { gui_error(g, TextFormat("no %s to import", p)); return; }
    if (ged_room_import_text(p)) { gui_note(g, TextFormat("imported %s", p)); g->room_dirty = true; }
    else gui_error(g, TextFormat("could not read %s", p));
}

// The block's tiles rendered as one picture (w*8 x h*8), from the room's tileset.
static void block_export_png(GuiState *g, int idx, int w, int h) {
    uint8_t *buf = calloc((size_t)w * 8 * h * 8, 4), tile[8 * 8 * 4];
    int toff = ged_rooms()->rooms[ged_room()].data.tile_offset;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            render_gfx_rgba(g->rctx, GFX_TILES, (ged_block_tile(idx, x, y) & 0x3f) + toff * 16, tile);
            for (int r = 0; r < 8; r++) memcpy(buf + (((size_t)(y * 8 + r) * w * 8) + x * 8) * 4, tile + r * 8 * 4, 8 * 4);
        }
    Image img = { .data = buf, .width = w * 8, .height = h * 8, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    const char *p = block_png_path(g, idx);
    if (ExportImage(img, p)) gui_note(g, TextFormat("exported %s", p));
    else gui_error(g, TextFormat("could not write %s", p));
    free(buf);
    g->pix_op = true;
}

// A picture becomes block tiles: scaled to w*8 x h*8, then each 8x8 cell gets
// the tile of the room's tileset that matches it best (least squared RGB
// difference), with the current attribute bits. One room undo entry.
static void block_import_png(GuiState *g, int idx, int w, int h, const char *path) {
    if (path == NULL) path = block_png_path(g, idx);
    g->pix_op = true;
    if (!FileExists(path)) { gui_error(g, TextFormat("no %s to import", path)); return; }
    Image img = LoadImage(path);
    if (img.data == NULL) { gui_error(g, TextFormat("could not read %s", path)); return; }
    if (img.width != w * 8 || img.height != h * 8) ImageResizeNN(&img, w * 8, h * 8);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    static uint8_t tiles[64][8 * 8 * 4];
    int toff = ged_rooms()->rooms[ged_room()].data.tile_offset;
    for (int t = 0; t < 64; t++) render_gfx_rgba(g->rctx, GFX_TILES, t + toff * 16, tiles[t]);
    ged_paint_begin();
    const uint8_t *px = img.data;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            long best = -1; int best_t = 0;
            for (int t = 0; t < 64; t++) {
                long d = 0;
                for (int r = 0; r < 8 && (best < 0 || d < best); r++)
                    for (int c = 0; c < 8; c++) {
                        const uint8_t *a = px + (((size_t)(y * 8 + r) * w * 8) + x * 8 + c) * 4, *b = tiles[t] + (r * 8 + c) * 4;
                        long dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
                        d += dr * dr + dg * dg + db * db;
                    }
                if (best < 0 || d < best) { best = d; best_t = t; }
            }
            ged_block_set_tile(idx, x, y, (uint8_t)(best_t | g->sel_attr));
        }
    UnloadImage(img);
    g->paint_dirty = true; g->save_timer = 0.4f;
    g->room_dirty = true;
    gui_note(g, TextFormat("imported %s", path));
}

// <data dir>/<FILE>.png for a screen, <FILE>-<index>.png for one image of a
// sheet (BLOCKS-012.png is tile 0x12 of BLOCKS.EGA or .CGA).
static const char *png_path(GuiState *g) {
    static char p[1400];
    char base[32];
    snprintf(base, sizeof base, "%s", render_gfx_name(g->rctx, g->pix_src));
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    const char *dir = g->data_dir ? g->data_dir : ".";
    if (render_gfx_is_screen(g->pix_src)) snprintf(p, sizeof p, "%s/%s.png", dir, base);
    else snprintf(p, sizeof p, "%s/%s-%03x.png", dir, base, pixed_index(g));
    return p;
}

static void export_png(GuiState *g) {
    static uint8_t buf[320 * 200 * 4];
    int w, h;
    g->pix_op = true;
    if (pixed_locked(g)) { gui_error(g, "tile 0 is always blank: nothing to export"); return; }
    pixed_dims(g, &w, &h);
    render_gfx_rgba(g->rctx, g->pix_src, pixed_index(g), buf);
    Image img = { .data = buf, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    const char *p = png_path(g);
    if (ExportImage(img, p)) gui_note(g, TextFormat("exported %s", p));
    else gui_error(g, TextFormat("could not write %s", p));
    g->pix_op = true;
}

// Replace the current image with a PNG: scaled to the image's size (nearest
// neighbour) and each pixel snapped to the nearest palette colour; transparent
// pixels become the sprite key colour. NULL path = the export name.
static void import_png(GuiState *g, const char *path) {
    if (path == NULL) path = png_path(g);
    g->pix_op = true;
    if (pixed_locked(g)) { gui_error(g, "tile 0 is always blank: nothing to import into"); return; }
    if (!FileExists(path)) { gui_error(g, TextFormat("no %s to import", path)); return; }
    Image img = LoadImage(path);
    if (img.data == NULL) { gui_error(g, TextFormat("could not read %s", path)); return; }
    int w, h, idx = pixed_index(g);
    pixed_dims(g, &w, &h);
    if (img.width != w || img.height != h) ImageResizeNN(&img, w, h);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const uint8_t (*pal)[3] = render_palette(g->rctx);
    int n = render_colours(g->rctx), key = render_gfx_key(g->rctx, g->pix_src);
    pix_undo_push(g, g->pix_src, idx);
    const uint8_t *px = img.data;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++, px += 4) {
            int c = key;
            if (key < 0 || px[3] >= 128) {
                long best = -1;
                for (int i = 0; i < n; i++) {
                    if (i == key) continue;                          // opaque pixels never land on the key colour
                    long dr = px[0] - pal[i][0], dg = px[1] - pal[i][1], db = px[2] - pal[i][2], d = dr * dr + dg * dg + db * db;
                    if (best < 0 || d < best) { best = d; c = i; }
                }
            }
            render_gfx_set_pixel(g->rctx, g->pix_src, idx, x, y, c);
        }
    UnloadImage(img);
    gfx_touched(g, g->pix_src);
    gui_note(g, TextFormat("imported %s", path));
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
    g->err_col = RED;
    fprintf(stderr, "gui: %s\n", msg);
}
static void gui_note(GuiState *g, const char *msg) {
    snprintf(g->err, sizeof g->err, "%s", msg);
    g->err_flash = 3.0f;
    g->err_col = GREEN;
}

static void backs_touched(GuiState *g) {
    g->backs_dirty = true; g->gfx_timer = 0.6f; g->world_pending = true;
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
    ged_set_render(g->rctx);
    g->cga = cga;
    render_show_player(g->rctx, false);
    g->pix_undo_n = 0; g->pix_seq = 0;                   // the undo entries belong to the other files
    g->pal_offset = -1;                                   // palette texture
    for (int i = 0; i < GFX_SOURCES; i++) g->sheet_stale[i] = true;
    g->scr_stale = true;
    spidey_build(g);
    g->world_stale = true;
    clamp_colour(g);
    g->room_dirty = true;
}

static void gfx_touched(GuiState *g, GfxSource src) {
    g->gfx_dirty[src] = true; g->gfx_timer = 0.6f;
    if (src == GFX_TILES || src == GFX_SCENERY || src == GFX_SPIDS1 || src == GFX_SPIDS2) g->world_pending = true;
    if (render_gfx_is_screen(src)) g->scr_stale = true;
    else if (src == GFX_TILES) g->pal_offset = -1;         // rebuild the palette texture
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
static void saved_add(GuiState *g, const char *name) {   // append to the "saved ..." indicator's file list
    size_t n = strlen(g->saved_what);
    snprintf(g->saved_what + n, sizeof g->saved_what - n, "%s%s", n ? ", " : "", name);
}

static bool gfx_save_all(GuiState *g) {
    bool any = false;
    g->saved_what[0] = '\0';
    for (int i = 0; i < GFX_SOURCES; i++) {
        if (!g->gfx_dirty[i]) continue;
        g->gfx_dirty[i] = false;
        if (render_gfx_save(g->rctx, (GfxSource)i)) { any = true; saved_add(g, render_gfx_name(g->rctx, (GfxSource)i)); }
        else gui_error(g, TextFormat("could not write %s", render_gfx_name(g->rctx, (GfxSource)i)));
        if (i == GFX_SPIDS1) spidey_build(g);
    }
    if (g->backs_dirty) {
        g->backs_dirty = false;
        if (render_backs_save(g->rctx)) { any = true; saved_add(g, "BACKS.SPL"); }
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
    static int stack[320 * 200][2];                          // the largest image; each pixel is pushed at most once
    int n = 0;
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
// w+ w- / h+ h- / gfx import / export: two per row under the tile picker, in
// the space the attribute buttons use when no block panel is up.
static Rectangle block_button(GuiState *g, int i, int h, int z) {
    (void)h; (void)z;
    int bw = (TILESET_PX * g->scale - PAD) / 2;
    return (Rectangle){ palette_x(g) + (i % 2) * (bw + PAD), swatch_y(g) + (i / 2) * (ch(g) + 8), bw, ch(g) + 4 };
}

// Attribute buttons (pass/solid/magnet/slip): shared by the ordinary tiles view
// (row_offset 0) and the block panel (row_offset 3, below its own 3 rows of
// buttons - see attr_button/block_button) so a block's tiles can still be
// painted with a chosen attribute.
static void draw_attr_buttons(GuiState *g, int row_offset) {
    int fs = g->font_px;
    for (int i = 0; i < 4; i++) {
        Rectangle rb = attr_button(g, i, row_offset);
        bool on = ATTRS[i].bits == g->sel_attr;
        bool kbd = g->focus == FOCUS_ATTRS && i == g->attr_sel % 4;
        DrawRectangleRec(rb, on ? (Color){90, 70, 20, 255} : (Color){50, 50, 60, 255});
        DrawRectangleLinesEx(rb, kbd ? 2 : 1, kbd || on ? GOLD : GRAY);
        int tw = text_w(&g->font, g->font_loaded, ATTRS[i].name, fs);
        gtext(&g->font, g->font_loaded, ATTRS[i].name, (int)rb.x + ((int)rb.width - tw) / 2, (int)rb.y + 2, fs, RAYWHITE);
    }
}

// The colour-code legend matching Ctrl-D t's room tint (blue pass, grey solid, violet magnet,
// cyan slip), so the tint can be read at a glance: "type: X" plus one swatch+label per class.
// Returns the legend's height in pixels, so a caller stacking more text below it knows how far
// to push down.
static int draw_attr_legend(GuiState *g, int lx, int ly) {
    int fs = g->font_px, y0 = ly;
    gtext(&g->font, g->font_loaded, TextFormat("type: %s", ATTRS[g->sel_attr >> 6].name), lx, ly, fs, RAYWHITE);
    ly += ch(g) + 4;
    // Two columns, three rows, so the fifth entry (hurt) fits in the height the four classes
    // used to take: the button grid below is pinned and would cover a fifth row.
    int colw = text_w(&g->font, g->font_loaded, "magnet", fs) + fs + 12;
    for (int i = 0; i < 5; i++) {
        int cx = lx + (i / 3) * colw, cy = ly + (i % 3) * (fs + 2);
        // Solid gets the pixel editor's transparent checker, not a flat colour, since it's
        // the one class Ctrl-D t doesn't actually tint (Hugh: "make the color on the side
        // show the transparent background you used for sprite editing").
        if (i == 4) DrawRectangle(cx, cy, fs, fs, HURT_COLOUR);   // the red Ctrl-D t lays over a hurting tile of any class (tile_hurts)
        else if (ATTRS[i].bits == ATTR_SOLID) draw_transparent_swatch(cx, cy, fs);
        else DrawRectangle(cx, cy, fs, fs, ATTR_COLOUR[i]);
        DrawRectangleLines(cx, cy, fs, fs, GRAY);
        gtext(&g->font, g->font_loaded, i == 4 ? "hurt" : ATTRS[i].name, cx + fs + 4, cy, fs, RAYWHITE);
    }
    return ly + 3 * (fs + 2) - y0;
}

// The block object under the cursor: its tile grid, click paints the picked
// tile, right click picks; buttons grow / shrink it.
static void draw_block_editor(GuiState *g, int idx, int bx, int by, int w, int h) {
    int z = block_zoom(g, w, h), x0 = pixed_x(g), y0 = palette_y(g), fs = g->font_px;
    Rectangle src_cell = {0, 0, 8, 8};
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int raw = ged_block_tile(idx, x, y);
            int t = raw & 0x3f;
            src_cell.x = (t % 8) * 8; src_cell.y = (t / 8) * 8;
            DrawTexturePro(g->pal_tex, src_cell, (Rectangle){x0 + x * z, y0 + y * z, z, z}, (Vector2){0, 0}, 0, WHITE);
            // Ctrl-D t: tint by class, same as the room picture (draw_overlay) - hurting
            // tiles red, solid and empty no overlay so it only calls out pass/magnet/slip.
            if (ged_show_tiles() && raw != 0 && tile_hurts((uint8_t)raw)) {
                Color c = HURT_COLOUR; c.a = HURT_TINT_ALPHA;
                DrawRectangle(x0 + x * z, y0 + y * z, z, z, c);
            } else if (ged_show_tiles() && raw != 0 && (raw >> 6) != 1) {
                Color c = ATTR_COLOUR[raw >> 6]; c.a = TILE_TINT_ALPHA[raw >> 6];
                DrawRectangle(x0 + x * z, y0 + y * z, z, z, c);
            }
        }
    for (int i = 0; i <= w; i++) DrawLine(x0 + i * z, y0, x0 + i * z, y0 + h * z, (Color){90, 90, 90, 255});
    for (int i = 0; i <= h; i++) DrawLine(x0, y0 + i * z, x0 + w * z, y0 + i * z, (Color){90, 90, 90, 255});
    if (g->focus == FOCUS_EDITOR) {
        if (g->ecx >= w) g->ecx = w - 1;
        if (g->ecy >= h) g->ecy = h - 1;
        DrawRectangleLinesEx((Rectangle){x0 + g->ecx * z, y0 + g->ecy * z, z, z}, 2, WHITE);
        DrawRectangleLinesEx((Rectangle){x0 - 2, y0 - 2, w * z + 4, h * z + 4}, 2, GOLD);
    }
    const char *names[BLOCK_BUTTONS] = { "w+", "w-", "h+", "h-", "import", "export" };
    for (int i = 0; i < BLOCK_BUTTONS; i++) {
        Rectangle rb = block_button(g, i, h, z);
        DrawRectangleRec(rb, (Color){50, 50, 60, 255});
        DrawRectangleLinesEx(rb, 1, GRAY);
        int tw = text_w(&g->font, g->font_loaded, names[i], fs);
        gtext(&g->font, g->font_loaded, names[i], (int)rb.x + ((int)rb.width - tw) / 2, (int)rb.y + 2, fs, RAYWHITE);
    }
    // Attribute buttons (pass/solid/magnet/slip), below the block panel's own buttons - a
    // block's tiles are painted with the current attribute too (Hugh: "can the pass/solid/
    // buttons show up below w-/+h+/-import/export").
    draw_attr_buttons(g, 3);
    int ty = y0 + h * z + PAD;                        // the text goes under the block grid; the buttons are under the picker
    gtext(&g->font, g->font_loaded, TextFormat("block O%d at %d,%d  %dx%d", idx, bx, by, w, h), x0, ty, fs, RAYWHITE);
    // The colour-code legend, right under that line (Hugh: "the color code can be below the
    // block tile editor view 'block 01 at 26,5', but only in block edit mode").
    int legend_h = draw_attr_legend(g, x0, ty + ch(g));
    ty += ch(g) + legend_h + PAD;
    gtext(&g->font, g->font_loaded, "click paints, right-click picks; w/h resize", x0, ty, fs, GRAY);
    gtext(&g->font, g->font_loaded, "gfx (in the grid below) swaps to the pixel editor", x0, ty + ch(g), fs, GRAY);
    gtext(&g->font, g->font_loaded, TextFormat("import / export %s (best tiles)", GetFileName(block_png_path(g, idx))), x0, ty + 2 * ch(g), fs, GRAY);
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
    draw_swatches(g, x0, swatch_y(g), swatch_px(g));
    int ty = pixed_text_y(g), fs = g->font_px;
    bool dirty = g->backs_dirty;
    for (int i = 0; i < GFX_SOURCES; i++) dirty |= g->gfx_dirty[i];
    gtext(&g->font, g->font_loaded,
          g->pix_src == GFX_TILES ? TextFormat("tile 0x%02x  colour %d", idx | g->sel_attr, g->sel_colour)
          : g->pix_src == GFX_SCENERY ? TextFormat("scenery tile 0x%02x  colour %d", idx, g->sel_colour)
          : g->pix_src == GFX_CHARSET ? TextFormat("font tile 0x%02x  colour %d", idx, g->sel_colour)
          : render_gfx_is_screen(g->pix_src) ? TextFormat("%s %dx%d  colour %d", render_gfx_name(g->rctx, g->pix_src), w, h, g->sel_colour)
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
        bool kbd = g->focus == FOCUS_TOOLS && g->tool_sel == 0;
        DrawRectangleRec(rb, g->eyedrop ? (Color){90, 70, 20, 255} : (Color){50, 50, 60, 255});
        DrawRectangleLinesEx(rb, kbd ? 2 : 1, kbd || g->eyedrop ? GOLD : GRAY);
        int tw = text_w(&g->font, g->font_loaded, "pick", fs);
        gtext(&g->font, g->font_loaded, "pick", (int)rb.x + ((int)rb.width - tw) / 2, (int)rb.y + ((int)rb.height - fs) / 2, fs, RAYWHITE);
        rb = fill_button(g);
        kbd = g->focus == FOCUS_TOOLS && g->tool_sel == 1;
        DrawRectangleRec(rb, g->fill_armed ? (Color){90, 70, 20, 255} : (Color){50, 50, 60, 255});
        DrawRectangleLinesEx(rb, kbd ? 2 : 1, kbd || g->fill_armed ? GOLD : GRAY);
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
    if (tiles_view(g)) {
        draw_attr_buttons(g, 0);
        int lx = (int)attr_button(g, 0, 0).x;
        int ly = (int)(attr_button(g, 2, 0).y + attr_button(g, 2, 0).height) + PAD;
        draw_attr_legend(g, lx, ly);
    }
    (void)block_available;
}

// The source grid under the picker. While a block panel is up its view buttons are dimmed (a
// block is tiles), the attribute buttons take rows 3-4 (see draw_block_editor), and the toggle
// moves to row 5 reading "gfx"; with the panel hidden that toggle sits at row 4 reading "block"
// (the two never show at once, so one function draws both) and room import/export take row 5.
static void draw_source_grid(GuiState *g, bool block_up) {
    static const char *names[] = { "reset", "tiles", "sprites", "backs", "font", "screens", "import", "export" };
    int fs = g->font_px;
    for (int i = 0; i < 8; i++) {
        Rectangle rb = pixed_button(g, i);
        bool on = (i == 1 && tiles_view(g)) || (i == 2 && g->sprites_view) || (i == 3 && g->backs_view) || (i == 4 && g->font_view) || (i == 5 && g->screens_view);
        bool kbd = !block_up && g->focus == FOCUS_BUTTONS && g->btn_row == i / 2 && g->btn_sel % 2 == i % 2;
        DrawRectangleRec(rb, on ? (Color){90, 70, 20, 255} : (Color){50, 50, 60, 255});
        DrawRectangleLinesEx(rb, kbd ? 2 : 1, kbd || on ? GOLD : GRAY);
        int tw = text_w(&g->font, g->font_loaded, names[i], fs);
        gtext(&g->font, g->font_loaded, names[i], (int)rb.x + ((int)rb.width - tw) / 2, (int)rb.y + 2, fs, block_up ? GRAY : RAYWHITE);
    }
    if (block_up || g->block_hidden >= 0) {
        Rectangle rb = grid_button(g, block_up ? 5 : 4, 0);
        const char *name = block_up ? "gfx" : "block";
        bool kbd = !block_up && g->focus == FOCUS_BUTTONS && g->btn_row == 4;
        DrawRectangleRec(rb, (Color){50, 50, 60, 255});
        DrawRectangleLinesEx(rb, kbd ? 2 : 1, kbd ? GOLD : GRAY);
        int tw = text_w(&g->font, g->font_loaded, name, fs);
        gtext(&g->font, g->font_loaded, name, (int)rb.x + ((int)rb.width - tw) / 2, (int)rb.y + 2, fs, RAYWHITE);
    }
    if (!block_up) {
        static const char *rnames[2] = { "imp room", "exp room" };
        for (int i = 0; i < 2; i++) {
            Rectangle rb = grid_button(g, 5, i);
            bool kbd = g->focus == FOCUS_BUTTONS && g->btn_row == 5 && g->btn_sel % 2 == i;
            DrawRectangleRec(rb, (Color){50, 50, 60, 255});
            DrawRectangleLinesEx(rb, kbd ? 2 : 1, kbd ? GOLD : GRAY);
            int tw = text_w(&g->font, g->font_loaded, rnames[i], fs);
            gtext(&g->font, g->font_loaded, rnames[i], (int)rb.x + ((int)rb.width - tw) / 2, (int)rb.y + 2, fs, RAYWHITE);
        }
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

static int world_y(GuiState *g) { int a = buttons_bottom(g), b = pixed_bottom(g); return (a > b ? a : b) + PAD; }
// attribute buttons (what painted tiles are): a row under the source buttons
// The tile attributes: a 2x2 block right under the tile picker, level with the
// colour swatches; its own Tab stop (FOCUS_ATTRS). `row_offset` pushes it down
// by that many button rows - 0 in the ordinary tiles view, 3 (below the block
// panel's own 3 rows of buttons) while a block object's tile grid is up,
// since both share the same picker-column space (see block_button).
static Rectangle attr_button(GuiState *g, int i, int row_offset) {
    int bw = (TILESET_PX * g->scale - PAD) / 2;
    return (Rectangle){ palette_x(g) + (i % 2) * (bw + PAD), swatch_y(g) + (row_offset + i / 2) * (ch(g) + 8), bw, ch(g) + 4 };
}
// Which grid rows have buttons right now.
static bool grid_row_valid(GuiState *g, int row) { return row < 4 || row == 5 || (row == 4 && g->block_hidden >= 0); }

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

// Where in room `room`'s thumbnail point `m` falls, in that room's own tile coordinates
// (clamped) - used to land the cursor where a Ctrl-click on the world map landed.
static void world_click_tile(GuiState *g, int room, Vector2 m, int *tx, int *ty) {
    int tw, th;
    world_geom(g, &tw, &th);
    Rectangle d = world_cell(g, room, tw, th);
    *tx = (int)((m.x - d.x) / d.width * WIDTH_TILES);
    *ty = (int)((m.y - d.y) / d.height * HEIGHT_TILES);
    if (*tx < 0) *tx = 0; else if (*tx >= WIDTH_TILES) *tx = WIDTH_TILES - 1;
    if (*ty < 0) *ty = 0; else if (*ty >= HEIGHT_TILES) *ty = HEIGHT_TILES - 1;
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
    else render_room_rgb(g->rctx, rooms, room, buf, -1, -1, false);
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
            char side[5] = "";                       // every exit of this room that leads here, e.g. "SW"
            if (r == cur->room_north) strcat(side, "N");
            if (r == cur->room_south) strcat(side, "S");
            if (r == cur->room_west) strcat(side, "W");
            if (r == cur->room_east) strcat(side, "E");
            if (r == ged_room()) DrawRectangleLinesEx(d, 2, YELLOW);
            else if (side[0]) {
                DrawRectangleLinesEx(d, 2, SKYBLUE);
                DrawRectangle((int)d.x, (int)d.y, (int)strlen(side) * cw(g) + 2, ch(g), (Color){0, 0, 0, 170});
                gtext(&g->font, g->font_loaded, side, (int)d.x + 1, (int)d.y, fs, SKYBLUE);
            }
        }
        if (r == g->hover_room) DrawRectangleLinesEx(d, 2, WHITE);
        if (g->focus == FOCUS_WORLD && r == g->world_sel) DrawRectangleLinesEx((Rectangle){d.x - 2, d.y - 2, d.width + 4, d.height + 4}, 2, GOLD);
        if (g->wpress && r == g->wpress_room) DrawRectangleLinesEx(d, 2, MAGENTA);   // the room being dragged
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

// Every switch link on screen as a segment: the chunks of this room's
// switches (block / object / same-room switch targets inside the picture,
// other rooms' thumbnails on the map) and the incoming arrows from other rooms'
// switches. `room` / `sw` / `chunk` name the chunk. Mirrors the geometry that
// draw_overlay and draw_world_links draw.
typedef struct {
    Vector2 a, b;
    int room, sw, chunk;        // the chunk this segment stands for
    int to_room, to_sw;         // a TOGGLE_BIT link: the switch it reaches (to_room -1 otherwise)
    int kind;                   // the chunk's type
    int obj, bx, by;            // TOGGLE_OBJECT: the object; TOGGLE_BLOCK: the run's first tile
} LinkSeg;

// The switch that owns a chunk whose TARGET (a TOGGLE_BLOCK's tile run, or a TOGGLE_OBJECT's
// object) sits at the cursor - so parking on, or clicking, what a switch's chunk touches
// highlights every one of that switch's links, not just the one under the cursor (Hugh:
// "automatically select the switch, even if offscreen, but the cursor stays on the selected
// chunk"). A same-room TOGGLE_BIT's target is already covered by ged_item_at()'s own
// switch-marker check (cur_kind == 1), so it isn't repeated here. -1 if none.
static int switch_owning_target(int cx, int cy, int cur_kind, int cur_idx) {
    struct DecompressedRoom *r = &ged_rooms()->rooms[ged_room()].data;
    for (int i = 0; i < r->num_switches; i++) {
        struct SwitchObject *sw = r->switches + i;
        for (size_t c = 1; c < sw->chunks.length; c++) {
            struct SwitchChunk *ch = sw->chunks.data + c;
            if (ch->type == TOGGLE_BLOCK && exit_memory_offset(ch) < 0) {
                int w = ch->dir == HORIZONTAL ? ch->size : 1;
                int h = ch->dir == VERTICAL ? ch->size : 1;
                if (cx >= ch->x && cx < ch->x + w && cy >= ch->y && cy < ch->y + h) return i;
            } else if (ch->type == TOGGLE_OBJECT && cur_kind == 2 && cur_idx == (int)ch->index) {
                return i;
            }
        }
    }
    return -1;
}

// The switch to treat as "selected" for highlighting/click purposes: the one open for editing
// or chosen with s<n> (ged_current_switch()), the one under the cursor's own marker, or the
// one found by switch_owning_target() above. -1 if none of those apply.
static int effective_sel_sw(int cx, int cy) {
    int cur_idx, cur_kind = ged_item_at(cx, cy, &cur_idx);
    int sel_sw = cur_kind == 1 ? cur_idx : ged_current_switch();
    int owner = switch_owning_target(cx, cy, cur_kind, cur_idx);
    return owner >= 0 ? owner : sel_sw;
}

static int collect_links(GuiState *g, LinkSeg *out, int max) {
    RoomFile *rooms = ged_rooms();
    int cur = ged_room(), n = 0, tw, th, cx, cy;
    struct DecompressedRoom *r = &rooms->rooms[cur].data;
    world_geom(g, &tw, &th);
    ged_cursor(&cx, &cy);
    int sel_sw = effective_sel_sw(cx, cy);
    bool show_all = ged_show_switch_links();
    for (int i = 0; i < r->num_switches && n < max; i++) {
        struct SwitchObject *sw = r->switches + i;
        if (sw->chunks.length == 0 || (!show_all && i != sel_sw)) continue;
        Vector2 o = tile_pt(g, sw->chunks.data[0].x + 0.5f, sw->chunks.data[0].y + 0.5f);
        for (size_t c = 1; c < sw->chunks.length && n < max; c++) {
            struct SwitchChunk *ch = sw->chunks.data + c;
            Vector2 e;
            int to_room = -1, to_sw = -1, obj = -1, bx = ch->x, by = ch->y;
            if (exit_memory_offset(ch) >= 0) {   // one segment per room the exit switches between
                int targets[2] = { ch->off, ch->on };
                for (int k = 0; k < (ch->off == ch->on ? 1 : 2) && n < max; k++) {
                    if (targets[k] >= 64) continue;
                    Rectangle d = world_cell(g, targets[k], tw, th);
                    out[n++] = (LinkSeg){ switch_anchor(g, sw), (Vector2){ d.x + d.width / 2, d.y + d.height / 2 }, cur, i, (int)c, targets[k], -1, TOGGLE_BLOCK, -1, -1, -1 };
                }
                continue;
            }
            if (ch->type == TOGGLE_BLOCK) e = tile_pt(g, ch->x + 0.5f, ch->y + 0.5f);
            else if (ch->type == TOGGLE_OBJECT && ch->index < r->num_objects) {
                struct RoomObject *t = r->objects + ch->index;
                e = tile_pt(g, t->x + (t->type == BLOCK ? t->block.width / 2.0f : 0.5f), t->y + (t->type == BLOCK ? t->block.height / 2.0f : 0.5f));
                obj = ch->index;
            } else if (ch->type == TOGGLE_BIT && ch->room_idx == cur && ch->switch_idx < r->num_switches && r->switches[ch->switch_idx].chunks.length) {
                struct SwitchChunk *p = r->switches[ch->switch_idx].chunks.data;
                e = tile_pt(g, p->x + 0.5f, p->y + 0.5f);
                to_room = cur; to_sw = ch->switch_idx;
            } else if (ch->type == TOGGLE_BIT && ch->room_idx != cur && ch->room_idx < 64) {
                Rectangle d = world_cell(g, ch->room_idx, tw, th);
                e = (Vector2){ d.x + d.width / 2, d.y + d.height / 2 };
                o = switch_anchor(g, sw);
                to_room = ch->room_idx; to_sw = ch->switch_idx;
            } else continue;
            out[n++] = (LinkSeg){ o, e, cur, i, (int)c, to_room, to_sw, ch->type, obj, bx, by };
        }
    }
    for (int rid = 0; rid < 64 && n < max; rid++) {
        if (rid == cur || !rooms->rooms[rid].valid) continue;
        struct DecompressedRoom *other = &rooms->rooms[rid].data;
        for (int i = 0; i < other->num_switches && n < max; i++) {
            struct SwitchObject *sw = other->switches + i;
            for (size_t c = 1; c < sw->chunks.length && n < max; c++) {
                struct SwitchChunk *ch = sw->chunks.data + c;
                int ex = exit_memory_offset(ch);
                if (ex >= 0 && (ch->off == cur || ch->on == cur)) {
                    if (!show_all) continue;   // no switch of ours is "active" for a foreign exit-memory chunk
                    Rectangle d = world_cell(g, rid, tw, th);
                    out[n++] = (LinkSeg){ (Vector2){ d.x + d.width / 2, d.y + d.height / 2 }, arrival_edge(g, ex), rid, i, (int)c, cur, -1, TOGGLE_BLOCK, -1, -1, -1 };
                    continue;
                }
                if (ch->type != TOGGLE_BIT || ch->room_idx != cur || ch->switch_idx >= r->num_switches || r->switches[ch->switch_idx].chunks.length == 0) continue;
                if (!show_all && (int)ch->switch_idx != sel_sw) continue;
                Rectangle d = world_cell(g, rid, tw, th);
                out[n++] = (LinkSeg){ (Vector2){ d.x + d.width / 2, d.y + d.height / 2 }, switch_anchor(g, r->switches + ch->switch_idx), rid, i, (int)c, cur, ch->switch_idx, TOGGLE_BIT, -1, 0, 0 };
            }
        }
    }
    return n;
}

static float point_segment_dist(Vector2 p, Vector2 a, Vector2 b) {
    float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0;
    if (t < 0) t = 0; else if (t > 1) t = 1;
    float qx = a.x + t * dx - p.x, qy = a.y + t * dy - p.y;
    return sqrtf(qx * qx + qy * qy);
}

// After a link led into another room: the cursor (and Spider-Man) go onto the
// switch it arrived at, when that switch is inside the room.
static void cursor_to_switch(int room, int sw) {
    struct DecompressedRoom *r = &ged_rooms()->rooms[room].data;
    if (ged_room() != room || sw < 0 || sw >= r->num_switches || r->switches[sw].chunks.length == 0) return;
    int x = r->switches[sw].chunks.data[0].x, y = r->switches[sw].chunks.data[0].y;
    if (x >= WIDTH_TILES) x = WIDTH_TILES - 1;         // out of bounds: as close as the room allows
    if (y >= HEIGHT_TILES) y = HEIGHT_TILES - 1;
    ged_set_cursor(x, y);
}

// A click near a link line selects what it stands for, which is what draws
// it thick. A link inside the room selects its chunk. A cross-room arrow
// works in two steps: the first click selects its end in this room (the
// chunk of an outgoing arrow, the switch an incoming one toggles); a click
// on it while that is already selected follows it to the other room and
// selects the switch / chunk there. Clicks near a switch marker are left to
// the cursor. true if a link took the click.
// The link within `reach` px of `m`, nearest first; -1 if none. Clicks near a
// switch marker are left to the cursor.
static int nearest_link(Vector2 m, LinkSeg *segs, int n, float reach) {
    int best = -1, cur = ged_room();
    for (int i = 0; i < n; i++) {
        Vector2 sw_end = segs[i].room == cur ? segs[i].a : segs[i].b;
        if (point_segment_dist(m, sw_end, sw_end) < 10) continue;
        float d = point_segment_dist(m, segs[i].a, segs[i].b);
        if (d < reach) { reach = d; best = i; }
    }
    return best;
}

// Once per frame: which link the mouse is near, so it can be drawn a little
// fatter as a hint that a click will take it.
static void link_hover(GuiState *g, Vector2 m) {
    g->hov_sw = -1;
    if (!ged_show_switches() || g->screens_view || world_room_at(g, m) >= 0) return;
    static LinkSeg segs[512];
    int n = collect_links(g, segs, 512), i = nearest_link(m, segs, n, 6);
    if (i >= 0) { g->hov_room = segs[i].room; g->hov_sw = segs[i].sw; g->hov_chunk = segs[i].chunk; }
}
static bool link_hovered(GuiState *g, int room, int sw, int chunk) {
    return g->hov_sw == sw && g->hov_room == room && g->hov_chunk == chunk;
}

static bool link_click(GuiState *g, Vector2 m) {
    if (!ged_show_switches()) return false;
    static LinkSeg segs[512];
    int n = collect_links(g, segs, 512), cur = ged_room();
    int best = nearest_link(m, segs, n, 6);
    if (best < 0) return false;
    LinkSeg *l = &segs[best];
    ged_new_input_batch();
    if (l->room == cur) {
        bool on_it = ged_current_switch() == l->sw && ged_current_chunk() == l->chunk;
        if (on_it && l->to_room >= 0 && l->to_room != cur) {            // follow an outgoing arrow
            ged_map_click(l->to_room, false, -1, -1);
            ged_select_chunk(l->to_sw, 0);
            cursor_to_switch(l->to_room, l->to_sw);
        } else if (on_it && l->kind == TOGGLE_OBJECT && l->obj >= 0) {   // chunk selected: over to the object it toggles
            ged_goto_object(l->obj);
        } else if (on_it && l->kind == TOGGLE_BLOCK && l->to_room >= 0) {   // an exit-memory arrow: go to that room
            ged_map_click(l->to_room, false, -1, -1);
        } else if (on_it && l->kind == TOGGLE_BLOCK) {                  // ... or onto the tile run
            ged_cancel();
            ged_set_cursor(l->bx, l->by);
        } else if (on_it && l->kind == TOGGLE_BIT && l->to_room == cur) {   // ... or the switch in this room it toggles
            ged_select_chunk(l->to_sw, 0);
            cursor_to_switch(cur, l->to_sw);
        } else ged_select_chunk(l->sw, l->chunk);                       // (from the thing, the next click comes back to the chunk)
    } else if (l->kind == TOGGLE_BLOCK) {                               // an exit rewritten to lead here: nothing here to select, go to that chunk
        ged_map_click(l->room, false, -1, -1);
        ged_select_chunk(l->sw, l->chunk);
        cursor_to_switch(l->room, l->sw);
    } else {
        bool on_it = ged_current_switch() == l->to_sw && ged_current_chunk() == 0;
        if (on_it) {                                                    // follow an incoming arrow back to its switch
            ged_map_click(l->room, false, -1, -1);
            ged_select_chunk(l->sw, l->chunk);
            cursor_to_switch(l->room, l->sw);
        } else ged_select_chunk(l->to_sw, 0);
    }
    g->focus = FOCUS_ROOM;
    return true;
}

// exit_memory_offset() itself lives in room.c/room.h (shared with swapRooms()'s own fixup).
static const char *EXIT_NAMES[] = { "", "", "north", "east", "south", "west" };

// Where a player arrives in this room when it becomes another room's exit
// `ex`: their north exit leads onto our bottom edge, east onto our left, ...
static Vector2 arrival_edge(GuiState *g, int ex) {
    float mx = pic_x(g) + pic_w(g) / 2.0f, my = pic_y(g) + pic_h(g) / 2.0f;
    switch (ex) {
        case 2:  return (Vector2){ mx, pic_y(g) + pic_h(g) - 2 };   // their north exit: we are above them
        case 3:  return (Vector2){ pic_x(g) + 2, my };              // their east exit: we are to their right, entered from our left
        case 4:  return (Vector2){ mx, pic_y(g) + 2 };
        default: return (Vector2){ pic_x(g) + pic_w(g) - 2, my };
    }
}

// An arrow from `o` to `e` with its head at `e`.
static void draw_arrow(Vector2 o, Vector2 e, float thick, float head, Color col) {
    DrawLineEx(o, e, thick, col);
    float dx = e.x - o.x, dy = e.y - o.y, len = sqrtf(dx * dx + dy * dy);
    if (len < 1) return;
    float ux = dx / len, uy = dy / len;
    Vector2 a = { e.x - ux * head - uy * head * 0.5f, e.y - uy * head + ux * head * 0.5f };
    Vector2 b = { e.x - ux * head + uy * head * 0.5f, e.y - uy * head - ux * head * 0.5f };
    DrawTriangle(e, b, a, col);
}

// Where a switch of this room is drawn from / to: its marker, or the picture's
// bottom edge for an out-of-bounds one.
static Vector2 switch_anchor(GuiState *g, struct SwitchObject *sw) {
    Vector2 o = tile_pt(g, sw->chunks.data[0].x + 0.5f, sw->chunks.data[0].y + 0.5f);
    if (o.y >= pic_y(g) + pic_h(g)) o.y = pic_y(g) + pic_h(g) - 2;
    return o;
}

// Switch links across rooms, drawn over the world map. Outgoing: a switch's
// TOGGLE_BIT chunks into another room run as green arrows to that room's
// thumbnail, tinted and framed green. Incoming: a switch elsewhere that
// toggles a switch of this room runs as a violet arrow from its room's
// thumbnail, tinted violet, to the switch here. (The map's own frames are
// yellow = this room, blue = neighbours.) Thick while the switch here is hot.
static void draw_world_links(GuiState *g) {
    if (!ged_show_switches() && ged_goto_kind() != 2) return;
    RoomFile *rooms = ged_rooms();
    struct DecompressedRoom *r = &rooms->rooms[ged_room()].data;
    int tw, th, cx, cy;
    world_geom(g, &tw, &th);
    ged_cursor(&cx, &cy);
    int sel_sw = effective_sel_sw(cx, cy);
    // incoming
    for (int rid = 0; rid < 64; rid++) {
        if (rid == ged_room() || !rooms->rooms[rid].valid) continue;
        struct DecompressedRoom *other = &rooms->rooms[rid].data;
        for (int i = 0; i < other->num_switches; i++) {
            struct SwitchObject *sw = other->switches + i;
            for (size_t c = 1; c < sw->chunks.length; c++) {
                struct SwitchChunk *ch = sw->chunks.data + c;
                int ex = exit_memory_offset(ch);
                if (ex >= 0 && (ch->off == ged_room() || ch->on == ged_room())) {   // a switch there makes one of its exits lead here
                    if (!ged_show_switch_links()) continue;   // no switch of ours is "active" for a foreign exit-memory chunk
                    Rectangle d = world_cell(g, rid, tw, th);
                    Vector2 from = { d.x + d.width / 2, d.y + d.height / 2 };
                    bool hov = link_hovered(g, rid, i, (int)c);
                    Color col = { 255, 150, 0, hov ? 255 : 200 };
                    DrawRectangleRec(d, (Color){255, 150, 0, 60});
                    DrawRectangleLinesEx(d, 3, col);
                    draw_arrow(from, arrival_edge(g, ex), hov ? 2.5f : 1.5f, 10, col);
                    const char *state_ = ch->off == ch->on ? "" : ch->on == ged_room() ? " on" : " off";
                    label(g, TextFormat("%s%s -> here", EXIT_NAMES[ex], state_), d.x + 2, d.y + d.height - g->font_px - 2, col, false);
                    continue;
                }
                if (ch->type != TOGGLE_BIT || ch->room_idx != ged_room() || ch->switch_idx >= r->num_switches) continue;
                struct SwitchObject *target = r->switches + ch->switch_idx;
                if (target->chunks.length == 0) continue;
                bool hot = (int)ch->switch_idx == sel_sw;
                if (!ged_show_switch_links() && !hot) continue;
                Rectangle d = world_cell(g, rid, tw, th);
                Vector2 from = { d.x + d.width / 2, d.y + d.height / 2 };
                bool hov = link_hovered(g, rid, i, (int)c);
                Color col = hot ? (Color){200, 140, 255, 255} : (Color){150, 80, 220, hov ? 255 : 200};
                DrawRectangleRec(d, (Color){150, 80, 220, hot ? 90 : 60});
                DrawRectangleLinesEx(d, hot ? 4 : 3, col);
                draw_arrow(from, switch_anchor(g, target), hot ? 3 : hov ? 2.5f : 1.5f, hot ? 14 : 10, col);
            }
        }
    }
    // outgoing
    for (int i = 0; i < r->num_switches; i++) {
        struct SwitchObject *sw = r->switches + i;
        if (sw->chunks.length == 0) continue;
        Vector2 o = switch_anchor(g, sw);
        bool hot = i == sel_sw;
        for (size_t c = 1; c < sw->chunks.length; c++) {
            struct SwitchChunk *ch = sw->chunks.data + c;
            int ex = exit_memory_offset(ch);
            if (ex >= 0) {                       // an exit rewritten by the switch: arrows to the off and on rooms
                if (!ged_show_switch_links() && !hot) continue;
                int targets[2] = { ch->off, ch->on };
                for (int k = 0; k < (ch->off == ch->on ? 1 : 2); k++) {
                    if (targets[k] >= 64) continue;
                    Rectangle d = world_cell(g, targets[k], tw, th);
                    Vector2 e = { d.x + d.width / 2, d.y + d.height / 2 };
                    bool hov = link_hovered(g, ged_room(), i, (int)c);
                    Color col = hot ? (Color){255, 200, 80, 255} : (Color){255, 150, 0, hov ? 255 : 200};
                    DrawRectangleRec(d, (Color){255, 150, 0, hot ? 90 : 60});
                    DrawRectangleLinesEx(d, hot ? 4 : 3, col);
                    draw_arrow(o, e, hot ? 3 : hov ? 2.5f : 1.5f, hot ? 14 : 10, col);
                    const char *tag = ch->off == ch->on ? EXIT_NAMES[ex] : TextFormat("%s %s", EXIT_NAMES[ex], k ? "on" : "off");
                    label(g, tag, d.x + 2, d.y + d.height - g->font_px - 2, col, false);
                }
                continue;
            }
            if (ch->type != TOGGLE_BIT || ch->room_idx == ged_room() || ch->room_idx >= 64) continue;
            if (!ged_show_switch_links() && !hot) continue;
            Rectangle d = world_cell(g, ch->room_idx, tw, th);
            Vector2 e = { d.x + d.width / 2, d.y + d.height / 2 };
            bool hov = link_hovered(g, ged_room(), i, (int)c);
            Color col = hot ? (Color){120, 255, 120, 255} : (Color){0, 200, 80, hov ? 255 : 200};
            DrawRectangleRec(d, (Color){0, 200, 80, hot ? 90 : 60});   // the target room, tinted and framed
            DrawRectangleLinesEx(d, hot ? 4 : 3, col);
            draw_arrow(o, e, hot ? 3 : hov ? 2.5f : 1.5f, hot ? 14 : 10, col);
        }
    }
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

    // Ctrl-D t: every tile tinted by the class its top two bits give it. Empty (0x00)
    // stays clear; other pass-through tiles are faint blue so scenery and hazards you can
    // walk through read as such next to solid (grey), magnetic (violet) and slippery (cyan).
    // A hurting tile (tile_hurts, room.h) is red whatever its class - in a room numbered
    // 7 mod 8 the same tile heals instead.
    if (ged_show_tiles() && !g->text_map) {
        // A TOGGLE_BLOCK chunk's currently-shown tile (on or off - see
        // ged_hot_block_preview) gets tinted by its own class too, not the base
        // tilemap's, matching what effective_tile actually draws there (Hugh:
        // "make sure they have tile debug colouring applied if its on").
        int hot_switch = -1, hot_chunk = -1; bool hot_on = false;
        ged_hot_block_preview(&hot_switch, &hot_chunk, &hot_on);
        for (int y = 0; y < HEIGHT_TILES; y++)
            for (int x = 0; x < WIDTH_TILES; x++) {
                uint8_t t = effective_tile(r, x, y, NULL, hot_switch, hot_chunk, hot_on);
                if (t == 0) continue;
                Vector2 p = tile_pt(g, x, y);
                Color c;
                if (tile_hurts(t)) { c = HURT_COLOUR; c.a = HURT_TINT_ALPHA; }
                // Solid gets no overlay at all - it's the common case, so tinting it would
                // just tint most of the room; showing it as the tile actually looks leaves
                // the tint for the classes worth calling out (Hugh: "make solid be no
                // overlay, just the tile as it is defined").
                else if ((t >> 6) == 1) continue;
                else { c = ATTR_COLOUR[t >> 6]; c.a = TILE_TINT_ALPHA[t >> 6]; }
                DrawRectangle((int)p.x, (int)p.y, (int)s, (int)s, c);
            }
    }

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
    // chunk that targets the object or tile under the cursor. Hot links keep
    // their colour and thicken; only goto mode paints things yellow.
    int cx, cy, cur_idx, cur_kind;
    ged_cursor(&cx, &cy);
    cur_kind = ged_item_at(cx, cy, &cur_idx);
    int sel_sw = effective_sel_sw(cx, cy);
    for (int i = 0; i < r->num_switches; i++) {
        struct SwitchObject *sw = r->switches + i;
        if (sw->chunks.length == 0) continue;
        Vector2 o = tile_pt(g, sw->chunks.data[0].x + 0.5f, sw->chunks.data[0].y + 0.5f);
        bool hot_sw = i == sel_sw;
        DrawCircleV(o, emph_sw || hot_sw ? 6 : 4, emph_sw ? YELLOW : C_SW);
        label(g, TextFormat("S%X", i), o.x + 6, o.y - fs / 2, C_SW, emph_sw);
        // A wee arrow toward the side the switch button faces, and a small "E" in
        // the tile's corner for a room-entry sensor (Hugh: "add a wee arrow to the
        // sides of switches, and E in the corner if entry"). Skipped when "side" is
        // meaningless: out of bounds (never actually touched - the arrow could otherwise
        // reach back into the visible picture from just past its edge, looking like a real
        // on-screen facing), or sitting on a blank tile (just needs walking over, from any
        // direction) - see switch_side_hidden.
        if (!switch_side_hidden(r, sw->chunks.data)) {
            Vector2 tip = o;
            switch (sw->chunks.data[0].side) {
                case TOP:    tip.y -= s * 1.0f; break;
                case RIGHT:  tip.x += s * 1.0f; break;
                case BOTTOM: tip.y += s * 1.0f; break;
                case LEFT:   tip.x -= s * 1.0f; break;
                default: break;
            }
            draw_arrow(o, tip, emph_sw || hot_sw ? 4 : 2, 4, emph_sw ? YELLOW : C_SW);
        }
        // Hidden only for a switch on an in-bounds blank tile: "entry" there just means "the
        // switch works at all" (always forced true), not a room-entry sensor - see
        // switch_entry_forced. An out-of-bounds switch's entry keeps its ordinary meaning.
        if (!switch_entry_forced(r, sw->chunks.data) && sw->chunks.data[0].room_entry) {
            Vector2 corner = tile_pt(g, sw->chunks.data[0].x, sw->chunks.data[0].y);
            gtext(&g->font, g->font_loaded, "E", (int)corner.x + 1, (int)corner.y, fs, emph_sw ? YELLOW : C_SW);
        }
        for (size_t c = 1; c < sw->chunks.length; c++) {
            struct SwitchChunk *ch = sw->chunks.data + c;
            bool hot = hot_sw;
            if (ch->type == TOGGLE_BLOCK && ch->y * WIDTH_TILES + ch->x >= WIDTH_TILES * HEIGHT_TILES) {
                continue;                        // a memory chunk: its exit arrows are drawn on the world map
            } else if (ch->type == TOGGLE_BLOCK) {
                int w = ch->dir == HORIZONTAL ? ch->size : 1;
                int h = ch->dir == VERTICAL ? ch->size : 1;
                if (cx >= ch->x && cx < ch->x + w && cy >= ch->y && cy < ch->y + h) hot = true;
                if (!ged_show_switch_links() && !hot) continue;   // Ctrl-D l off: only the active switch's links
                Vector2 t = tile_pt(g, ch->x, ch->y);
                float lw = hot ? 3 : link_hovered(g, ged_room(), i, (int)c) ? 2.5f : 1;
                DrawRectangleLinesEx((Rectangle){t.x, t.y, w * s, h * s}, hot ? 3 : 1, C_TBLK);
                Vector2 e = tile_pt(g, ch->x + 0.5f, ch->y + 0.5f);
                DrawLineEx(o, e, lw, C_TBLK);
            } else if (ch->type == TOGGLE_OBJECT && ch->index < r->num_objects) {
                struct RoomObject *t = r->objects + ch->index;
                if (cur_kind == 2 && cur_idx == ch->index) hot = true;
                if (!ged_show_switch_links() && !hot) continue;
                float cw = t->type == BLOCK ? t->block.width / 2.0f : 0.5f;
                float chh = t->type == BLOCK ? t->block.height / 2.0f : 0.5f;
                Vector2 e = tile_pt(g, t->x + cw, t->y + chh);
                DrawLineEx(o, e, hot ? 3 : link_hovered(g, ged_room(), i, (int)c) ? 2.5f : 1, C_OBJ);
            } else if (ch->type == TOGGLE_BIT && ch->room_idx == ged_room() && ch->switch_idx < r->num_switches) {
                // a switch in this room; links into other rooms are drawn to the world map (draw_world_links)
                struct SwitchObject *ts = r->switches + ch->switch_idx;
                if (ts->chunks.length == 0) continue;
                if (cur_kind == 1 && cur_idx == (int)ch->switch_idx) hot = true;
                if (!ged_show_switch_links() && !hot) continue;
                Vector2 e = tile_pt(g, ts->chunks.data[0].x + 0.5f, ts->chunks.data[0].y + 0.5f);
                DrawLineEx(o, e, hot ? 3 : link_hovered(g, ged_room(), i, (int)c) ? 2.5f : 1, C_BIT);
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
        if (g->scr_loaded) UnloadTexture(g->scr_tex);
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
        g->es = ged_open(file, NULL, data_dir);
    } else {
        g->es = ged_open(file, g->es, data_dir);   // re-adopt (or reset) the editor state after a reload
    }
    ged_set_render(g->rctx);                    // the editor's background view reads and writes our backgrounds
    render_show_player(g->rctx, false);  // he is drawn at the cursor instead
    render_set_movertab(g->rctx, ged_movertab());   // mirror sprites per their facing/upside-down bits
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
        if (IsFileDropped()) {                             // a PNG dropped on the window replaces the current image
            FilePathList dropped = LoadDroppedFiles();
            int bi, bx, by, bw, bh;
            if (dropped.count > 0) {
                if (block_panel(g, &bi, &bx, &by, &bw, &bh)) block_import_png(g, bi, bw, bh, dropped.paths[0]);
                else import_png(g, dropped.paths[0]);
            }
            UnloadDroppedFiles(dropped);
            g->room_dirty = true;
        }
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
                if (g->world_pending) { world_build(g, ged_rooms(), buf); g->world_pending = false; }   // every room may use the edited image
                g->room_dirty = true;
            }
        }
        if (g->paint_dirty) {                              // debounced write of paint strokes
            g->save_timer -= dt;
            if (g->save_timer <= 0 && !g->painting) {
                ged_flush();                           // its write shows up through ged_save_count below
                g->room_dirty = true;                  // a refused write undid the stroke: show the room as saved
                g->paint_dirty = false;
            }
        }

        ged_set_backs_view(g->backs_view);                 // the backs picker up = the editor's background mode (its keys, panel, help; F2 shows its hex)
        if (ged_backs_edited()) { g->room_dirty = true; g->world_pending = true; g->world_stale = true; }
        ged_redraw();                                      // the editor lays out its text screen

        RoomFile *rooms = ged_rooms();
        int room = ged_room();
        if (g->room_dirty || g->drawn_room != room) {
            int hot_switch = -1, hot_chunk = -1; bool hot_on = false;
            ged_hot_block_preview(&hot_switch, &hot_chunk, &hot_on);
            render_show_objects(g->rctx, !g->backs_view);   // background mode: the room's tiles over its background, no objects or sprites
            render_room_rgb(g->rctx, rooms, room, buf, hot_switch, hot_chunk, hot_on);
            Image img = { .data = buf, .width = ROOM_PX_W, .height = ROOM_PX_H,
                          .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8 };
            if (!g->tex_loaded) {
                g->tex = LoadTextureFromImage(img);
                SetTextureFilter(g->tex, TEXTURE_FILTER_POINT);
                g->tex_loaded = true;
            } else {
                UpdateTexture(g->tex, buf);
            }
            if (g->drawn_room != room && g->drawn_room >= 0) {        // the room we left may have changed, or be removed if still blank
                ged_left_room(g->drawn_room);
                world_update_room(g, rooms, g->drawn_room, buf);
            }
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
        if (g->font_view && (!g->sheet_loaded[GFX_CHARSET] || g->sheet_stale[GFX_CHARSET])) sheet_build(g, GFX_CHARSET);
        if (g->screens_view && render_gfx_available(g->rctx, g->pix_src) && (!g->scr_loaded || g->scr_stale || g->scr_src != g->pix_src))
            screen_tex_build(g);

        {   // any write the editor made this frame (a keyboard edit, a paint stroke's flush, Ctrl-M's
            // MOVERTAB.DAT save): flash its file name
            const char *f = NULL;
            unsigned n = ged_save_count(&f);
            if (n != g->seen_saves) { g->seen_saves = n; g->saved_flash = 1.2f; snprintf(g->saved_what, sizeof g->saved_what, "%s", f); }
        }
        if (g->saved_flash > 0) g->saved_flash -= dt;
        if (g->err_flash > 0) g->err_flash -= dt;
        link_hover(g, GetMousePosition());
        BeginDrawing();
        ClearBackground(BLACK);
        if (g->screens_view) {                       // screen mode: the picture and its panel, nothing else
            draw_screen_mode(g);
            if (ged_help_shown()) draw_help(g);
            EndDrawing();
            continue;
        }
        draw_text_screen(g);
        if (!g->text_map && !ged_map_is_text()) {   // room-list states show the editor's text instead
            DrawTexturePro(g->tex, (Rectangle){0, 0, ROOM_PX_W, ROOM_PX_H},
                           (Rectangle){pic_x(g), pic_y(g), pic_w(g), pic_h(g)},
                           (Vector2){0, 0}, 0, WHITE);
            // clip to the picture: out-of-bounds switches (the editor keeps
            // them at y >= 22) would otherwise draw over the text panels
            BeginScissorMode(pic_x(g), pic_y(g), pic_w(g), pic_h(g));
            if (!g->backs_view) {   // the backs picker is about the background: no room overlays over it
                draw_overlay(g);
                draw_hover_preview(g);
                draw_drag(g);
                draw_link_drag(g);
            }
            draw_cursor(g);
            EndScissorMode();
        }
        draw_palette(g);
        {
            int bi, bx, by, bw, bh;
            bool on_block = ged_block_at_cursor(&bi, &bx, &by, &bw, &bh);
            if (!on_block) g->block_hidden = -1;
            if (on_block && bi != g->block_hidden && !tiles_view(g)) set_view(g, VIEW_TILES);   // a block is made of tiles: the picker shows them
            if (on_block && bi != g->block_hidden) draw_block_editor(g, bi, bx, by, bw, bh);
            else draw_gfx_editor(g, on_block);
            draw_source_grid(g, on_block && bi != g->block_hidden);
        }
        draw_world_map(g);
        if (!g->text_map && !ged_map_is_text() && !g->backs_view) draw_world_links(g);   // over the map: switch links into other rooms
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
    if (g->scr_loaded) UnloadTexture(g->scr_tex);
    if (g->font_loaded) UnloadFont(g->font);
    if (g->help_font_loaded) UnloadFont(g->help_font);
    if (g->panel_font_loaded) UnloadFont(g->panel_font);
    render_close(g->rctx);
    free(g);
    CloseWindow();
    return NULL;
}
