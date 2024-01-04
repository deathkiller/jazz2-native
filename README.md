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
Jazz² Resurrection is reimplementation of the game **Jazz Jackrabbit 2** released in 1998. Supports various versions of the game (Shareware Demo, Holiday Hare '98, The Secret Files and Christmas Chronicles). Also, it partially supports some features of JJ2+ extension and MLLE. This repository contains fully rewritten game in C++ with better performance and many improvements. Further information can be found [here](http://deat.tk/jazz2/).

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
* Download the game
* Copy contents of original *Jazz Jackrabbit 2* directory to `‹Game›\Source\`
* Run `‹Game›\Jazz2.exe`, `‹Game›\Jazz2_avx2.exe` or `‹Game›\Jazz2_sdl2.exe` application

`‹Game›` *is path to Jazz² Resurrection. The game requires **Windows 7** (or newer) and GPU with **OpenGL 3.0** support. Cache is recreated during intro cinematics on the first startup, so it can't be skipped.*

### Linux
* Download the game
* Install dependencies: `sudo apt install libglew2.2 libglfw3 libsdl2-2.0-0 libopenal1 libvorbisfile3 libopenmpt0`
  * Alternatively, install provided `.deb` or `.rpm` package and dependencies should be installed automatically
* Copy contents of original *Jazz Jackrabbit 2* directory to `‹Game›/Source/`
  * If packages are used, the files must be copied to `~/.local/share/Jazz² Resurrection/Source/` instead
* Run `‹Game›/jazz2` or `‹Game›/jazz2_sdl2` application
  * If packages are used, the game should be visible in application list

`‹Game›` *is path to Jazz² Resurrection. The game requires GPU with **OpenGL 3.0** or **OpenGL ES 3.0** (ARM) support. Cache is recreated during intro cinematics on the first startup, so it can't be skipped.*

<sup>Alternatively, you can use package repository for your Linux distribution:</sup><br>
[![ArchLinux](https://img.shields.io/badge/Arch%20Linux-grey?logo=archlinux&logoColor=ffffff)](https://aur.archlinux.org/packages/jazz2-bin)
[![Flathub](https://img.shields.io/flathub/v/tk.deat.Jazz2Resurrection?label=Flathub&logo=flathub&logoColor=ffffff)](https://flathub.org/apps/tk.deat.Jazz2Resurrection)
[![NixOS](https://img.shields.io/badge/NixOS-grey?logo=nixos&logoColor=ffffff)](https://search.nixos.org/packages?channel=unstable&show=jazz2&from=0&size=50&sort=relevance&type=packages&query=jazz2)
[![OpenSUSE](https://img.shields.io/obs/games/jazz2/openSUSE_Tumbleweed/x86_64?label=OpenSUSE&logo=opensuse&logoColor=ffffff)](https://build.opensuse.org/package/show/games/jazz2)
[![Ubuntu](https://img.shields.io/badge/Ubuntu-grey?logo=ubuntu&logoColor=ffffff)](https://xtradeb.net/play/jazz2/)

### macOS
* Download the game and install provided `.dmg` application bundle
* Copy contents of original *Jazz Jackrabbit 2* directory to `~/Library/Application Support/Jazz² Resurrection/Source/`
* Run the newly installed application

*Cache is recreated during intro cinematics on the first startup, so it can't be skipped.*

Alternatively, you can install it using <sub><sub>[![Homebrew](https://img.shields.io/homebrew/cask/v/jazz2-resurrection?logo=homebrew&logoColor=ffffff&label=Homebrew&color=b56b2b)](https://formulae.brew.sh/cask/jazz2-resurrection)</sub></sub> `brew install --cask jazz2-resurrection`

### Android
* Download the game
* Install `Jazz2.apk` or `Jazz2_x64.apk` on the device
* Copy contents of original *Jazz Jackrabbit 2* directory to `‹Storage›/Android/data/jazz2.resurrection/files/Source/`
  * On **Android 11** or newer, you can *Allow access to external storage* in main menu, then you can use these additional paths:
    * `‹Storage›/Games/Jazz² Resurrection/Source/`
    * `‹Storage›/Download/Jazz² Resurrection/Source/`
* Run the newly installed application

`‹Storage›` *is usually internal storage on your device.* `Content` *directory is included directly in APK file, no action is needed. The game requires **Android 5.0** (or newer) and GPU with **OpenGL ES 3.0** support. Cache is recreated during intro cinematics on the first startup.*

### Nintendo Switch
* Download the game
* Install `Jazz2.nro` package (custom firmware is needed)
* Copy contents of original *Jazz Jackrabbit 2* directory to `/Games/Jazz2/Source/` on SD card
* Run the newly installed application with enabled full RAM access

*Cache is recreated during intro cinematics on the first startup, so it can't be skipped. It may take more time, so white screen could be shown longer than expected.*

### Web (Emscripten)
* Go to http://deat.tk/jazz2/wasm/
* Import episodes from original *Jazz Jackrabbit 2* directory in main menu to unlock additional content

*The game requires browser with **WebAssembly** and **WebGL 2.0** support – usually any modern web browser.*

### Xbox (Universal Windows Platform)
* Download the game
* Install `Jazz2.cer` certificate if needed (the application is self-signed)
* Install `Jazz2.msixbundle` package
* Run the newly installed application
* Copy contents of original *Jazz Jackrabbit 2* directory to destination shown in the main menu
  * Alternatively, copy the files to `\Games\Jazz² Resurrection\Source\` on an external drive to preserve settings across installations, the application must be set to `Game` type, `exFAT` is recommended or correct read/write permissions must be assigned
* Run the application again


## Building the application
### Windows
* Build dependencies will be downloaded automatically by *CMake*
  * Can be disabled with `NCINE_DOWNLOAD_DEPENDENCIES` option, then download [build dependencies](https://github.com/deathkiller/jazz2-libraries) manually to `.\Libs\`
* Build the project with *CMake*
  * Alternatively, download [build dependencies](https://github.com/deathkiller/jazz2-libraries) to `.\Libs\`, open the solution in [Microsoft Visual Studio 2019](https://www.visualstudio.com/) (or newer) and build it

### Linux
* Build dependencies will be downloaded automatically by *CMake*
  * Can be disabled with `NCINE_DOWNLOAD_DEPENDENCIES` option, then download [build dependencies](https://github.com/deathkiller/jazz2-libraries) manually to `./Libs/`
  * System libraries always have higher priority, there is no need to download them separately if your system already contains all dependencies
  * In case of build errors, install following packages (or equivalent for your distribution):<br>`libgl1-mesa-dev libglew-dev libglfw3-dev libsdl2-dev libopenal-dev libopenmpt-dev zlib1g-dev`
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
  * Can be disabled with `NCINE_DOWNLOAD_DEPENDENCIES` option, then download [build dependencies](https://github.com/deathkiller/jazz2-libraries) manually to `./Libs/`
* Build the project with *CMake* and `NCINE_BUILD_ANDROID` option

### Nintendo Switch
* Install [devkitPro toolchain](https://devkitpro.org/wiki/devkitPro_pacman)
* Build the project with *CMake* and devkitPro toolchain
```bash
cmake -D CMAKE_TOOLCHAIN_FILE=${DEVKITPRO}/cmake/Switch.cmake -D NCINE_PREFERRED_BACKEND=SDL2
```

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

### Xbox (Universal Windows Platform)
* Build dependencies will be downloaded automatically by *CMake*
  * Can be disabled with `NCINE_DOWNLOAD_DEPENDENCIES` option, then download [build dependencies](https://github.com/deathkiller/jazz2-libraries) manually to `.\Libs\`
* Run *CMake* to create [Microsoft Visual Studio 2019](https://www.visualstudio.com/) (or newer) solution
```bash
cmake -D CMAKE_SYSTEM_NAME=WindowsStore -D CMAKE_SYSTEM_VERSION="10.0"
```


## License
This project is licensed under the terms of the [GNU General Public License v3.0](./LICENSE).

This project uses modified [nCine](https://github.com/nCine/nCine) game engine released under the following license:
```
Copyright (c) 2011-2022 Angelo Theodorou

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```