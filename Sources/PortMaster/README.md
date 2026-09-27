## Installation
[Jazz² Resurrection](https://github.com/deathkiller/jazz2-native) needs the files of the original *Jazz Jackrabbit 2* - copy the contents of its game directory to `ports/jazz2/Source/`. Game cache is created from them on the first launch, which can take a while. Settings and saves are stored in `ports/jazz2/` too.

Supported releases are Shareware Demo, v1.20/1.23, Holiday Hare '98, The Secret Files and The Christmas Chronicles. To combine more of them, copy them over each other in this order, replacing existing files.

## Controls
- **Movement** - D-Pad or Left Stick
- **Jump** - A
- **Run** - B or L2
- **Shoot** - X or R2
- **Switch Weapon** - Y
- **Menu** - Start
- **Exit** - Start + Select

All buttons can be remapped in *Options > Controls*.

## Troubleshooting
If textures are missing or flickering, try launching the game with the `/gpu-workaround fixed-batch-size` option and please [report](https://github.com/deathkiller/jazz2-native/issues) the device.

## Building
The port is built automatically by [this workflow](https://github.com/deathkiller/jazz2-native/blob/master/.github/workflows/portmaster.yml) in an Ubuntu 20.04 container, linked against SDL 2.26.2:
```shell
cmake -B ./_build/ -D CMAKE_BUILD_TYPE=Release -D NCINE_PREFERRED_BACKEND=SDL2 -D NCINE_PREFERRED_RHI=OpenGL -D NCINE_RHI_GL_PROFILE=ES3 -D NCINE_WITH_GLEW=OFF -D NCINE_COMPILE_OPENMPT=ON -D NCINE_LINUX_PACKAGE=jazz2 -D NCINE_PACKAGED_CONTENT_PATH=ON -D "CMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc"
make -j $(nproc) -C ./_build/
```
