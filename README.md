# spidey-editor: A map editor for The Amazing Spider Man game from 1990

[![patreon](https://img.shields.io/badge/patreon-FF5441?style=for-the-badge&logo=Patreon)](https://www.patreon.com/hughdavenport)
[![youtube](https://img.shields.io/badge/youtube-FF0000?style=for-the-badge&logo=youtube)](https://www.youtube.com/watch?v=LjgvuFc71fo&list=PL5r5Q39GjMDfYcGEZ6O_a0Y1WVbgpv5W-)

This repo is a hobby project implementing a map editor for [The Amazing Spider Man](https://en.wikipedia.org/wiki/The_Amazing_Spider-Man_\(1990_video_game\)). This game was originally developed for the Commodore 64 and Amiga, and later ported to IBM PC compatibles. I personally knew it from DOS (IBM PC). It was developed in a [YouTube series](https://www.youtube.com/watch?v=LjgvuFc71fo&list=PL5r5Q39GjMDfYcGEZ6O_a0Y1WVbgpv5W-)

The editor should theoretically work with the `ROOMS.SPL` file from any installation on any platform. My testing so far has been on the DOS version.

The editor itself runs on linux, and it may run on other platforms. To build:
```shell
make
```

To show help:
```shell
./a.out
```

To use interactive editor. `ROOMS.SPL` is updated automatically. The inbuilt help can be accessed with the `?` character.
```shell
./a.out editor
```

There are more advanced subcommands which give a *very* rudimentary interface, but with the power to do anything. Recommend saving `ROOMS.SPL` before running any of these:
```shell
./a.out rooms
./a.out display [roomid]
./a.out patch [roomid] [PATCH INSTRUCTIONS]
./a.out delete [roomid] [THING]
```

There is limited help for these commands, but the source can help ... In general what you see in output of `display` can likely get used as an input for `THING`. Here are some examples:
 - `./a.out display`
 - `./a.out display 1`
 - `./a.out patch 1 objects[1].sprite mummy`
 - `./a.out patch 1 gravity_vertical 0`
 - `./a.out patch 1 name "My test"`
 - `./a.out patch 1 tile[idx] 0x41` or `./a.out patch 1 tile[x][y] 0x41`
 - `./a.out patch 1 tiles [filename]`
 - `./a.out patch 1 switches[3].chunks[1].on 131`
 - `./a.out patch 1 objects[1].sprite mummy`
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

You can also create *new* things, by using the index exactly one above the last. More and it will corrupt your file. In the future you will be able to use `[]` for an index, which will automatically choose the 1 above.
 - `./a.out patch 1 switches[].x 1 .y 13 .chunks[].type TOGGLE_OBJECT .index 0 .test 0x20 .value left`

If you make any cool room files, feel free to share them. I have the technical skills, less so much the level design skills :D

Please leave any comments about what you liked. Feel free to suggest any features or improvements.

You are welcome to support me financially if you would like on my [patreon](https://www.patreon.com/hughdavenport).
