#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>

#include "room.h"

typedef struct {
    const char *data_dir;   // directory holding the EGA / BACKS.SPL assets
    int scale;              // nearest-neighbour upscale factor (>= 1)
    bool debug;             // draw the debug overlay (switches/objects/grid)
} RenderOpts;

// Render one room to a PNG at out_path. Returns false (with a message on
// stderr) if an asset is missing or the file can't be written.
bool render_room_png(RoomFile *file, int room_id, const char *out_path, RenderOpts opts);

// Render every valid room to out_dir/roomNN.png. Returns false if the
// directory can't be created or any room fails.
bool render_all_png(RoomFile *file, const char *out_dir, RenderOpts opts);

// Render all rooms tiled into a single overview image at out_path.
bool render_montage_png(RoomFile *file, const char *out_path, RenderOpts opts);

/* --- reusable decode API (for the GUI; assets loaded once) --------------- */

#define ROOM_PX_W (WIDTH_TILES * 8)    // 256
#define ROOM_PX_H (HEIGHT_TILES * 8)   // 176

typedef struct RenderCtx RenderCtx;

// Open a render context, loading the EGA/BACKS assets from data_dir once.
// Returns NULL (with a message on stderr) if an asset is missing.
RenderCtx *render_open(const char *data_dir);
void render_close(RenderCtx *ctx);

// Fill `out` (ROOM_PX_W*ROOM_PX_H*3 RGB bytes) with the room's pixels:
// background + foreground + sprites, no debug overlay (the GUI draws its own).
bool render_room_rgb(RenderCtx *ctx, RoomFile *file, int room_id, uint8_t *out);

// Whether render_room_rgb draws Spider-Man at his start position in room 1
// (default true; the GUI draws him at the cursor instead).
void render_show_player(RenderCtx *ctx, bool show);

#define SPIDS_FRAME_W 24
#define SPIDS_FRAME_H 21
// One SPIDS1 frame as SPIDS_FRAME_W*SPIDS_FRAME_H*4 RGBA bytes (key colour -> alpha 0).
void render_spids_rgba(RenderCtx *ctx, int frame, uint8_t *out);
// The frame the game shows for the standing player.
int render_player_frame(void);

// Pixel editing of the game's graphics files in the context's data dir.
// A "source" is one file of fixed-size images: BLOCKS.EGA (512 8x8 tiles),
// SPIDS1.EGA / SPIDS2.EGA (64 24x21 sprite frames, colour 15 transparent).
// Colours are palette indices 0..15; render_palette() gives their RGB.
typedef enum { GFX_TILES, GFX_SPIDS1, GFX_SPIDS2, GFX_SOURCES } GfxSource;
#define GFX_MAX_BYTES 256                 // largest per-image record (a SPIDS frame stride)
const char *render_gfx_name(GfxSource src);
int  render_gfx_count(GfxSource src);      // images in the file
void render_gfx_size(GfxSource src, int *w, int *h);
int  render_gfx_bytes(GfxSource src);      // bytes per image record
int  render_gfx_key(GfxSource src);        // transparent colour, or -1
int  render_gfx_pixel(RenderCtx *ctx, GfxSource src, int idx, int x, int y);
void render_gfx_set_pixel(RenderCtx *ctx, GfxSource src, int idx, int x, int y, int colour);
// Raw record (for undo), the record as shipped (<file>.orig, or the unedited
// file on disk) for "reset", and writing the file back (one-time .orig backup).
bool render_gfx_get(RenderCtx *ctx, GfxSource src, int idx, uint8_t *out);
void render_gfx_put(RenderCtx *ctx, GfxSource src, int idx, const uint8_t *in);
bool render_gfx_original(RenderCtx *ctx, GfxSource src, int idx, uint8_t *out);
bool render_gfx_save(RenderCtx *ctx, GfxSource src);
// One image as RGBA (transparent key -> alpha 0).
void render_gfx_rgba(RenderCtx *ctx, GfxSource src, int idx, uint8_t *out);
const uint8_t (*render_palette(void))[3];
#define TILE_BYTES 32
const uint8_t (*render_palette(void))[3];

#define TILESET_PX 64   // 8x8 grid of 8x8 tiles = the 64 tiles of one tileset

// Render the 64 foreground (BLOCKS) tiles for `tile_offset` into `out`
// (TILESET_PX*TILESET_PX*3 RGB), laid out 8 across, tile t at (t%8, t/8).
void render_tileset_rgb(RenderCtx *ctx, int tile_offset, uint8_t *out);

#endif /* RENDER_H */
