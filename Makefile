all: room.c editor.c
	cc -ggdb -Wextra -Werror -Wall -Wpedantic -fsanitize=address room.c editor.c
