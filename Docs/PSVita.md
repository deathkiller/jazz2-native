# PS Vita Port

## Status

This branch contains a first playable PS Vita build. It uses SDL2 for the
application and input backend, vitaGL over SceGxm for graphics, and creates a
VPK through VitaSDK CMake helpers. The current Vita version builds, starts, and
renders the menu and gameplay graphics; the Vita path disables the expensive
runtime rescale shaders and simplified the main-menu background to stay within
vitaGL/GXM limits.

Audio is enabled by default when OpenAL and libopenmpt are present in VitaSDK.
If either dependency is missing, configure with `-DNCINE_WITH_AUDIO=OFF` for a
graphics-only test build.

## Main Problems And Decisions

| Priority | Problem | Decision | Completion check |
| --- | --- | --- | --- |
| P0 | Cross-compilation and packaging | Build with VitaSDK's CMake toolchain; create `.self` and `.vpk` with `vita_create_self()` and `vita_create_vpk()` | A VPK installs in VitaShell and starts |
| P0 | Vita GPU supports an ES 2.0-level API | Use vitaGL and the strict ES 2.0 shader/RHI profile; do not use desktop GL, GLES 3, Vulkan, or D3D11 paths | Menu and one level render without GL errors |
| P0 | SDL does not own the vitaGL context | Initialize and present through `vglInit()` / `vglSwapBuffers()`; keep the viewport fixed at 960x544 | No crash during first rendered frame; no cropped scene |
| P0 | Game and original assets are not redistributable | Keep built-in resources and user-provided JJ2 files separate in `ux0:/data/Jazz2/` | First run finds files in `Source/` and recreates cache |
| P1 | Limited RAM, CPU, and GPU bandwidth | Start with 960x544, release mode, no runtime shader rescaling, no binary shader cache, and no online multiplayer | Stable frame pacing and no out-of-memory failures in representative levels |
| P1 | Vita controls and lifecycle differ from desktop | Map SDL controller events, enable analog/touch sampling, use PlayStation button labels, and verify suspend/resume | Menu, gameplay, pause, sleep/resume, and exit work |
| P1 | Audio backend and module decoder must be supplied by VitaSDK | Enable OpenAL and libopenmpt; keep Vorbis off because the original assets use `.j2b` and WAV | Menu and level music plus concurrent SFX play without underruns |
| P2 | Network transport is costly and untested | Disable online multiplayer for the first playable build | Single-player regression test passes |

## Build Prerequisites

- VitaSDK with `arm-vita-eabi-gcc`, CMake integration, SDL2, vitaGL,
  vitashark, OpenAL, libopenmpt, and the listed Sce stub libraries. The CMake
  configure step fails explicitly when audio is enabled but either OpenAL or
  libopenmpt cannot be found.
- CMake 3.15 or newer and Ninja on the build host.
- Original Jazz Jackrabbit 2 data. Do not add it to this repository or VPK.
- `ur0:/data/libshacccg.suprx` on the Vita. vitaGL requires this runtime shader
  compiler; install it with PIB Configuration Tool or extract it with
  ShaRKF00D before launching the game. The copy in
  `ur0:/patch/PCSI00011/module/` is part of PSM and is not the path used by
  vitaGL.

## First Playable Build

From a shell where `VITASDK` is set:

```sh
cmake -S . -B build-vita -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DNCINE_PREFERRED_BACKEND=SDL2 \
  -DNCINE_PREFERRED_RHI=OpenGL \
  -DNCINE_WITH_AUDIO=ON \
  -DNCINE_WITH_OPENMPT=ON \
  -DNCINE_WITH_VORBIS=OFF \
  -DWITH_MULTIPLAYER=OFF \
  -DNCINE_DOWNLOAD_DEPENDENCIES=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita
```

The result is `build-vita/jazz2.vpk`. The default title ID is `JAZZ2VITA` and
can be changed with `-DVITA_TITLEID=XXXXXXXXX`; it must be exactly nine
uppercase letters or digits.

VitaSDK's `vita-pack-vpk` does not quote asset paths. Build from a source and
output path without spaces. When the repository is under a Windows directory
with spaces, use a WSL symlink such as `ln -s /mnt/c/.../jazz2\ vita
/root/jazz2-vita` and configure from `/root/jazz2-vita`.

`icon0.png` is Vita-specific: it must be an opaque 8-bit indexed PNG. The
package uses `Sources/Icons/VitaIcon.png`; do not replace it with a truecolor
or RGBA PNG, or VitaShell will reject the VPK with `0x8010113D`.

## Device Layout

Install the VPK with VitaShell, then create these directories:

```text
ux0:/data/Jazz2/Source/
ux0:/data/Jazz2/Cache/
```

The VPK includes the project's distributable `Content` files. Copy files from a
legally owned original Jazz Jackrabbit 2 installation to `Source/`.
`Cache/` is generated on the first launch and can be deleted to rebuild it.

## Controls

| Vita input | Action |
| --- | --- |
| D-pad or left analog stick | Move, aim, menu navigation |
| Cross | Jump / confirm |
| Square or R trigger | Fire |
| Circle, L shoulder, or L trigger | Run |
| Triangle | Change weapon |
| Start | Pause / game menu |

Existing Vita preference files from older test builds are reset to the updated
default mapping on first launch of this version.

## Execution Order

1. Install the toolchain and build the P0 configuration above.
2. Fix all compiler and linker failures without changing desktop targets.
3. Test VPK installation, first-run import, menu, a level, controls, and exit
   on hardware; retain `ux0:/data/Jazz2/` logs when a test fails.
4. Measure frame time and memory in representative levels; disable or simplify
   costly effects before changing core gameplay.
5. Verify music, concurrent sound effects, and suspend/resume with the OpenAL
   device. If the VitaSDK lacks OpenAL or libopenmpt, use
   `-DNCINE_WITH_AUDIO=OFF` until those libraries are installed; do not ship a
   build that silently omits module music.
