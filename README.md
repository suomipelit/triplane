# Triplane Classic - a side-scrolling dogfighting game

## Intro

![Triplane GIF](https://github.com/suomipelit/suomipelit.github.io/blob/master/gifs/triplane.gif)

Triplane Classic is a side-scrolling dogfighting game featuring solo
missions and multiplayer mode with up to four players. It is a port of
the original Triplane Turmoil game for DOS and aims to match the
original game exactly so that high scores remain comparable to the
original.

Please read the manual page for information on how to actually play
the game.

## Copyrights

The original Triplane Turmoil was developed by Markku Rankala, Teemu
J. Takanen and Henrikki Merikallio. Some work was also contributed by
Risto Puhakka, Antti Lehtoranta and Mikko Kinnunen. 

The portable SDL version was created from the original source by Timo
Juhani Lindfors (timo.lindfors@iki.fi), Teemu J. Takanen
(tjt@users.sourceforge.net), and Riku Saikkonen. We also thank Timo Lilja
for his earlier Xlib porting efforts and Sami Liedes for
spellchecking and optimization ideas.

Dodekaedron Software Creations Oy is the current copyright holder for
the Triplane Classic source code, documentation, images and sounds. In
2009 it granted a license to distribute these under the terms of the
GNU General Public License version 3 or later.

SDL2 porting and CMake build scripts were done by Suomipelit Organization.

## Trademarks

Triplane Turmoil is a trademark of Dodekaedron Software
Creations Oy.

Triplane Turmoil 2 is a trademark of Draconus Entertainment Ltd.

Triplane Classic is not a trademark. However, if you make substantial
modifications that, for example, change the scoring system, we
encourage you to pick a new name for your modified game so that users
are not confused.

## Compiling from source

[![Build Status](https://api.travis-ci.org/suomipelit/triplane.svg?branch=master)](https://travis-ci.org/suomipelit/triplane)

### Requirements

- CMake
- C++ compiler: At least gcc, clang and Visual Studio are supported
- Libraries: SDL2, SDL2_mixer
  - On macOS, you can install these with Homebrew. `brew install sdl2 sdl2_mixer`
  - On Windows you can download these from SDL website

### Building

```shell
mkdir bin
cd bin
cmake ..
cmake --build .
```

On Windows, you may need to explicitly specify paths to your SDL libraries, like
```shell
cmake -DSDL2_PATH="C:\\<path>\\SDL2-2.0.9" -DSDL2_MIXER_PATH="C:\\<path>\\SDL2_mixer-2.0.4" ..
```
which produces project files for 32-bit target. For 64-bit target, use e.g. `cmake -G "Visual Studio 15 2017 Win64"`.

#### Browser build

Emscripten JavaScript/WebAssembly build for browsers is also
supported. It has limitations like missing sound and persistent data
storing, and the port is considered as beta. You can try it live
[here](https://suomipelit.github.io/triplane-web/).

`fokker.dks` main data file must be built separately or borrowed from
release package. Paste it to binary directory before progressing
further.

``` shell
cmake -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=<path to>/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake ..
cmake --build .
```

For best performance it is recommended to build with
`-DCMAKE_BUILD_TYPE=Release`.

It might be easiest to run local HTTP server with Python

``` shell
python -m http.server
```

and opening the game with supported browser at `http://localhost:8000/triplane.html`.

## Releases

### v1.0.8-SP1 - 2019-12-14

- Update to SDL2
- Add fullcreen option to GUI
- Toggle fullcreen with ALT + Enter
- Fix split transition
- Fix start-up problem if no player was created
- Minor maintenance

## Contact

Teemu J. Takanen <tjt@users.sourceforge.net>

Timo Juhani Lindfors <timo.lindfors@iki.fi>

[Suomipelit Slack](https://join.slack.com/t/suomipelit/shared_invite/enQtNDg1ODkwODU4MTE4LWExY2Q3Mjc0ODg3OTY3ZjlmYThkZDRlMDBjZWUwM2I4NWZlZTFkMWI4YjM1OTM1ODQ4NGQ1NGFiNjQ5MjY0NzM)
