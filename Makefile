all: room.c editor.c
	$(CC) -ggdb -Wextra -Werror -Wall -Wpedantic -fsanitize=address room.c editor.c
