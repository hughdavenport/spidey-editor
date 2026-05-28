#include <raylib.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "render.h"
#include "room.h"

#define HUD_H 64
#define PANEL_PAD 8

// Files whose change triggers a hot rebuild of gui.so.
static const char *WATCHED[] = {
    "gui.c", "gui.h", "render.c", "render.h",
    "room.c", "room.h", "spl.c", "spl.h", "array.h",
};

typedef struct { int x, y; } Cur;

typedef struct {
    size_t structsize;
    int room_id;
    int scale;
    bool debug;
    bool tex_loaded;
    int drawn_room;
    RenderCtx *rctx;
    Texture2D tex;
    Cur cursors[64];        // per-room cursor (editor-style)
    Font font;
    bool font_loaded;
    uint16_t partial;       // hex entry: 0 = empty, 0xFF00|d after the 1st digit
    float saved_flash;      // seconds left to show the "saved" indicator
    bool dirty;             // unsaved edits pending (debounced autosave)
    float save_timer;       // seconds until the pending save flushes
    float src_check;        // seconds until the next hot-reload source check
    Texture2D pal_tex;      // tileset palette (current tile_offset)
    bool pal_loaded;
    int pal_offset;         // tile_offset the palette texture was built for
    uint8_t sel_tile;       // currently selected palette tile (0..63)
    bool painting;          // left-mouse drag painting into the room
    int mode;               // 0=NORMAL, 1=GOTO_ROOM, 2=GOTO_SWITCH, 3=GOTO_OBJECT
    uint8_t goto_acc;       // accumulator for goto digit entry
    uint8_t goto_n;         // digit count entered so far
    uint8_t clip_tile;      // yanked tile byte ('y' copies, 'p' pastes)
    bool clip_set;
    int undo_n, redo_n;     // tile-snapshot undo/redo stacks
    struct { uint8_t room; uint8_t tiles[WIDTH_TILES * HEIGHT_TILES]; } undo[24], redo[24];
} GuiState;
#define UNDO_MAX 24
enum { MODE_NORMAL, MODE_GOTO_ROOM, MODE_GOTO_SWITCH, MODE_GOTO_OBJECT };

static const char *FONT_CANDIDATES[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};

// DrawText replacement using the loaded TTF (falls back to raylib's default).
static void gtext(GuiState *g, const char *s, int x, int y, int size, Color col) {
    if (g->font_loaded)
        DrawTextEx(g->font, s, (Vector2){x, y}, size, size / 10.0f, col);
    else
        DrawText(s, x, y, size, col);
}

/* --------------------------------------------------------------- helpers */

static int next_valid(RoomFile *file, int from, int dir) {
    for (int i = 1; i <= 64; i++) {
        int r = ((from + dir * i) % 64 + 64) % 64;
        if (file->rooms[r].valid) return r;
    }
    return from;
}
static int first_valid(RoomFile *file) {
    for (int r = 0; r < 64; r++) if (file->rooms[r].valid) return r;
    return 0;
}

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

/* ---------------------------------------------------------------- drawing */

// Playfield pixel for a tile coordinate (px within the tile via fx/fy in [0,8]).
static Vector2 tile_pt(GuiState *g, float tx, float ty) {
    return (Vector2){ tx * 8 * g->scale, HUD_H + ty * 8 * g->scale };
}

