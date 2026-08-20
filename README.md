<div align="center">
    <a href="https://github.com/deathkiller/jazz2-native"><img src="https://raw.githubusercontent.com/deathkiller/jazz2/master/Docs/Logo.gif" alt="Jazz² Resurrection" title="Jazz² Resurrection"></a>
</div>

<div align="center">
    Open-source <strong>Jazz Jackrabbit 2</strong> reimplementation
</div>

<div align="center">
  <sub>
    Brought to you by <a href="https://github.com/deathkiller">@deathkiller</a>
  </sub>
</div>
<hr/>


## Introduction
Jazz² Resurrection is reimplementation of the game **Jazz Jackrabbit 2** released in 1998. Supports various versions of the game (Shareware Demo, Holiday Hare '98, The Secret Files and Christmas Chronicles). Also, it partially supports some features of JJ2+ extension and MLLE. This repository contains fully rewritten game in C++ with better performance and many improvements. Further information can be found [here](https://de4th.dev/jazz2/).

[![Build Status](https://img.shields.io/github/actions/workflow/status/deathkiller/jazz2-native/linux.yml?branch=master&logo=data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCI+PHBhdGggZmlsbD0iI2ZmZmZmZiIgZD0iTTI0IDIuNXYxOUwxOCAyNCAwIDE4LjV2LS41NjFsMTggMS41NDVWMHpNMSAxMy4xMTFMNC4zODUgMTAgMSA2Ljg4OWwxLjQxOC0uODI3TDUuODUzIDguNjUgMTIgM2wzIDEuNDU2djExLjA4OEwxMiAxN2wtNi4xNDctNS42NS0zLjQzNCAyLjU4OXpNNy42NDQgMTBMMTIgMTMuMjgzVjYuNzE3eiI+PC9wYXRoPjwvc3ZnPg==)](https://github.com/deathkiller/jazz2-native/actions)
[![Latest Release](https://img.shields.io/github/v/tag/deathkiller/jazz2?label=release)](https://github.com/deathkiller/jazz2/releases/latest)
[![All Downloads](https://img.shields.io/github/downloads/deathkiller/jazz2/total.svg?color=blueviolet)](https://github.com/deathkiller/jazz2/releases)
[![Code Quality](https://img.shields.io/codacy/grade/64eb3ca12bd04c64bf3f3515744b591a.svg?logo=codacy&logoColor=ffffff)](https://www.codacy.com/app/deathkiller/jazz2-native)
[![License](https://img.shields.io/github/license/deathkiller/jazz2-native.svg)](https://github.com/deathkiller/jazz2-native/blob/master/LICENSE)
[![Discord](https://img.shields.io/discord/355651795390955520.svg?color=839ef7&label=chat&logo=discord&logoColor=ffffff&labelColor=586eb5)](https://discord.gg/Y7SBvkD)


## Preview
<div align="center">
    <img src="https://raw.githubusercontent.com/deathkiller/jazz2/master/Docs/Screen2.gif" alt="Preview">
</div>

<div align="center"><a href="https://www.youtube.com/playlist?list=PLfrN-pyVL7k6n2VJF197F0yVOZq4EPTsP">:tv: Watch gameplay videos</a></div>


## Running the application
### Windows
* Install [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)
* Download the game
* Copy contents of original *Jazz Jackrabbit 2* directory to `‹Game›\Source\`
* Run `‹Game›\Jazz2.exe`, `‹Game›\Jazz2_avx2.exe` or `‹Game›\Jazz2_sdl2.exe` application

`‹Game›` *denotes path to Jazz² Resurrection. The game requires **Windows 7** (or newer) and GPU with **OpenGL 3.3** support. Game files should **not** be copied to* `Program Files`*. Cache is recreated during the intro cinematics on the first startup, so it can't be skipped. Also, the sound effects in the intro cinematics require the cache, so they will be missing the first time the game is started up.*

### Linux
* Download the game
* Install dependencies: `sudo apt install libcurl4 libglew2.2 libglfw3 libsdl2-2.0-0 libopenal1 libvorbisfile3 libopenmpt0`
  * Alternatively, install provided `.deb` or `.rpm` package and dependencies should be installed automatically
* Copy contents of original *Jazz Jackrabbit 2* directory to `‹Game›/Source/`
  * If packages are used, the files must be copied to `~/.local/share/Jazz² Resurrection/Source/` or `/usr/local/share/Jazz² Resurrection/Source/` instead, please follow instructions of specific package
* Run `‹Game›/jazz2` or `‹Game›/jazz2_sdl2` application
  * If packages are used, the game should be visible in application lists

`‹Game›` *denotes path to Jazz² Resurrection.* `~` *denotes user's home directory. The game requires GPU with **OpenGL 3.3** or **OpenGL ES 3.0** (ARM) support. Cache is recreated during the intro cinematics on the first startup, so it can't be skipped. Also, the sound effects in the intro cinematics require the cache, so they will be missing the first time the game is started up.*

<sup>Alternatively, you can use package repository for your Linux distribution:</sup><br>
[![ArchLinux](https://img.shields.io/badge/Arch%20Linux-grey?logo=archlinux&logoColor=ffffff)](https://aur.archlinux.org/packages/jazz2-bin)
[![Debian](https://img.shields.io/debian/v/jazz2-native/unstable?label=Debian&logo=debian&logoColor=ffffff)](https://tracker.debian.org/pkg/jazz2-native)
[![Flathub](https://img.shields.io/flathub/v/tk.deat.Jazz2Resurrection?label=Flathub&logo=flathub&logoColor=ffffff)](https://flathub.org/apps/tk.deat.Jazz2Resurrection)
[![Gentoo](https://img.shields.io/badge/Gentoo-grey?logo=gentoo&logoColor=ffffff)](https://packages.gentoo.org/packages/games-arcade/jazz2)
[![NixOS](https://img.shields.io/badge/NixOS-grey?logo=nixos&logoColor=ffffff)](https://search.nixos.org/packages?channel=unstable&show=jazz2&from=0&size=50&sort=relevance&type=packages&query=jazz2)
[![OpenSUSE](https://img.shields.io/obs/games/jazz2/openSUSE_Tumbleweed/x86_64?label=OpenSUSE&logo=opensuse&logoColor=ffffff)](https://build.opensuse.org/package/show/games/jazz2)
[![Ubuntu](https://img.shields.io/ubuntu/v/jazz2-native?label=Ubuntu&logo=ubuntu&logoColor=ffffff)](https://launchpad.net/ubuntu/+source/jazz2-native)
[![XtraDeb](https://img.shields.io/badge/XtraDeb-grey?logo=ubuntu&logoColor=ffffff)](https://xtradeb.net/play/jazz2/)

### macOS
* Download the game and install provided `.dmg` application bundle
* Copy contents of original *Jazz Jackrabbit 2* directory to `~/Library/Application Support/Jazz² Resurrection/Source/`
* Run the newly installed application

`~` *denotes user's home directory. Cache is recreated during the intro cinematics on the first startup, so it can't be skipped. Also, the sound effects in the intro cinematics require the cache, so they will be missing the first time the game is started up.*

Alternatively, you can install it using <sub><sub>[![Homebrew](https://img.shields.io/badge/brew-grey?logo=homebrew&logoColor=ffffff&label=Home&color=b56b2b)](https://formulae.brew.sh/cask/jazz2-resurrection)</sub></sub> `brew trust deathkiller/jazz2` and `brew install deathkiller/jazz2/jazz2`

### Android
* Download the game
* Install `Jazz2.apk` or `Jazz2_x64.apk` on the device
* Copy contents of original *Jazz Jackrabbit 2* directory to `‹Storage›/Android/data/jazz2.resurrection/files/Source/`
  * On **Android 11** or newer, you can *Allow access to external storage* in main menu, then you can use these additional paths:
    * `‹Storage›/Games/Jazz² Resurrection/Source/`
    * `‹Storage›/Download/Jazz² Resurrection/Source/`
* Run the newly installed application

`‹Storage›` *usually denotes internal storage on your device.* `Content` *directory is included directly in APK file, no action is needed. The game requires **Android 5.0** (or newer) and GPU with **OpenGL ES 3.0** support. Cache is recreated during the intro cinematics on the first startup. Also, the sound effects in the intro cinematics require the cache, so they will be missing the first time the game is started up.*

### Nintendo Switch
* Download the game
* Install `Jazz2.nro` package (custom firmware is needed)
* Copy contents of original *Jazz Jackrabbit 2* directory to `/Games/Jazz2/Source/` on SD card
* Run the newly installed application with enabled full RAM access

*Cache is recreated during the intro cinematics on the first startup, so it can't be skipped. It may take more time, so white screen could be shown longer than expected. Also, the sound effects in the intro cinematics require the cache, so they will be missing the first time the game is started up.*

### Sega Dreamcast
* The game plays from a disc, which has to carry the game content converted from an original *Jazz Jackrabbit 2* installation – such disc image can't be distributed, so it has to be built first, as described in [the developer documentation](https://de4th.dev/jazz2/docs/consoles.html#consoles-dreamcast)
* Burn the resulting `jazz2.cdi` image with a tool that understands the Dreamcast's multi-session layout, or serve it from an optical-drive emulator such as GDEMU/MODE
  * The image also boots directly in *Flycast*, *lxdream* or *redream*
* Run the disc

*The game runs in 640×480 and the disc is read-only, so nothing is ever converted or written on the console – the content has to be complete in the image. The PowerVR has no programmable shading, so there is no post-processing tier and no rescale filters. The port has not been tested on real hardware yet.*

### Nintendo Wii
* Download the game
* Copy contents of the provided `sd` directory to the root of an SD card or USB storage device
* Copy contents of original *Jazz Jackrabbit 2* directory to `sd:/apps/Jazz2/Source/` on the same device
  * Alternatively, prepare the content in advance as described in [the developer documentation](https://de4th.dev/jazz2/docs/consoles.html#consoles-wii) to skip the conversion
* Run *Jazz² Resurrection* from the **Homebrew Channel**

*The game reads its content from `sd:/apps/Jazz2/Content/`, so the storage device has to stay present – otherwise the game stops at *Cannot access the SD card*. The Hollywood GPU is fixed-function, so there is no post-processing tier and no rescale filters. The port has not been tested on real hardware yet.*

### Nintendo GameCube
* The console can't convert the game data itself, so the content has to be prepared in advance from an original *Jazz Jackrabbit 2* installation and can't be distributed – see [the developer documentation](https://de4th.dev/jazz2/docs/consoles.html#consoles-gamecube)
* Copy the resulting `sd` directory contents to an SD card in an **SD Gecko** (memory-card slot A) or an **SD2SP2** adapter
* Start `Jazz2/Jazz2.dol` from **Swiss**

*The game reads its content from `carda:/Jazz2/Content/`, which is slot A by definition. The console has 24 MB of memory and no second pool, which makes it the tightest of the two PowerPC targets, and only GameCube controllers are read. Everything noted for the Wii applies here as well, including that the port has not been tested on real hardware yet.*

### PlayStation Portable
* Download the game
* Copy the provided `PSP/GAME/Jazz2` directory to the memory stick at exactly that path (custom firmware is needed)
  * For *PPSSPP*, copy it into the emulator's configured memory stick instead
* Copy contents of original *Jazz Jackrabbit 2* directory to `ms0:/PSP/GAME/Jazz2/Source/`
  * Alternatively, prepare the content in advance as described in [the developer documentation](https://de4th.dev/jazz2/docs/consoles.html#consoles-psp) to skip the conversion
* Run the newly installed application

*The game runs in the native 480×272 and reads its content from `ms0:/PSP/GAME/Jazz2/Content/`. Threads are unavailable on this console, so local splitscreen is not available either. The port has not been tested on real hardware yet.*

### PlayStation 2
* As on the Dreamcast, the game plays from a disc that has to carry the converted game content, so the `jazz2.iso` image can't be distributed either and has to be built first – see [the developer documentation](https://de4th.dev/jazz2/docs/consoles.html#consoles-ps2)
* Burn the resulting image to a disc, or load it in *PCSX2*

*The game runs in NTSC 640×448 and the disc is read-only, so `Jazz2.config` can't be written and settings aren't preserved. There is no audio backend for this console yet, so the game runs silent, threads are unavailable, so local splitscreen is not available either, and the Graphics Synthesizer is fixed-function, so there are no rescale filters. The port has not been tested on real hardware yet.*

### PlayStation 3
* Download the game
* Install `Jazz2.pkg` package (custom firmware is needed)
  * Alternatively, run the unsigned `Jazz2.self` on a CFW console or in *RPCS3*, which requires the PlayStation 3 firmware installed once
* Copy contents of original *Jazz Jackrabbit 2* directory to `Source/` next to `EBOOT.BIN` of the installed package (`/dev_hdd0/game/JAZZ20000/USRDIR/Source/`)
  * Alternatively, prepare the content in advance as described in [the developer documentation](https://de4th.dev/jazz2/docs/consoles.html#consoles-ps3) to skip the conversion
* Run the newly installed application

*Cache and save data are written to `/dev_hdd0/game/JAZZ20000/USRDIR/`. Unlike the older consoles the RSX is a programmable part, so the whole post-processing chain is available – except three of the heavier rescale filters, which are hidden in the options. Threads are unavailable, so local splitscreen is not available either. The port has not been tested on real hardware yet.*

### PlayStation Vita
* Download the game
* Install `Jazz2.vpk` package with **VitaShell** or over FTP (custom firmware is needed)
* Extract `libshacccg.suprx` from the console firmware to `ur0:/data/`, shaders are compiled on the console
* Copy contents of original *Jazz Jackrabbit 2* directory to `ux0:/data/jazz2/Source/`
* Run the newly installed application

*The `Content` directory is included directly in the VPK file, no action is needed. Cache is recreated in `ux0:/data/jazz2/Cache/` during the intro cinematics on the first startup, so it can't be skipped. Also, the sound effects in the intro cinematics require the cache, so they will be missing the first time the game is started up. The port has not been tested on real hardware yet. See [the developer documentation](https://de4th.dev/jazz2/docs/consoles.html#consoles-vita) for details.*

### Web (Emscripten)
* Go to https://de4th.dev/jazz2/wasm/
* Import episodes from original *Jazz Jackrabbit 2* directory in main menu to unlock additional content

*The game requires browser with **WebAssembly** and **WebGL 2.0** support – usually any modern web browser.*

### Libretro core (RetroArch)
* Copy `jazz2_libretro.so` (and `jazz2_libretro.info`, if provided) to the frontend's *cores* directory
* Copy the `Content/` directory next to the game files, then copy contents of original *Jazz Jackrabbit 2* directory to `Source/` beside it – `Cache/` is generated next to them
  * Alternatively, place both in the frontend's *system* directory as `‹System›/jazz2/Content/` and `‹System›/jazz2/Source/`
* Load any file from `Source/` (for example `Anims.j2a`) as content, or start the core without content if `‹System›/jazz2/Content/` exists

*Settings, progress, highscores and the resumable state are written to `‹Saves›/jazz2/` in the frontend's *saves* directory. The core exposes the RetroPad as a gamepad (up to 4 players, local splitscreen included) and renders at 720×405, the native logical resolution of the game, which the frontend scales to the screen – so its video shaders and scaling options replace the in-game *Rescale Mode*. Save states use the game's own level-resume snapshot, so they are not frame-exact – netplay, run-ahead, rewind and reset are not supported. Cache is recreated during the intro cinematics on the first startup, so it can't be skipped.*

### Xbox (Universal Windows Platform)
* Download the game
* Install `Jazz2.cer` certificate if needed (the application is self-signed)
* Install `Jazz2.msixbundle` package
* Run the newly installed application
* Copy contents of original *Jazz Jackrabbit 2* directory to destination shown in the main menu
  * Alternatively, copy the files to `\Games\Jazz² Resurrection\Source\` on an external drive to preserve settings across installations, the application must be set to `Game` type and `NTFS` is the recommended filesystem
  * On `NTFS`, the **ALL APPLICATION PACKAGES** group must be granted full access to that directory, otherwise the application can't read the game files or write its settings there – attach the drive to a Windows PC and run in *Command Prompt* or *PowerShell* as administrator:

    ```batch
    icacls "D:\Games\Jazz² Resurrection" /grant "*S-1-15-2-1:(OI)(CI)(F)" /T
    ```

    where `D:` is the external drive. `*S-1-15-2-1` is the SID of *ALL APPLICATION PACKAGES*, so the command works regardless of system language. Filesystems that have no permissions at all, such as `exFAT`, need no such step
* Run the application again

*Cache is recreated during the intro cinematics on the first startup, so it can't be skipped. It may take more time, so white screen could be shown longer than expected. Also, the sound effects in the intro cinematics require the cache, so they will be missing the first time the game is started up.*


## Building the application

This section contains only a brief explanation of the build process. For a more detailed explanation, including build configuration parameters, please refer to [the developer documentation](https://de4th.dev/jazz2/docs/).

### Windows
* Build dependencies will be downloaded automatically by *CMake*
  * Can be disabled with `NCINE_DOWNLOAD_DEPENDENCIES` option, then download [build dependencies](https://github.com/deathkiller/jazz2-libraries) manually to `.\Libs\`
* Build the project with *CMake*
  * Alternatively, download [build dependencies](https://github.com/deathkiller/jazz2-libraries) to `.\Libs\`, open the solution in [Microsoft Visual Studio 2019](https://www.visualstudio.com/) (or newer) and build it

### Linux
* Build dependencies will be downloaded automatically by *CMake*
  * Can be disabled with `NCINE_DOWNLOAD_DEPENDENCIES` option, then download [build dependencies](https://github.com/deathkiller/jazz2-libraries) manually to `./Libs/`
  * System libraries always have higher priority, there is no need to download them separately if your system already contains all dependencies
  * In case of build errors, install following packages (or equivalent for your distribution):<br>`libgl1-mesa-dev libglew-dev libglfw3-dev libsdl2-dev libopenal-dev libopenmpt-dev libcurl4-openssl-dev zlib1g-dev`
* Build the project with *CMake*

### macOS
* Build dependencies will be downloaded automatically by *CMake*
  * Can be disabled with `NCINE_DOWNLOAD_DEPENDENCIES` option, then download [build dependencies](https://github.com/deathkiller/jazz2-libraries/tree/macos) manually to `./Libs/`
* Build the project with *CMake*

### Android
* Install Android SDK (preferably to `../android-sdk/`)
* Install Android NDK (preferably to `../android-ndk/`)
* Install Gradle (preferably to `../gradle/`)
* Build dependencies will be downloaded automatically by *CMake*
  * Can be disabled with `NCINE_DOWNLOAD_DEPENDENCIES` option, then download [build dependencies](https://github.com/deathkiller/jazz2-libraries/tree/android) manually to `./Libs/`
* Build the project with *CMake* and `NCINE_BUILD_ANDROID` option

### Nintendo Switch
* Install [devkitPro toolchain](https://devkitpro.org/wiki/devkitPro_pacman)
* Build the project with *CMake* and devkitPro toolchain
```bash
cmake -D CMAKE_TOOLCHAIN_FILE=${DEVKITPRO}/cmake/Switch.cmake -D NCINE_PREFERRED_BACKEND=SDL2
```

### Other consoles
The game runs on **Sega Dreamcast**, **Nintendo Wii**, **Nintendo GameCube**, **PlayStation Portable**, **PlayStation 2**, **PlayStation 3** and **PlayStation Vita** as well. Each of them is cross-compiled with its own SDK and *CMake* toolchain file, most of them have a bespoke window and rendering backend for their fixed-function graphics hardware, and the game content has to be prepared in advance with *AssetPacker* and passed to the build with `NCINE_CONTENT_DIR` option.

Please refer to [the console documentation](https://de4th.dev/jazz2/docs/consoles.html) for the toolchain, build, packaging, deployment and logging steps of each console.

### Web (Emscripten)
* Install [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (preferably to `../emsdk/`)
```bash
cd ..
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
```
* Build dependencies will be downloaded automatically by *CMake*
  * Can be disabled with `NCINE_DOWNLOAD_DEPENDENCIES` option
* Copy required game files to `./Content/` directory – the files must be provided in advance
* Build the project with *CMake* and Emscripten toolchain

### Libretro core (RetroArch)
* Build the project with *CMake* and `NCINE_BUILD_LIBRETRO` option, the result is a `jazz2_libretro` shared library instead of an executable, no window backend (*GLFW*, *SDL2* or *Qt5*) is needed
```bash
cmake -D NCINE_BUILD_LIBRETRO=ON
```
* By default the core uses the software renderer, so it runs on any frontend. Add `-D NCINE_PREFERRED_RHI=OpenGL` to render on the GPU instead – such core requires **OpenGL ES 3.0** and a frontend context obtained through `SET_HW_RENDER`

### Xbox (Universal Windows Platform)
* Build dependencies will be downloaded automatically by *CMake*
  * Can be disabled with `NCINE_DOWNLOAD_DEPENDENCIES` option, then download [build dependencies](https://github.com/deathkiller/jazz2-libraries) manually to `.\Libs\`
* Run *CMake* to create [Microsoft Visual Studio 2019](https://www.visualstudio.com/) (or newer) solution
```bash
cmake -D CMAKE_SYSTEM_NAME=WindowsStore -D CMAKE_SYSTEM_VERSION="10.0"
```


## License
This project is licensed under the terms of the [GNU General Public License v3.0](./LICENSE) and uses extensively modified [nCine](https://github.com/nCine/nCine) game engine.