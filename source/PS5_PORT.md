# AssaultCube -> PS5 (homebrew, personal console)

Target: ps5-payload-sdk (https://github.com/ps5-payload-dev/sdk), run as an ELF payload.
Status: scaffolding only. Nothing here has been built or run on hardware.

## Known from the code
- Window/GL setup: `src/main.cpp` (SDL_CreateWindow + SDL_GL_CreateContext, ~lines 600-650).
- Fixed-function GL (`glBegin` etc.): rendergl.cpp (14 uses), renderhud.cpp (11), plus texture,
  rendertext, menus, entities, editing, renderparticles, shadow, vertmodel.h.
- Mouse look via `SDL_SetRelativeMouseMode` (main.cpp ~135).
- `src/build_ps5.sh` wraps the existing Makefile with the SDK cross-compiler (untested).

## Assumptions to verify (not checked)
- Which of SDL2 / SDL2_image / OpenAL / vorbis / zlib exist as SDK ports.
- Whether the SDK gives a usable GLES2/3 (EGL) path or only raw VideoOut/AGC.

## Plan
1. Renderer: replace immediate-mode GL with a small shim (batch into vertex arrays, draw with
   GLES2 shaders) behind one header (e.g. `glshim.h`) so desktop builds keep working.
   Validate on PC with a GLES2/ANGLE build first.
2. Input: SDL_GameController support (sticks -> look/move, triggers -> fire/aim) and
   controller-driven menus.
3. Audio: confirm an OpenAL port, else route through SDL audio.
4. Packaging: ship packages/ and config/ next to the ELF; avoid runtime downloads (autodownload.cpp).
5. Networking: enet over BSD sockets should work; check master-server/HTTP paths.
