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

#endif /* RENDER_H */
