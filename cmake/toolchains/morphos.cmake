# CMake toolchain file for MorphOS (PowerPC)
#
# Uses the MorphOS SDK's own GCC (ppc-morphos-gcc 11.3, full C++17). The easiest way to get it is the
# maintained container image docker.io/walkero/amigagccondocker:mos-gcc-amd64 (see Docs/Amiga.dox).
#
# Like the AmigaOS 4 target (cmake/toolchains/os4.cmake) this needs no window backend of its own -
# MorphOS has a mature SDL2. The renderer is the CPU software rasterizer by default; MorphOS's 3D
# interface is TinyGL, which has no shaders at all, so the alternative is not the engine's OpenGL
# backend but the fixed-function LegacyGL one (-DNCINE_PREFERRED_RHI=LegacyGL, see Docs/Amiga.dox).
#
# Set MORPHOS_SDK in the environment if the SDK is not the container's /gg, and MORPHOS_DEPS to a
# prefix holding cross-compiled zlib - the SDK does not ship one, and the game cannot run without it.
# Docs/Amiga.dox carries the one command that cross-compiles it.

if(DEFINED ENV{MORPHOS_SDK})
	set(MORPHOS_SDK $ENV{MORPHOS_SDK})
else()
	set(MORPHOS_SDK "/gg")
endif()
if(DEFINED ENV{MORPHOS_DEPS})
	set(MORPHOS_DEPS $ENV{MORPHOS_DEPS})
else()
	set(MORPHOS_DEPS "${MORPHOS_SDK}")
endif()

if(NOT EXISTS "${MORPHOS_SDK}/lib/libSDL2.a")
	message(FATAL_ERROR "The MorphOS SDK at \"${MORPHOS_SDK}\" has no SDL2 (set MORPHOS_SDK, or build inside the toolchain container - see Docs/Amiga.dox)")
endif()
if(NOT EXISTS "${MORPHOS_DEPS}/lib/libz.a")
	message(FATAL_ERROR "No cross-compiled zlib in \"${MORPHOS_DEPS}\" - the MorphOS SDK ships none, cross-compile it first (see Docs/Amiga.dox)")
endif()

set(ENV{MORPHOS_SDK} "${MORPHOS_SDK}")
set(ENV{MORPHOS_DEPS} "${MORPHOS_DEPS}")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR powerpc)

set(CMAKE_C_COMPILER "ppc-morphos-gcc")
set(CMAKE_CXX_COMPILER "ppc-morphos-g++")
set(CMAKE_ASM_COMPILER "ppc-morphos-gcc")
set(CMAKE_AR "ppc-morphos-ar" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "ppc-morphos-ranlib" CACHE FILEPATH "Ranlib")
set(CMAKE_STRIP "ppc-morphos-strip" CACHE FILEPATH "Strip")
set(CMAKE_OBJCOPY "ppc-morphos-objcopy" CACHE FILEPATH "Objcopy")

# There is no MorphOS host to run a link test on, so CMake's compiler probe has to stop at the object file
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# -noixemul selects MorphOS's own C library rather than the ixemul.library emulation, which is what
# everything in the SDK (SDL2 included) is built against.
#
# __STDC_LIMIT_MACROS/__STDC_CONSTANT_MACROS: this SDK's <stdint.h> still hides INT32_MIN and friends
# behind them in C++, the pre-C++11 rule, so a C++17 codebase that uses those macros needs them asked
# for explicitly.
#
# _NO_PPCINLINE keeps the SDK's ppcinline headers out: they define OS calls as function-like macros,
# and `bind` among them rewrites std::bind inside libstdc++'s own <functional> as soon as anything
# includes <unistd.h> (zlib.h does). The prototypes in clib/*_protos.h are still included, so nothing
# that genuinely calls the OS breaks - and this port calls it through SDL2 anyway. See
# morphos-compat.h, force-included below, for what that leaves to declare by hand.

#
# PROTO_EXEC_H and PROTO_USERGROUP_H are defined so <unistd.h> skips those two proto headers: between
# them they put `struct Task`, `Signal` and friends in the global namespace, and `Task` is also the name
# of the engine's coroutine type - the two together make every unqualified `Task` ambiguous, in the SDK's
# own headers as much as in the engine's. Nothing in this port calls exec or usergroup directly.
set(_morphosMachDep "-noixemul -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -D_NO_PPCINLINE -DPROTO_EXEC_H -DPROTO_USERGROUP_H")

set(CMAKE_C_FLAGS_INIT "${_morphosMachDep} -include ${CMAKE_CURRENT_LIST_DIR}/morphos-compat.h")
set(CMAKE_CXX_FLAGS_INIT "${_morphosMachDep} -include ${CMAKE_CURRENT_LIST_DIR}/morphos-compat.h")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_morphosMachDep} -L${MORPHOS_DEPS}/lib")

set(CMAKE_FIND_ROOT_PATH "${MORPHOS_DEPS}" "${MORPHOS_SDK}" "${MORPHOS_SDK}/ppc-morphos")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_SYSTEM_PREFIX_PATH ${CMAKE_FIND_ROOT_PATH})

set(PLATFORM_MORPHOS TRUE)
set(AMIGA TRUE)
