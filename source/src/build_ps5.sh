#!/bin/sh
# Experimental PS5 homebrew build (ps5-payload-sdk). UNTESTED scaffold.
# Usage: PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk ./build_ps5.sh
# Needs SDL2, SDL2_image, zlib, OpenAL (or an SDL audio shim) and libvorbisfile
# in the SDK sysroot (e.g. via pacbrew). Verify availability first.
set -e
: "${PS5_PAYLOAD_SDK:?set PS5_PAYLOAD_SDK to your ps5-payload-sdk install dir}"
SYSROOT="$PS5_PAYLOAD_SDK/target"
exec make client \
    CXX="$PS5_PAYLOAD_SDK/bin/prospero-clang++" \
    CLIENT_INCLUDES="-I. -Ibot -I../enet/include -idirafter ../include -I$SYSROOT/include -I$SYSROOT/include/SDL2 -DPS5" \
    CLIENT_LIBS="-L../enet/.libs -L$SYSROOT/lib -lenet -lSDL2 -lSDL2_image -lz -lopenal -lvorbisfile -lGLESv2 -lEGL" \
    "$@"
