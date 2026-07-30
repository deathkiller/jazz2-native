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

The configure output must contain `PS Vita audio: OpenAL and libopenmpt
enabled`. If it does not, do not package the build: its `build.ninja` lacks
`WITH_AUDIO`, so it cannot produce sound. Delete the previous build directory
and configure it again with the audio options above.

VitaSDK's `vita-pack-vpk` does not quote asset paths. Build from a source and
output path without spaces. When the repository is under a Windows directory
with spaces, use a WSL symlink such as `ln -s /mnt/c/.../jazz2\ vita
/root/jazz2-vita` and configure from `/root/jazz2-vita`.

`icon0.png` is Vita-specific: it must be an opaque 8-bit indexed PNG. The
package uses `Sources/Icons/VitaIcon.png`; do not replace it with a truecolor
or RGBA PNG, or VitaShell will reject the VPK with `0x8010113D`.

## WSL Audio Debug Build

This is the reproducible command sequence used for Vita audio development. It
creates a separate build directory so an audio build cannot reuse cached values
from a previous graphics-only configuration.

The source and build paths must not contain spaces. If the Windows checkout has
spaces in its path, expose it in WSL through a symlink:

```sh
ln -s "/mnt/c/Users/ASvinin/Desktop/opencode/jazz2 vita" /root/jazz2-vita
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
```

### Optional: VitaGL Without Its Startup Screen

VitaGL displays its own startup screen for at least one second whenever
`vglInit()` runs. Build a private static library with its supported
`NO_SPLASHSCREEN=1` flag to remove it. This is linked into the VPK; it does not
modify the installed VitaSDK.

```sh
git clone --depth 1 https://github.com/Rinnegatamante/vitaGL.git /root/vitaGL
make -C /root/vitaGL clean
make -C /root/vitaGL NO_SPLASHSCREEN=1 --jobs "$(nproc)"
arm-vita-eabi-strip --strip-debug /root/vitaGL/libvitaGL.a
```

The project accepts this library through `NCINE_VITAGL_LIBRARY`. Omit that
argument to use VitaSDK's normal `libvitaGL.a`, which retains the VitaGL splash.

### Configure And Package

Delete the directory before changing audio switches. `CMakeCache.txt` preserves
options from a prior configuration.

```sh
rm -rf /root/jazz2-vita-audio-build

cmake -S /root/jazz2-vita -B /root/jazz2-vita-audio-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DNCINE_PREFERRED_BACKEND=SDL2 \
  -DNCINE_PREFERRED_RHI=OpenGL \
  -DNCINE_WITH_AUDIO=ON \
  -DNCINE_WITH_OPENMPT=ON \
  -DNCINE_WITH_VORBIS=OFF \
  -DWITH_MULTIPLAYER=OFF \
  -DNCINE_DOWNLOAD_DEPENDENCIES=OFF \
  -DNCINE_VITAGL_LIBRARY=/root/vitaGL/libvitaGL.a \
  -DVITA_TITLEID=JAZZ2AUD1 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build /root/jazz2-vita-audio-build --parallel
unzip -t /root/jazz2-vita-audio-build/jazz2.vpk
```

`JAZZ2AUD1` is a nine-character test title ID. Reuse the same ID to install an
update over the previous audio test, or change it to another unique nine-letter
or digit ID to retain multiple builds on the Vita.

The VPK is `jazz2.vpk` in the build directory. Copy it to the shared Windows
workspace if needed:

```sh
cp /root/jazz2-vita-audio-build/jazz2.vpk \
  "/mnt/c/Users/ASvinin/Desktop/opencode/jazz2 vita/build-vita-p0-ubuntu24/Jazz2Vita-audio-debug.vpk"
```

Successful configuration prints:

```text
PS Vita audio: OpenAL and libopenmpt enabled; Vorbis=OFF
```

Treat a missing line, an OpenAL error, or a libopenmpt error as a failed audio
configuration. Install the missing VitaSDK development library and configure
again; do not turn audio off for an audio test. `Content` is packaged into the
VPK automatically. Original JJ2 data remains external at
`ux0:/data/Jazz2/Source/`.

## Performance Log

Vita builds create `ux0:/data/Jazz2/VitaPerformance.log` at startup and write a
throttled slow-frame diagnostic there. After at least 0.5 seconds below 25 FPS, it records
one entry at most every two seconds, for example:

```text
VitaPerf: 31.2 ms, 32.1 FPS; draws 184 (sprite 143, tile 12, particle 24, text 5), vertices 736, transparent 171
```

`sprite` rising when an object enters view points to actors, decorations, or UI;
`particle` points to particle effects; `tile` points to tile layers; `light` points
to dynamic lighting; `mesh` covers mesh sprites; `other` identifies commands not
yet categorized by their caller. The counters
are gathered from the previous rendered frame, so walk into the problematic
area, wait for the FPS drop, then exit and copy `VitaPerformance.log` for analysis.
The Vita build also displays the measured FPS and last frame time in the upper
left corner.

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