static void draw_hud(GuiState *g, RoomFile *file) {
    struct DecompressedRoom *r = &file->rooms[g->room_id].data;
    Cur cur = g->cursors[g->room_id];
    char name[25] = {0};
    strncpy(name, r->name, 24);
    for (int j = 23; j >= 0 && (name[j] == ' ' || name[j] == '\0'); j--) name[j] = '\0';
    DrawRectangle(0, 0, GetScreenWidth(), HUD_H, (Color){20, 20, 28, 255});
    gtext(g, TextFormat("Room %d: %s", g->room_id, name), 8, 5, 20, GOLD);
    if (g->saved_flash > 0) gtext(g, "saved", GetScreenWidth() - 70, 8, 16, GREEN);
    const char *entry = g->partial ? TextFormat("  entry:%X_", g->partial & 0x0f) : "";
    gtext(g, TextFormat("bkgrnd %d  tileset %d  dmg %d  grav |%d -%d   cur %d,%d = 0x%02x%s",
                        r->background, r->tile_offset, r->room_damage,
                        r->gravity_vertical, r->gravity_horizontal,
                        cur.x, cur.y, r->tiles[cur.y * WIDTH_TILES + cur.x], entry), 8, 30, 14, LIGHTGRAY);
    const char *mode_lbl =
        g->mode == MODE_GOTO_ROOM   ? "GOTO ROOM"   :
        g->mode == MODE_GOTO_SWITCH ? "GOTO SWITCH" :
        g->mode == MODE_GOTO_OBJECT ? "GOTO OBJECT" : "";
    if (g->mode != MODE_NORMAL) {
        char buf[16]; int n = g->goto_n;
        snprintf(buf, sizeof buf, n == 0 ? "_" : n == 1 ? "%X_" : "%02X", g->goto_acc);
        gtext(g, TextFormat("%s: %s  [enter/digits commit, esc cancel]", mode_lbl, buf), 8, 47, 13, YELLOW);
    } else {
        const char *clip = g->clip_set ? TextFormat("  [Y:%02X]", g->clip_tile) : "";
        gtext(g, TextFormat("[hjkl]cur [S+hjkl]move [0-f]write [y/p]yank/paste [r/s/o]goto [u/U]undo [^D]dbg:%s%s",
                            g->debug ? "on" : "off", clip), 8, 47, 12, GRAY);
    }
}

// Move the cursor; wrap to the linked neighbour room at an edge (editor-style).
static void move_cursor(GuiState *g, RoomFile *file, int dx, int dy) {
    Cur *c = &g->cursors[g->room_id];
    struct DecompressedRoom *r = &file->rooms[g->room_id].data;
    int nx = c->x + dx, ny = c->y + dy, nr = -1, ex = c->x, ey = c->y;
    if (nx < 0)              { nr = r->room_west;  ex = WIDTH_TILES - 1; }
    else if (nx >= WIDTH_TILES) { nr = r->room_east; ex = 0; }
    else if (ny < 0)         { nr = r->room_north; ey = HEIGHT_TILES - 1; }
    else if (ny >= HEIGHT_TILES) { nr = r->room_south; ey = 0; }
    else { c->x = nx; c->y = ny; return; }
    if (nr >= 0 && nr < 64 && file->rooms[nr].valid) {   // cross to neighbour
        g->room_id = nr;
        g->cursors[nr].x = (dx != 0) ? ex : c->x;
        g->cursors[nr].y = (dy != 0) ? ey : c->y;
    }
}

static void draw_cursor(GuiState *g) {
    Cur c = g->cursors[g->room_id];
    float s = 8.0f * g->scale;
    Rectangle box = { c.x * s, HUD_H + c.y * s, s, s };
    DrawRectangleLinesEx(box, 2, (Color){255, 255, 255, 230});
    DrawRectangleLinesEx((Rectangle){box.x - 1, box.y - 1, box.width + 2, box.height + 2}, 1, BLACK);
}

static int panel_x0(GuiState *g) { return ROOM_PX_W * g->scale + PANEL_PAD; }

static void draw_palette(GuiState *g) {
    int s = 8 * g->scale, x0 = panel_x0(g), y0 = HUD_H;
    DrawTexturePro(g->pal_tex, (Rectangle){0, 0, TILESET_PX, TILESET_PX},
                   (Rectangle){x0, y0, TILESET_PX * g->scale, TILESET_PX * g->scale},
                   (Vector2){0, 0}, 0, WHITE);
    for (int i = 0; i <= 8; i++) {
        DrawLine(x0 + i * s, y0, x0 + i * s, y0 + 8 * s, (Color){80, 80, 80, 255});
        DrawLine(x0, y0 + i * s, x0 + 8 * s, y0 + i * s, (Color){80, 80, 80, 255});
    }
    int sx = g->sel_tile % 8, sy = g->sel_tile / 8;
    DrawRectangleLinesEx((Rectangle){x0 + sx * s, y0 + sy * s, s, s}, 2, GOLD);
    gtext(g, TextFormat("tileset %d  sel 0x%02x", g->pal_offset, g->sel_tile), x0, y0 + 8 * s + 6, 14, RAYWHITE);
    gtext(g, "click tile, drag in room", x0, y0 + 8 * s + 24, 12, GRAY);
}

