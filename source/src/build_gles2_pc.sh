#!/bin/sh
# Build the client with the GLES2 fixed-function shim on a Linux PC (Mesa or ANGLE GLES2) to test the
# shim before touching the PS5. UNTESTED. Needs SDL2 dev headers incl. SDL_opengles2.h, and libGLESv2.
set -e
exec make client \
    CLIENT_INCLUDES="-I. -Ibot -I../enet/include -I/usr/include $(sdl2-config --cflags) -idirafter ../include -DUSE_GLES2_SHIM" \
    CLIENT_LIBS="-L../enet/.libs -lenet $(sdl2-config --libs) -lSDL2_image -lz -lGLESv2 -lopenal -lvorbisfile" \
    "$@"
