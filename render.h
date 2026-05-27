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

#endif /* RENDER_H */
