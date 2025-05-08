# spidey-editor: A map editor for The Amazing Spider Man game from 1990

[![patreon](https://img.shields.io/badge/patreon-FF5441?style=for-the-badge&logo=Patreon)](https://www.patreon.com/hughdavenport)
[![youtube](https://img.shields.io/badge/youtube-FF0000?style=for-the-badge&logo=youtube)](https://www.youtube.com/watch?v=LjgvuFc71fo&list=PL5r5Q39GjMDfYcGEZ6O_a0Y1WVbgpv5W-)

This repo is a hobby project implementing a map editor for [The Amazing Spider Man](https://en.wikipedia.org/wiki/The_Amazing_Spider-Man_(1990_video_game). This game was originally developed for the Commodore 64 and Amiga, and later ported to IBM PC compatibles. I personally knew it from DOS (IBM PC). It was developed in a [YouTube series](https://www.youtube.com/watch?v=LjgvuFc71fo&list=PL5r5Q39GjMDfYcGEZ6O_a0Y1WVbgpv5W-)

The editor should theoretically work with the `ROOMS.SPL` file from any installation on any platform. My testing so far has been on the DOS version.

The editor itself runs on linux, and it may run on other platforms. To build, run `make`. This generates `a.out`, which can be run by itself to show rudimentary help. You most likely want to use `./a.out editor` which uses the newer interactive editor. There are lower level commands not yet integrated into the editor which can be used with `./a.out display/patch/delete` which gives you a *very* rudimentary interface, but with the power to do anything.

If you make any cool room files, feel free to share them. I have the technical skills, less so much the level design skills :D

Please leave any comments about what you liked. Feel free to suggest any features or improvements.

You are welcome to support me financially if you would like on my [patreon](https://www.patreon.com/hughdavenport).
