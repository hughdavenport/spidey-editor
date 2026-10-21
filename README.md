# spidey-editor: A map editor for The Amazing Spider Man game from 1990

[![patreon](https://img.shields.io/badge/patreon-FF5441?style=for-the-badge&logo=Patreon)](https://www.patreon.com/hughdavenport)
[![youtube](https://img.shields.io/badge/youtube-FF0000?style=for-the-badge&logo=youtube)](https://www.youtube.com/watch?v=LjgvuFc71fo&list=PL5r5Q39GjMDfYcGEZ6O_a0Y1WVbgpv5W-)

This repo is a hobby project implementing a map editor for [The Amazing Spider Man](https://en.wikipedia.org/wiki/The_Amazing_Spider-Man_\(1990_video_game\)). This game was originally developed for the Commodore 64 and Amiga, and later ported to IBM PC compatibles. I personally knew it from DOS (IBM PC). It was developed in a [YouTube series](https://www.youtube.com/watch?v=LjgvuFc71fo&list=PL5r5Q39GjMDfYcGEZ6O_a0Y1WVbgpv5W-)

Working out the game's file formats got a hand from [Spider-claude](https://github.com/hughdavenport/Spider-claude), an early experiment in using Claude to do the reverse engineering.

The editor should theoretically work with the `ROOMS.SPL` file from any installation on any platform. My testing so far has been on the DOS version.

The game's files are copyrighted and are not in this repo: point the editor at your own copy of the game. To try it without one, `docs/demo` can generate a stand-in data set (see [Trying it without the game](#trying-it-without-the-game)).

More detail than this README has: [docs/USAGE.md](docs/USAGE.md) (every command and key) and [docs/SWITCHES.md](docs/SWITCHES.md) (how the game's switches work and how to edit them).

## Building

The editor runs on linux, and it may run on other platforms. It needs a C compiler; [raylib](https://www.raylib.com/) 6.0, used by the graphical editor, is included in `raylib-6.0_linux_amd64/`.

```shell
make            # ./a.out: debug build with AddressSanitizer; the editors hot-reload as you edit the source
make release    # ./spidey-editor: one optimised binary, no hot reload
```

`./spidey-editor` takes all the same commands as `./a.out` below.

## The editors

```shell
./a.out editor                  # the graphical editor when there is a display, else the text one
./a.out gui [--scale N]         # the graphical editor
./a.out tui                     # the text-mode editor, for a terminal (or over ssh)
```

Both edit the same way - the graphical one runs the text editor's keys and adds a picture of the room, a mouse, a world map, and editors for the game's tiles, sprites, backgrounds, font and full-screen pictures. `ROOMS.SPL` is saved as you go. The inbuilt help comes up with `Ctrl-?` (or `?`).

Every command reads `ROOMS.SPL`, `MOVERTAB.DAT`, `BACKS.SPL` and the graphics from one data directory: the working directory, or another given with `--data-dir` (anywhere on the command line):

```shell
./a.out editor --data-dir ~/games/spider
```

The graphical editor and `render` need the game's graphics (`BLOCKS.EGA`, `SPIDS1.EGA`, ...) there; the text editor and the other commands need only `ROOMS.SPL`.

`Ctrl-P` in either editor plays the room you are editing: `play.sh` copies your files into the game's directory (`Spider/`, or `$SPIDEY_DIR`), puts that room at the start, and runs the game in [DOSBox-X](https://dosbox-x.com/). It drives the game's menus with `xdotool` and waits for the window with `i3-msg`.

## Commands

```shell
./a.out help
```

```shell
./a.out rooms                                   # list the rooms
./a.out display [ROOMID]                        # dump rooms as text (all of them by default)
./a.out import [FILE|-] [OUT.SPL]               # build a whole file from display's text
./a.out importRoom ROOMID [FILE|-] [OUT.SPL]    # replace one room from display's text
./a.out patch ROOMID THING VALUE...             # change a room (see below)
./a.out delete ROOMID THING                     # delete a switch, chunk or object
./a.out swap A B [--movertab FILE]              # exchange two rooms, links and all
./a.out recompress                              # rewrite ROOMS.SPL without changing anything
./a.out render ROOMID|all|montage OUT|DIR       # rooms to PNG (--debug, --scale N)
./a.out find_tile TILE [TILESET]                # rooms using a tile (0-63; tileset 0, 4, ... 28)
./a.out find_sprite TYPE                        # rooms with a sprite type (RAT, MUMMY, ...)
```

Commands that change things write `ROOMS.SPL` in the data directory, or the `OUT.SPL` given last. Recommend saving `ROOMS.SPL` before running any of these. Every save recomputes the per-room object and switch counts the game indexes `MOVERTAB.DAT` and its switch states by, and a save that would not fit in the game's limits is refused, leaving the file on disk as it was.

`display` and `import` use the same text, so `display > rooms.txt`, hand-edit `rooms.txt`, then `import rooms.txt` is a legitimate (if old-fashioned) way to edit, and an easy target for a script that generates levels.

There is limited help for `patch` and `delete`, but the source can help ... In general what you see in output of `display` can likely get used as an input for `THING`. Here are some examples:
 - `./a.out display`
 - `./a.out display 1`
 - `./a.out patch 1 objects[1].sprite mummy`
 - `./a.out patch 1 gravity_vertical 0`
 - `./a.out patch 1 name "My test"`
 - `./a.out patch 1 tile[idx] 0x41` or `./a.out patch 1 tile[x][y] 0x41`
 - `./a.out patch 1 tiles [filename]`
 - `./a.out patch 1 switches[3].chunks[1].on 131`
 - `./a.out patch 1 objects[0].tiles[idx] val` or `./a.out patch 1 objects[0].tiles[x][y] val`
 - `./a.out patch 1 objects[0].tiles [filename]`
 - `./a.out delete 1 objects[0]`
 - `./a.out delete 1 switches[0]`
 - `./a.out delete 1 switches[0].chunks[1]`

Here the `1` signifies room 1, which is `Midnight` in the original game. The following may be plurals or singular: `tile/tiles`, `switch/switches`, `object/objects`. Some misspellings are permitted.

You can also chain together patch instructions. `.` means operate on the same `thing`, whereas `..` means operate on the `thing` above the current one. For example:
 - `./a.out patch 1 objects[0].width 0 .height 0 .sprite mummy .type sprite`
 - `./a.out patch 1 switch[1].chunks[1].y 1 ..chunks[2].type TOGGLE_BLOCK`
 - `./a.out patch 1 switch[1].chunks[1].x 1 ..chunks[2].index 1 gravity_vertical 1`

You can also create *new* things, by using the index exactly one above the last, or using `[]` for an index, which will automatically choose the 1 above.
 - `./a.out patch 1 switches[].x 1 .y 13 .chunks[].type TOGGLE_OBJECT .index 0 .test 0x20 .value left`

You can use the same to delete things:
 - `./a.out delete 1 objects[]`

## Trying it without the game

`docs/demo/gen_demo.py` draws a complete stand-in for the game's graphics (tiles, sprites, backgrounds, font, full-screen pictures, EGA and CGA) and `docs/demo/rooms.txt` is a small three-room level with every kind of switch in it:

```shell
python3 docs/demo/gen_demo.py demo
(cd demo && ../a.out import ../docs/demo/rooms.txt ROOMS.SPL)
./a.out editor --data-dir demo
```

The screenshots in `docs/` are of this data set.

## Tests

```shell
make check                          # .SPL codec, background store, save limits, import (C, spl_check.c)
python3 tui_smoke_test.py           # drives the text editor in a pseudo-terminal
./gui_smoke_test.sh                 # drives the graphical editor on Xvfb (needs Xvfb, xdotool, ImageMagick)
python3 docs/demo/test_gen_demo.py  # the demo data decodes to what was drawn
docs/demo/screenshots.sh            # retakes the screenshots in docs/screenshots
```

None of them need the game: each builds the demo data set in a scratch directory and works there, so your own files are never touched. `make check` also repeats its checks on a scratch copy of `Spider/` when there is one. Run `make` first.

## Feedback

If you make any cool room files, feel free to share them. I have the technical skills, less so much the level design skills :D

Please leave any comments about what you liked. Feel free to suggest any features or improvements.

You are welcome to support me financially if you would like on my [patreon](https://www.patreon.com/hughdavenport).
