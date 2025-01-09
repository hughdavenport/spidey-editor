all: room.c editor.c
	cc -ggdb -Werror -Wall -Wpedantic -fsanitize=address room.c editor.c
