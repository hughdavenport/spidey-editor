EDITOR_SRCS = editor.c editor_actions.c editor_draw.c editor_input.c editor_screen.c

all: main.c room.c spl.c room.h spl.h array.h editor_types.h $(EDITOR_SRCS)
	$(CC) -ggdb -Wextra -Werror -Wall -Wpedantic -fsanitize=address main.c room.c spl.c $(EDITOR_SRCS)
