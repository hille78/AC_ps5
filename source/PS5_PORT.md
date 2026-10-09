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

## Graphics backend: ps5-vulkan (https://github.com/mpereiraesaa/ps5-vulkan)
Experimental static Vulkan 1.3 SDK (`libps5vk.a`, no loader/ICD) on top of ps5-payload-sdk, with 1080p
VideoOut presentation, SPIR-V shaders compiled at runtime. GPL-3.0-or-later, so a PS5 binary linking it
must be distributed with source under GPL-compatible terms (AssaultCube's zlib-style license allows that).
Firmware support is not stated on its page; check BUILDING.md/API.md before relying on it.
Consequence: the PS5 has no GLES, so the fixed-function shim needs two backends:
- GLES2 backend: for testing on PC.
- Vulkan backend: for PS5, using one uber-shader (GLSL precompiled to SPIR-V) with the same uniforms.
The legacy-GL emulation (matrix stacks, glBegin/glEnd batching, tex-env combine, fog, alpha test) is shared.

## GLES2 shim status (src/glshim.h, src/glshim.cpp) - written, NOT yet compiled or run
Enabled with `-DUSE_GLES2_SHIM` (`src/build_gles2_pc.sh` for a PC test, `src/build_ps5.sh` for the SDK).
Implemented: matrix stacks, glBegin/glEnd batching, client arrays, QUADS->TRIANGLES, one uber shader with
tex-env (modulate/replace/add/decal/blend/combine), 2 texture units, linear fog, alpha test, texture
matrices, ES2 texture-upload fixes (internalformat, BGR), extension probes answered by `shim_getprocaddress`.
Known gaps:
- Display lists are disabled under the shim (static-model caching), so models draw in the slower path.
- `glGetTexImage` (mapshot screenshots) and `glReadPixels(GL_DEPTH_COMPONENT)` (editor cursor depth) are not supported in ES2.
- `glPolygonMode` wireframe, `glShadeModel`, hints, normals: dropped. Fog is linear only.
- Uniforms are re-uploaded whenever any state changes (dirty flag); a per-uniform cache would cut GL calls.
- Header interplay with `GL/glext.h` (typedef/enum redefinitions) is unverified until the first compile.

## Plan
1. Renderer: replace immediate-mode GL with a small shim (batch into vertex arrays, draw with
   GLES2 shaders) behind one header (e.g. `glshim.h`) so desktop builds keep working.
   Validate on PC with a GLES2/ANGLE build first.
2. Input: SDL_GameController support (sticks -> look/move, triggers -> fire/aim) and
   controller-driven menus.
3. Audio: confirm an OpenAL port, else route through SDL audio.
4. Packaging: ship packages/ and config/ next to the ELF; avoid runtime downloads (autodownload.cpp).
5. Networking: enet over BSD sockets should work; check master-server/HTTP paths.
