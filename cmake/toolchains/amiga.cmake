# CMake toolchain file for classic Amiga (AmigaOS 3.x, m68k)
#
# Uses AmigaPorts' m68k-amigaos-gcc (https://github.com/AmigaPorts/m68k-amigaos-gcc), which is
# published as a prebuilt release for Linux x86_64 and macOS arm64, so nothing here has to build a
# cross-compiler. GCC 16.2 there; GCC 13 is the floor this codebase needs, being C++17 throughout.
# The release also carries the third-party developer kits this port compiles against - CyberGraphX
# and AHI headers - plus the vasm the AMMX kernels are assembled with.
#
# The C runtime is libnix (-mcrt=nix20, Kickstart 2.0+), which is the leanest of the toolchain's
# runtimes and enough for a game that ships its own content tree. Like PSL1GHT and libdragon this
# SDK has no CMake toolchain file of its own, so this one lives with the project.
#
# Set AMIGA_INST in the environment to the toolchain prefix (the directory holding
# bin/m68k-amigaos-gcc - for a release archive that is the unpacked "m68k-amigaos-gcc-<version>"
# directory itself, which is relocatable). zlib must be cross-compiled into
# "$AMIGA_INST/m68k-amigaos" once, exactly like the N64 toolchain's zlib (see Docs/Amiga.dox for
# the one-time commands).
#
# The binary targets a 68040/68060 with FPU ("-mcpu=68060", which implies the FPU): GCC's 68060 code generation
# avoids the instructions the 060 traps on, the 68080 (Vampire) and Emu68 (PiStorm) execute the
# same code natively, and a plain 68040 runs it through the 68040 support libraries. Machines
# without an FPU are below this port's floor (see Docs/AmigaPortDesign.md).

if(DEFINED ENV{AMIGA_INST})
	set(AMIGA_INST $ENV{AMIGA_INST})
else()
	message(FATAL_ERROR "The environment variable AMIGA_INST needs to be defined.")
endif()

if(NOT EXISTS "${AMIGA_INST}/bin/m68k-amigaos-gcc")
	message(FATAL_ERROR "m68k-amigaos-gcc not found at \"${AMIGA_INST}\" (unpack a release from https://github.com/AmigaPorts/m68k-amigaos-gcc/releases)")
endif()
if(NOT EXISTS "${AMIGA_INST}/m68k-amigaos/ndk-include/exec/exec.h" AND NOT EXISTS "${AMIGA_INST}/m68k-amigaos/ndk/include/exec/exec.h")
	message(FATAL_ERROR "m68k-amigaos-gcc at \"${AMIGA_INST}\" has no NDK headers - the release archive was not unpacked whole")
endif()

set(ENV{AMIGA_INST} "${AMIGA_INST}")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR m68k)

set(CMAKE_C_COMPILER "${AMIGA_INST}/bin/m68k-amigaos-gcc")
set(CMAKE_CXX_COMPILER "${AMIGA_INST}/bin/m68k-amigaos-g++")
set(CMAKE_ASM_COMPILER "${AMIGA_INST}/bin/m68k-amigaos-gcc")
set(CMAKE_AR "${AMIGA_INST}/bin/m68k-amigaos-ar" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "${AMIGA_INST}/bin/m68k-amigaos-ranlib" CACHE FILEPATH "Ranlib")
set(CMAKE_STRIP "${AMIGA_INST}/bin/m68k-amigaos-strip" CACHE FILEPATH "Strip")
set(CMAKE_OBJCOPY "${AMIGA_INST}/bin/m68k-amigaos-objcopy" CACHE FILEPATH "Objcopy")

# There is no Amiga host to run a link test on, so CMake's compiler probe has to stop at the object file
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# -mcpu=68060: see the header comment; the 060 implies its FPU, and an EXPLICIT -m68881 must not be
# added - it knocks GCC's multilib selection off "libm060" back to the plain-68000 libraries.
# -fomit-frame-pointer matters on a register-starved 68k. -fno-exceptions is the project's
# convention on the console tier and libnix has no unwinder anyway (RTTI stays on - the game has a
# dynamic_cast or two, like every other console build). Large data/code model is the compiler's
# default (no -fbaserel: the binary's data far exceeds a 64 KB near section).
# -fno-optimize-sibling-calls is CORRECTNESS, not tuning: the m68k-amigaos ld resolves a sibcall
# (bra.l/60FF) to a WEAK symbol against the wrong address (verified with a minimal two-line
# reproducer - strong targets and ordinary jsr calls to the same weak symbols resolve fine), and a
# C++ codebase is full of tail calls into inline/template functions. Costs only the tail-call
# optimization.
set(_amigaMachDep "-mcpu=68060 -mcrt=nix20 -fomit-frame-pointer -fno-exceptions -fno-optimize-sibling-calls")

set(CMAKE_C_FLAGS_INIT "${_amigaMachDep}")
# The force-included header restores the C99 maths set libstdc++ was configured without - the same
# arrangement (and file layout) as the PS3 toolchain, see amiga-libstdc++-c99.h
# -std=gnu++17 is pinned here rather than left to CMake: GCC 16 defaults to gnu++20, and
# `target_compile_features(cxx_std_17)` is a MINIMUM, so CMake adds no flag at all and the whole
# codebase silently compiles as C++20. That is not merely inconsistent with every other target -
# the toolchain's <math.h> pulls `using std::lerp;` into the global namespace under C++20, which
# collides with the engine's own nCine::lerp on every call.
set(CMAKE_CXX_FLAGS_INIT "${_amigaMachDep} -std=gnu++17 -include ${CMAKE_CURRENT_LIST_DIR}/amiga-libstdc++-c99.h")
# -mcrt selects the matching startup/libs at link time as well
set(CMAKE_EXE_LINKER_FLAGS_INIT "-mcrt=nix20")

set(CMAKE_FIND_ROOT_PATH "${AMIGA_INST}" "${AMIGA_INST}/m68k-amigaos")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_SYSTEM_PREFIX_PATH ${CMAKE_FIND_ROOT_PATH})

set(PLATFORM_AMIGA TRUE)
set(AMIGA TRUE)
