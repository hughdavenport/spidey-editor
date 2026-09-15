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

// Open a render context, loading the graphics and BACKS.SPL from data_dir
// once: the .EGA files (16 colours), or with `cga` the .CGA files (the same
// pictures packed 2 bits per pixel, 4 colours: black, cyan, magenta, white).
// Returns NULL (with a message on stderr) if an asset is missing.
RenderCtx *render_open(const char *data_dir, bool cga);
bool render_is_cga(RenderCtx *ctx);
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
// A "source" is one file of fixed-size images: BLOCKS (512 8x8 tiles),
// SPIDS1 / SPIDS2 (64 24x21 sprite frames, the last palette colour transparent:
// 15 in EGA, 3 in CGA), SCENERY (256 8x8 background tiles); .EGA or .CGA
// depending on the context's mode. Colours are palette indices, 0..15 or 0..3.
// Sheets: BLOCKS, SPIDS1, SPIDS2, SCENERY, CHARSET (the 8x8 font, 128 tiles).
// Screens: one full picture each, row-planar in EGA (LOADSCRN title, WINSCRN
// ending, PAGE1-4 story, 320x200; HISCORE 320x38; SPIDFIGR / SKELFIGR health
// figures, 64x192). The four sheets the rooms need must exist; the rest are
// optional (render_gfx_available).
// CGA only: MASKS1 / MASKS2 hold one AND mask per SPIDS frame (3 = keep the
// background, 0 = the sprite's pixel is drawn), since CGA has no spare colour
// to key on. In CGA mode a sprite pixel's colour is 4 = "transparent" when its
// mask says so; painting colour 4 sets the mask, any other colour clears it.
// The masks travel with the sprites: undo records, reset and save cover both.
typedef enum { GFX_TILES, GFX_SPIDS1, GFX_SPIDS2, GFX_SCENERY, GFX_CHARSET,
               GFX_LOADSCRN, GFX_WINSCRN, GFX_PAGE1, GFX_PAGE2, GFX_PAGE3, GFX_PAGE4,
               GFX_HISCORE, GFX_SPIDFIGR, GFX_SKELFIGR, GFX_MASKS1, GFX_MASKS2, GFX_SOURCES } GfxSource;
#define GFX_MAX_BYTES 32000               // largest per-image record (an EGA 320x200 screen)
bool render_gfx_is_screen(GfxSource src);             // a single picture rather than a sheet
bool render_gfx_available(RenderCtx *ctx, GfxSource src);   // its file was found in the data dir
const char *render_gfx_name(RenderCtx *ctx, GfxSource src);   // file name in this mode, e.g. "BLOCKS.CGA"
int  render_gfx_count(GfxSource src);      // images in the file
void render_gfx_size(GfxSource src, int *w, int *h);
int  render_gfx_bytes(RenderCtx *ctx, GfxSource src);     // bytes per image record
int  render_gfx_key(RenderCtx *ctx, GfxSource src);       // transparent colour (15 EGA sprites, 4 = "masked" CGA sprites), or -1
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
// The mode's palette (16 or 4 RGB triplets) and how many colours it has.
const uint8_t (*render_palette(RenderCtx *ctx))[3];
int  render_colours(RenderCtx *ctx);

// Backgrounds: BACKS.SPL holds up to 64 blocks, each a 32x22 map of SCENERY
// tiles; a room shows block `background` (rooms may share one). Tile 0 is
// never drawn (black). Blocks are cached in the context; render_backs_save()
// recompresses and writes the whole file (one-time <file>.orig backup).
#define BACKS_BLOCKS 64
#define BACKS_FILE_LIMIT 0x2b93   // bytes: the shipped BACKS.SPL's size. The game's buffer limit is unknown; this is the known-safe bound
#define BACKS_TILES (WIDTH_TILES * HEIGHT_TILES)   // 704
int  render_backs_tile(RenderCtx *ctx, int bid, int x, int y);
void render_backs_set_tile(RenderCtx *ctx, int bid, int x, int y, int tile);
bool render_backs_get(RenderCtx *ctx, int bid, uint8_t *out);           // the block (BACKS_TILES bytes), for undo
void render_backs_put(RenderCtx *ctx, int bid, const uint8_t *in);
bool render_backs_original(RenderCtx *ctx, int bid, uint8_t *out);      // as shipped (<file>.orig, or the file on disk)
bool render_backs_save(RenderCtx *ctx);

#define TILESET_PX 64   // 8x8 grid of 8x8 tiles = the 64 tiles of one tileset

// Render the 64 foreground (BLOCKS) tiles for `tile_offset` into `out`
// (TILESET_PX*TILESET_PX*3 RGB), laid out 8 across, tile t at (t%8, t/8).
void render_tileset_rgb(RenderCtx *ctx, int tile_offset, uint8_t *out);

#endif /* RENDER_H */