// Ghost-preview the selected tile at the hovered room cell ("see the change").
static void draw_hover_preview(GuiState *g) {
    Vector2 m = GetMousePosition();
    int s = 8 * g->scale;
    if (m.x < 0 || m.x >= ROOM_PX_W * g->scale || m.y < HUD_H || m.y >= HUD_H + ROOM_PX_H * g->scale)
        return;
    int tx = (int)m.x / s, ty = ((int)m.y - HUD_H) / s;
    Rectangle dst = { tx * s, HUD_H + ty * s, s, s };
    Rectangle src = { (g->sel_tile % 8) * 8, (g->sel_tile / 8) * 8, 8, 8 };
    DrawTexturePro(g->pal_tex, src, dst, (Vector2){0, 0}, 0, (Color){255, 255, 255, 150});
    DrawRectangleLinesEx(dst, 2, GOLD);
}

static void draw_overlay(GuiState *g, RoomFile *file) {
    struct DecompressedRoom *r = &file->rooms[g->room_id].data;
    float s = 8.0f * g->scale;
    Color C_SW = MAGENTA, C_BLK = SKYBLUE, C_OBJ = (Color){96,160,255,255},
          C_BIT = GREEN, C_TBLK = ORANGE, C_SPR = YELLOW;

    // faint grid
    for (int gx = 0; gx <= WIDTH_TILES; gx++) {
        Vector2 a = tile_pt(g, gx, 0), b = tile_pt(g, gx, HEIGHT_TILES);
        DrawLineV(a, b, (Color){255,255,255,24});
    }
    for (int gy = 0; gy <= HEIGHT_TILES; gy++) {
        Vector2 a = tile_pt(g, 0, gy), b = tile_pt(g, WIDTH_TILES, gy);
        DrawLineV(a, b, (Color){255,255,255,24});
    }

    // objects / sprites
    for (int i = 0; i < r->num_objects; i++) {
        struct RoomObject *o = r->objects + i;
        Vector2 p = tile_pt(g, o->x, o->y);
        if (o->type == BLOCK) {
            DrawRectangleLinesEx((Rectangle){p.x, p.y, o->block.width * s, o->block.height * s}, 1, C_BLK);
            gtext(g, TextFormat("B%d", i), p.x + 2, p.y + 1, 12, C_BLK);
        } else {
            DrawRectangleLinesEx((Rectangle){p.x, p.y, s, s}, 1, C_SPR);
            gtext(g, TextFormat("%s d%d", creature_name(o->sprite.type, o->sprite.damage), o->sprite.damage),
                     p.x + 2, p.y - 13, 12, C_SPR);
        }
    }

    // switches + per-chunk effects
    for (int i = 0; i < r->num_switches; i++) {
        struct SwitchObject *sw = r->switches + i;
        if (sw->chunks.length == 0) continue;
        Vector2 o = tile_pt(g, sw->chunks.data[0].x + 0.5f, sw->chunks.data[0].y + 0.5f);
        DrawCircleV(o, 4, C_SW);
        gtext(g, TextFormat("S%d", i), o.x + 5, o.y - 6, 12, C_SW);
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
                    gtext(g, TextFormat("R%d", rid), e.x - 8, e.y - 7, 12, C_BIT);
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- editing */

static void snapshot(GuiState *g, RoomFile *file) {  // push current room tiles to undo
    if (g->undo_n == UNDO_MAX) {                      // drop oldest
        memmove(g->undo, g->undo + 1, (UNDO_MAX - 1) * sizeof g->undo[0]);
        g->undo_n--;
    }
    g->undo[g->undo_n].room = g->room_id;
    memcpy(g->undo[g->undo_n].tiles, file->rooms[g->room_id].data.tiles, WIDTH_TILES * HEIGHT_TILES);
    g->undo_n++;
    g->redo_n = 0;                                    // a new edit clears redo
}

// Mark the room edited: re-render now, flush to disk after a short idle so we
// don't run the (expensive) full recompress on every keystroke (which stalled
// the frame and dropped the next key).
static void mark_edited(GuiState *g) {
    g->dirty = true;
    g->save_timer = 0.4f;
    g->drawn_room = -1;                               // re-render the room texture
}

// Move the tile under the cursor one cell in (dx,dy) (Shift+arrow). Stays
// within the room (no neighbour-room transfer yet — that's a follow-up).
static void move_tile(GuiState *g, RoomFile *file, int dx, int dy) {
    Cur c = g->cursors[g->room_id];
    int nx = c.x + dx, ny = c.y + dy;
    if (nx < 0 || nx >= WIDTH_TILES || ny < 0 || ny >= HEIGHT_TILES) return;
    uint8_t *t = file->rooms[g->room_id].data.tiles;
    snapshot(g, file);
    uint8_t v = t[c.y * WIDTH_TILES + c.x];
    t[c.y * WIDTH_TILES + c.x] = 0;
    t[ny * WIDTH_TILES + nx] = v;
    g->cursors[g->room_id] = (Cur){nx, ny};
    mark_edited(g);
}

static void commit_goto(GuiState *g, RoomFile *file) {
    int idx = g->goto_acc;
    if (g->mode == MODE_GOTO_ROOM) {
        if (idx >= 0 && idx < 64 && file->rooms[idx].valid) g->room_id = idx;
    } else if (g->mode == MODE_GOTO_SWITCH) {
        struct DecompressedRoom *r = &file->rooms[g->room_id].data;
        if (idx < r->num_switches && r->switches[idx].chunks.length > 0) {
            g->cursors[g->room_id] = (Cur){ r->switches[idx].chunks.data[0].x,
                                            r->switches[idx].chunks.data[0].y };
        }
    } else if (g->mode == MODE_GOTO_OBJECT) {
        struct DecompressedRoom *r = &file->rooms[g->room_id].data;
        if (idx < r->num_objects)
            g->cursors[g->room_id] = (Cur){ r->objects[idx].x, r->objects[idx].y };
    }
    g->mode = MODE_NORMAL;
    g->goto_acc = 0;
    g->goto_n = 0;
}

// Restore from src stack into the room, saving current tiles to dst stack first.
static bool restore(GuiState *g, RoomFile *file, int *src_n, void *src_v, int *dst_n, void *dst_v) {
    typedef struct { uint8_t room; uint8_t tiles[WIDTH_TILES * HEIGHT_TILES]; } Snap;
    Snap *src = src_v, *dst = dst_v;
    if (*src_n == 0) return false;
    Snap *s = &src[*src_n - 1];
    g->room_id = s->room;
    if (*dst_n < UNDO_MAX) {
        dst[*dst_n].room = s->room;
        memcpy(dst[*dst_n].tiles, file->rooms[s->room].data.tiles, WIDTH_TILES * HEIGHT_TILES);
        (*dst_n)++;
    }
    memcpy(file->rooms[s->room].data.tiles, s->tiles, WIDTH_TILES * HEIGHT_TILES);
    (*src_n)--;
    return true;
}

/* ------------------------------------------------------------------ loop */

void *gui_loop(const char *library, RoomFile *file, const char *data_dir, void *state) {
    long last_build = GetFileModTime(library);
    static uint8_t buf[ROOM_PX_W * ROOM_PX_H * 3];

    GuiState *g = state;
    if (g != NULL && g->structsize != sizeof *g) {
        fprintf(stderr, "gui: state size changed, resetting\n");
        if (g->tex_loaded) UnloadTexture(g->tex);
        render_close(g->rctx);
        free(g);
        g = NULL;
    }
    if (g == NULL) {
        g = calloc(1, sizeof *g);
        g->structsize = sizeof *g;
        g->scale = 3;
        g->debug = true;
        g->room_id = first_valid(file);
        g->rctx = render_open(data_dir);
        if (g->rctx == NULL) { free(g); return NULL; }
        InitWindow(ROOM_PX_W * g->scale + TILESET_PX * g->scale + PANEL_PAD * 2,
                   ROOM_PX_H * g->scale + HUD_H, "Spidey Editor — GUI");
        SetTargetFPS(60);
        SetExitKey(KEY_NULL);   // free ESC for "cancel"; close via window button
    }
    g->drawn_room = -1;   // force a texture refresh on (re)entry

    if (!g->font_loaded) {                 // load a nicer TTF (per (re)entry)
        for (size_t i = 0; i < sizeof FONT_CANDIDATES / sizeof FONT_CANDIDATES[0]; i++)
            if (FileExists(FONT_CANDIDATES[i])) {
                g->font = LoadFontEx(FONT_CANDIDATES[i], 32, NULL, 0);
                SetTextureFilter(g->font.texture, TEXTURE_FILTER_BILINEAR);
                g->font_loaded = g->font.texture.id != 0;
                break;
            }
    }

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        g->src_check -= dt;                                // throttle the hot-reload stat()s
        if (g->src_check <= 0) {
            g->src_check = 0.5f;
            if (sources_changed(last_build)) {
                fprintf(stderr, "gui: source changed, rebuilding\n");
                if (g->dirty) { writeRooms(file); g->dirty = false; }  // don't lose edits
                if (system(GUI_BUILD_CMD) == 0) return g;  // host reloads
                fprintf(stderr, "gui: rebuild failed, not reloading\n");
                last_build = GetFileModTime(library);
            }
        }

        bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        bool shift = IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT);

        // movement: plain = move cursor (neighbour wrap); shift = move tile in-room
        int dx = 0, dy = 0;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_L)) dx =  1;
        if (IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_H)) dx = -1;
        if (IsKeyPressed(KEY_UP)    || IsKeyPressed(KEY_K)) dy = -1;
        if (IsKeyPressed(KEY_DOWN)  || IsKeyPressed(KEY_J)) dy =  1;
        if (dx || dy) { if (shift) move_tile(g, file, dx, dy); else move_cursor(g, file, dx, dy); }

        if (IsKeyPressed(KEY_LEFT_BRACKET))  g->room_id = next_valid(file, g->room_id, -1);
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) g->room_id = next_valid(file, g->room_id, +1);
        if (ctrl && IsKeyPressed(KEY_D)) g->debug = !g->debug;   // Ctrl-D (editor-style)

        // Mode entry: r=goto room, s=goto switch, o=goto object (NORMAL, no mods).
        if (g->mode == MODE_NORMAL && !ctrl && !shift) {
            if (IsKeyPressed(KEY_R)) { g->mode = MODE_GOTO_ROOM;   g->goto_acc = 0; g->goto_n = 0; g->partial = 0; }
            if (IsKeyPressed(KEY_S)) { g->mode = MODE_GOTO_SWITCH; g->goto_acc = 0; g->goto_n = 0; g->partial = 0; }
            if (IsKeyPressed(KEY_O)) { g->mode = MODE_GOTO_OBJECT; g->goto_acc = 0; g->goto_n = 0; g->partial = 0; }
        }
        if (g->mode != MODE_NORMAL && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)))
            commit_goto(g, file);

        // Hex digits via the char queue. Branch on mode: NORMAL = tile entry;
        // GOTO_* = accumulate index, commit after enough digits.
        for (int ch = GetCharPressed(); ch != 0; ch = GetCharPressed()) {
            int nib = -1;
            if (ch >= '0' && ch <= '9') nib = ch - '0';
            else if (ch >= 'a' && ch <= 'f') nib = 10 + ch - 'a';
            else if (ch >= 'A' && ch <= 'F') nib = 10 + ch - 'A';
            if (nib < 0) continue;
            if (g->mode == MODE_NORMAL) {
                if (g->partial == 0) {
                    g->partial = 0xFF00 | nib;             // first nibble
                } else {
                    uint8_t byte = ((g->partial & 0x0f) << 4) | nib;
                    Cur c = g->cursors[g->room_id];
                    snapshot(g, file);
                    file->rooms[g->room_id].data.tiles[c.y * WIDTH_TILES + c.x] = byte;
                    g->partial = 0;
                    mark_edited(g);
                }
            } else {
                g->goto_acc = (uint8_t)((g->goto_acc << 4) | nib);
                g->goto_n++;
                int need = (g->mode == MODE_GOTO_ROOM) ? 2 : 1;
                if (g->goto_n >= need) commit_goto(g, file);
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_DELETE)) {
            if (g->mode != MODE_NORMAL) { g->goto_acc = 0; g->goto_n = 0; }
            else if (g->partial) { g->partial = 0; }
            else {
                Cur c = g->cursors[g->room_id];
                snapshot(g, file);
                file->rooms[g->room_id].data.tiles[c.y * WIDTH_TILES + c.x] = 0;
                mark_edited(g);
            }
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            g->partial = 0; g->mode = MODE_NORMAL; g->goto_acc = 0; g->goto_n = 0;
        }
        if (g->mode == MODE_NORMAL && !ctrl && !shift && IsKeyPressed(KEY_Y)) {
            Cur c = g->cursors[g->room_id];
            g->clip_tile = file->rooms[g->room_id].data.tiles[c.y * WIDTH_TILES + c.x];
            g->clip_set = true;
        }
        if (g->mode == MODE_NORMAL && !ctrl && !shift && IsKeyPressed(KEY_P) && g->clip_set) {
            Cur c = g->cursors[g->room_id];
            snapshot(g, file);
            file->rooms[g->room_id].data.tiles[c.y * WIDTH_TILES + c.x] = g->clip_tile;
            mark_edited(g);
        }
        if (g->mode == MODE_NORMAL && !ctrl && IsKeyPressed(KEY_U)) {
            bool redo = shift;
            bool ok = redo ? restore(g, file, &g->redo_n, g->redo, &g->undo_n, g->undo)
                           : restore(g, file, &g->undo_n, g->undo, &g->redo_n, g->redo);
            if (ok) mark_edited(g);
        }

        // mouse: pick a tile from the palette, or paint into the room (drag)
        {
            Vector2 m = GetMousePosition();
            int s = 8 * g->scale, px0 = panel_x0(g);
            bool in_pal = m.x >= px0 && m.x < px0 + 8 * s && m.y >= HUD_H && m.y < HUD_H + 8 * s;
            bool in_room = m.x >= 0 && m.x < ROOM_PX_W * g->scale && m.y >= HUD_H
                           && m.y < HUD_H + ROOM_PX_H * g->scale;
            int tx = (int)m.x / s, ty = ((int)m.y - HUD_H) / s;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (in_pal) {
                    g->sel_tile = ((int)(m.y - HUD_H) / s) * 8 + (int)(m.x - px0) / s;
                } else if (in_room) {
                    snapshot(g, file);                  // one undo entry per stroke
                    g->painting = true;
                    g->cursors[g->room_id] = (Cur){tx, ty};
                    file->rooms[g->room_id].data.tiles[ty * WIDTH_TILES + tx] = g->sel_tile;
                    mark_edited(g);
                }
            }
            if (g->painting && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && in_room) {
                if (tx != g->cursors[g->room_id].x || ty != g->cursors[g->room_id].y) {
                    g->cursors[g->room_id] = (Cur){tx, ty};
                    file->rooms[g->room_id].data.tiles[ty * WIDTH_TILES + tx] = g->sel_tile;
                    mark_edited(g);
                }
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) g->painting = false;
        }

        if (g->dirty) {                                 // debounced autosave
            g->save_timer -= dt;
            if (g->save_timer <= 0) { writeRooms(file); g->dirty = false; g->saved_flash = 1.2f; }
        }

        if (g->drawn_room != g->room_id) {
            render_room_rgb(g->rctx, file, g->room_id, buf);
            Image img = { .data = buf, .width = ROOM_PX_W, .height = ROOM_PX_H,
                          .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8 };
            if (!g->tex_loaded) {
                g->tex = LoadTextureFromImage(img);
                SetTextureFilter(g->tex, TEXTURE_FILTER_POINT);
                g->tex_loaded = true;
            } else {
                UpdateTexture(g->tex, buf);
            }
            g->drawn_room = g->room_id;
        }

        int toff = file->rooms[g->room_id].data.tile_offset;   // rebuild palette on tileset change
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

        if (g->saved_flash > 0) g->saved_flash -= GetFrameTime();
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(g->tex,
                       (Rectangle){0, 0, ROOM_PX_W, ROOM_PX_H},
                       (Rectangle){0, HUD_H, ROOM_PX_W * g->scale, ROOM_PX_H * g->scale},
                       (Vector2){0, 0}, 0, WHITE);
        if (g->debug) draw_overlay(g, file);
        draw_hover_preview(g);
        draw_cursor(g);
        draw_palette(g);
        draw_hud(g, file);
        EndDrawing();
    }

    if (g->dirty) writeRooms(file);   // flush any pending edit before closing
    if (g->tex_loaded) UnloadTexture(g->tex);
    if (g->pal_loaded) UnloadTexture(g->pal_tex);
    if (g->font_loaded) UnloadFont(g->font);
    render_close(g->rctx);
    free(g);
    CloseWindow();
    return NULL;
}
