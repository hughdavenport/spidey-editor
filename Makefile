EDITOR_SRCS = editor.c editor_actions.c editor_draw.c editor_input.c editor_screen.c

all: main.c room.c spl.c render.c gui_host.c room.h spl.h render.h gui.h array.h editor_types.h $(EDITOR_SRCS)
	$(CC) -ggdb -Wextra -Werror -Wall -Wpedantic -fsanitize=address main.c room.c spl.c render.c gui_host.c $(EDITOR_SRCS)

# Release: one self-contained binary, no editor.so / gui.so, no hot reload,
# raylib linked statically, optimised, no ASAN. asserts stay (they guard writes).
RAYLIB = raylib-6.0_linux_amd64
RELEASE_SRCS = main.c room.c spl.c render.c gui_host.c gui.c gui_editor.c $(EDITOR_SRCS)
release: $(RELEASE_SRCS) room.h spl.h render.h gui.h gui_bridge.h array.h editor_types.h
	$(CC) -O2 -DSPIDEY_RELEASE -Wall -Wextra -I $(RAYLIB)/include $(RELEASE_SRCS) $(RAYLIB)/lib/libraylib.a -lm -lpthread -ldl -lX11 -o spidey-editor

# Round-trip checks of the .SPL compressor and the background store, against
# the game data in Spider/ (copied to a scratch dir; nothing there is touched).
check: spl_check.c spl.c spl.h render.c render.h room.c room.h array.h
	$(CC) -ggdb -Wextra -Werror -Wall -Wpedantic -fsanitize=address spl_check.c spl.c render.c room.c -o spl_check
	ASAN_OPTIONS=detect_leaks=0 ./spl_check Spider ROOMS.SPL LEVELSET.SPL Spider/BACKS.SPL

.PHONY: release check
