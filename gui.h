#ifndef GUI_H
#define GUI_H

#include "room.h"

// Build command for the hot-reloadable GUI shared library (gui.so). Links
// raylib (shared) so the dlopen'd library can call it directly; the host keeps
// libraylib resident across reloads. Run from the repo root.
#define RAYLIB_DIR "raylib-6.0_linux_amd64"
#define GUI_BUILD_CMD \
    "cc -ggdb -Wall -Wextra -fsanitize=address -fpic -shared " \
    "-I " RAYLIB_DIR "/include render.c room.c spl.c gui.c -o gui.so " \
    "-Wl,-rpath,'$ORIGIN/" RAYLIB_DIR "/lib' -L " RAYLIB_DIR "/lib -lraylib -lm"

// Host entry: open the raylib GUI on `file`, reading assets from data_dir.
int gui_main(RoomFile *file, const char *data_dir);

// Shared-library entry (in gui.so). Runs the frame loop until the window is
// closed (returns NULL) or a watched source file changed (returns the state so
// the host can reload). `state` is NULL on first call.
void *gui_loop(const char *library, RoomFile *file, const char *data_dir, void *state);

#endif /* GUI_H */
