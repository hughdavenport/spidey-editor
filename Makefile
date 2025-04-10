all: main.c room.c room.h array.h editor.c
	$(CC) -ggdb -Wextra -Werror -Wall -Wpedantic -fsanitize=address main.c room.c editor.c
