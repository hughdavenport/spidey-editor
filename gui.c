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

// Files whose change triggers a hot rebuild of gui.so.
static const char *WATCHED[] = {
    "gui.c", "gui.h", "render.c", "render.h",
    "room.c", "room.h", "spl.c", "spl.h", "array.h",
};

typedef struct {
    size_t structsize;
    int room_id;
    int scale;
    bool debug;
    bool tex_loaded;
    int drawn_room;
    RenderCtx *rctx;
    Texture2D tex;
} GuiState;

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
    char name[25] = {0};
    strncpy(name, r->name, 24);
    for (int j = 23; j >= 0 && (name[j] == ' ' || name[j] == '\0'); j--) name[j] = '\0';
    DrawRectangle(0, 0, GetScreenWidth(), HUD_H, (Color){20, 20, 28, 255});
    DrawText(TextFormat("Room %d: %s", g->room_id, name), 8, 6, 20, GOLD);
    DrawText(TextFormat("bkgrnd %d  tileset %d  dmg %d  grav |%d -%d",
                        r->background, r->tile_offset, r->room_damage,
                        r->gravity_vertical, r->gravity_horizontal), 8, 30, 14, LIGHTGRAY);
    DrawText(TextFormat("links  up:%d down:%d left:%d right:%d   [<-/->] room   [D] debug:%s",
                        r->room_north, r->room_south, r->room_west, r->room_east,
                        g->debug ? "on" : "off"), 8, 46, 14, GRAY);
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
            DrawText(TextFormat("B%d", i), p.x + 2, p.y + 1, 10, C_BLK);
        } else {
            DrawRectangleLinesEx((Rectangle){p.x, p.y, s, s}, 1, C_SPR);
            DrawText(TextFormat("%s d%d", creature_name(o->sprite.type, o->sprite.damage), o->sprite.damage),
                     p.x + 2, p.y - 11, 10, C_SPR);
        }
    }

    // switches + per-chunk effects
    for (int i = 0; i < r->num_switches; i++) {
        struct SwitchObject *sw = r->switches + i;
        if (sw->chunks.length == 0) continue;
        Vector2 o = tile_pt(g, sw->chunks.data[0].x + 0.5f, sw->chunks.data[0].y + 0.5f);
        DrawCircleV(o, 4, C_SW);
        DrawText(TextFormat("S%d", i), o.x + 5, o.y - 5, 10, C_SW);
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
                    DrawText(TextFormat("R%d", rid), e.x - 8, e.y - 6, 10, C_BIT);
                }
            }
        }
    }
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
        InitWindow(ROOM_PX_W * g->scale, ROOM_PX_H * g->scale + HUD_H, "Spidey Editor — GUI");
        SetTargetFPS(60);
    }
    g->drawn_room = -1;   // force a texture refresh on (re)entry

    while (!WindowShouldClose()) {
        if (sources_changed(last_build)) {
            fprintf(stderr, "gui: source changed, rebuilding\n");
            if (system(GUI_BUILD_CMD) == 0) return g;     // host reloads
            fprintf(stderr, "gui: rebuild failed, not reloading\n");
            last_build = GetFileModTime(library);          // avoid rebuild loop
        }

        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_L) || IsKeyPressed(KEY_PERIOD))
            g->room_id = next_valid(file, g->room_id, +1);
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_H) || IsKeyPressed(KEY_COMMA))
            g->room_id = next_valid(file, g->room_id, -1);
        if (IsKeyPressed(KEY_D)) g->debug = !g->debug;

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

        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(g->tex,
                       (Rectangle){0, 0, ROOM_PX_W, ROOM_PX_H},
                       (Rectangle){0, HUD_H, ROOM_PX_W * g->scale, ROOM_PX_H * g->scale},
                       (Vector2){0, 0}, 0, WHITE);
        if (g->debug) draw_overlay(g, file);
        draw_hud(g, file);
        EndDrawing();
    }

    if (g->tex_loaded) UnloadTexture(g->tex);
    render_close(g->rctx);
    free(g);
    CloseWindow();
    return NULL;
}
