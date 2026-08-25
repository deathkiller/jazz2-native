# CMake toolchain file for classic Amiga (AmigaOS 3.x, m68k)
#
# Uses Bebbo's amiga-gcc (https://franke.ms/git/bebbo/amiga-gcc, branch amiga13.4 - GCC 13 with full
# C++17; the default gcc-6.5 branch cannot compile this codebase). The C runtime is libnix
# (-mcrt=nix20, Kickstart 2.0+), which is the leanest of the toolchain's runtimes and enough for a
# game that ships its own content tree. Like PSL1GHT and libdragon this SDK has no CMake toolchain
# file of its own, so this one lives with the project.
#
# Set AMIGA_INST in the environment to the amiga-gcc install prefix (the directory holding
# bin/m68k-amigaos-gcc). zlib must be cross-compiled into "$AMIGA_INST/m68k-amigaos" once, exactly
# like the N64 toolchain's zlib (see Docs/Amiga.dox for the one-time commands).
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
	message(FATAL_ERROR "amiga-gcc not found at \"${AMIGA_INST}\" (build https://franke.ms/git/bebbo/amiga-gcc with the gcc module on branch amiga13.4)")
endif()
if(NOT EXISTS "${AMIGA_INST}/m68k-amigaos/ndk-include/exec/exec.h" AND NOT EXISTS "${AMIGA_INST}/m68k-amigaos/ndk/include/exec/exec.h")
	message(FATAL_ERROR "amiga-gcc at \"${AMIGA_INST}\" has no NDK headers (run its \"make ndk\" target)")
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
# -fno-optimize-sibling-calls is CORRECTNESS, not tuning: bebbo-ld resolves a sibcall (bra.l/60FF)
# to a WEAK symbol against the wrong address (verified with a minimal two-line reproducer - strong
# targets and ordinary jsr calls to the same weak symbols resolve fine), and a C++ codebase is full
# of tail calls into inline/template functions. Costs only the tail-call optimization.
set(_amigaMachDep "-mcpu=68060 -mcrt=nix20 -fomit-frame-pointer -fno-exceptions -fno-optimize-sibling-calls")

set(CMAKE_C_FLAGS_INIT "${_amigaMachDep}")
# The force-included header restores the C99 maths set libstdc++ was configured without - the same
# arrangement (and file layout) as the PS3 toolchain, see amiga-libstdc++-c99.h
# The force-included header restores the C99 maths set libstdc++ was configured without - the
# same arrangement (and file layout) as the PS3 toolchain, see amiga-libstdc++-c99.h
set(CMAKE_CXX_FLAGS_INIT "${_amigaMachDep} -include ${CMAKE_CURRENT_LIST_DIR}/amiga-libstdc++-c99.h")
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
