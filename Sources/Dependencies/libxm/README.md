This directory vendors libxm, by Romain "Artefact2" Dalmaso, in the form libdragon keeps it in `src/audio/libxm`
(libdragon commit `55aac1355773169d87e45424b772faf0dfb9432a`) - which is what libdragon's `audioconv64` builds into
itself to write `.xm64` modules. AssetPacker does the same (see `Sources/Utilities/AssetPacker/Xm64Converter.cpp`):
the module is loaded into libxm's context, played through once to size the buffers the console needs, and the
context is then saved in the layout libdragon's player reads.

Licensed under the WTFPL (see the header of each file). Changes: `XM_DEBUG` can be overridden from the command line
and defaults to off, and the GCC-only bits that a host tool does not need are behind portable fallbacks (marked with
"AssetPacker:").
