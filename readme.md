# Moose-vs-Goose

This project is meant to help others understand a valid way of bringing many
related SDL technologies together. These are:

- C
- CMake
- SDL3 (using the Callbacks system)
- SDL_Mixer
- SDL_TTF
- SDL_Image
- Web builds - Emscripten
- Native builds

## Overview

This project is a C project that is built with CMake. All of the SDL dependencies are
downloaded into your build folder. Further, the project works with the Emscripten platform as well as native.

## Build Instructions

For Native builds:

1. make a `build` folder, and `cd` to it.
2. run `cmake ..`
3. run `make`
4. run the executable `./bralltog`

For browser builds using Emscripten

0. ensure `emsdk` is installed and sourced to your shell.
1. make an `emscripten` folder.
2. copy the `runserver.sh` script to it. This assumes python is present.
3. navigate to your new `emscripten` folder.
4. run `emcmake cmake ..`
5. run `emmake make && ./runserver.sh`

## Final Words

I hope this helps newcomers to SDL development in C get started more quickly.

## Credits

Credit where credit is due.

1. The `SDL Enthusiasts` Discord. What a great community. Be sure to sign up.
2. This project for helping me get started with Emscripten and CMake.
  (https://github.com/TechnicJelle/TetsawSDL3/blob/main/CMakeLists.txt)
3. This project, whose source helped many to get audio to work with Emscripten.
  (https://github.com/libsdl-org/SDL/issues/6385)
