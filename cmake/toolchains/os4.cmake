# CMake toolchain file for AmigaOS 4.1 (PowerPC)
#
# Uses the adtools cross-compiler (ppc-amigaos-gcc 11.5, full C++17) together with the AmigaOS 4.1
# SDK. The easiest way to get both is the maintained container image
# docker.io/walkero/amigagccondocker:os4-gcc11 (see Docs/Amiga.dox).
#
# Unlike the classic Amiga port (cmake/toolchains/amiga.cmake) this target needs no backend of its
# own: AmigaOS 4 has a mature SDL2, so the window and input side is the SDL2 backend the desktop
# builds use, and the renderer is the CPU software rasterizer, which on a PC-class CPU is fast enough
# to be the sensible first answer rather than a compromise. A hardware path is not ruled out - the
# Radeon in an X5000 is reachable through Warp3D Nova's ogles2.library, and the engine does have an
# ES2 profile (NCINE_RHI_GL_PROFILE=ES2, the one the PS Vita's vitaGL build uses) that matches it -
# but the SDK this toolchain builds against ships no ogles2 headers or link library, and there is no
# AmigaOS 4 machine here to test such a build on, so it stays a separate project.
#
# Set OS4_INST in the environment to the adtools prefix (the directory holding bin/ppc-amigaos-gcc)
# if it is not the container's /opt/ppc-amigaos.

if(DEFINED ENV{OS4_INST})
	set(OS4_INST $ENV{OS4_INST})
else()
	set(OS4_INST "/opt/ppc-amigaos")
endif()

if(NOT EXISTS "${OS4_INST}/bin/ppc-amigaos-gcc")
	message(FATAL_ERROR "adtools not found at \"${OS4_INST}\" (set OS4_INST, or build inside the toolchain container - see Docs/Amiga.dox)")
endif()

set(_os4Sdk "${OS4_INST}/ppc-amigaos/SDK")
if(NOT EXISTS "${_os4Sdk}/local/newlib/lib/libSDL2.a")
	message(FATAL_ERROR "The AmigaOS 4.1 SDK at \"${_os4Sdk}\" has no SDL2 (this port needs the SDK's newlib SDL2)")
endif()

set(ENV{OS4_INST} "${OS4_INST}")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR powerpc)

set(CMAKE_C_COMPILER "${OS4_INST}/bin/ppc-amigaos-gcc")
set(CMAKE_CXX_COMPILER "${OS4_INST}/bin/ppc-amigaos-g++")
set(CMAKE_ASM_COMPILER "${OS4_INST}/bin/ppc-amigaos-gcc")
set(CMAKE_AR "${OS4_INST}/bin/ppc-amigaos-ar" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "${OS4_INST}/bin/ppc-amigaos-ranlib" CACHE FILEPATH "Ranlib")
set(CMAKE_STRIP "${OS4_INST}/bin/ppc-amigaos-strip" CACHE FILEPATH "Strip")
set(CMAKE_OBJCOPY "${OS4_INST}/bin/ppc-amigaos-objcopy" CACHE FILEPATH "Objcopy")

# There is no AmigaOS 4 host to run a link test on, so CMake's compiler probe has to stop at the object file
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# -mcrt=newlib: the SDK's prebuilt SDL2, zlib and Vorbis are the newlib flavour, and mixing runtimes
# does not link. -athread=native gives the compiler its gthreads implementation, which is what makes
# std::thread work - unlike the classic Amiga, this target keeps threads. The engine also calls the
# pthread API directly, and that comes from the SDK's own libpthread rather than from the toolchain:
# the -athread=pthread flavour the specs offer cannot be used, because the gthr-amigaos-pthread.o it
# links against is not shipped in this toolchain at all.
#
# -Uamiga removes a legacy lowercase predefine (the compiler defines both `amiga` and `AMIGA`, the way
# ancient compilers defined `unix`). `AMIGA` is what the OS headers test and is left alone; the
# lowercase one is used by nothing and breaks any C++ that has an identifier of that name - libopenmpt
# has `common_encoding::amiga`, and it is a third-party source this project does not patch.
set(_os4MachDep "-mcrt=newlib -athread=native -Uamiga")

set(CMAKE_C_FLAGS_INIT "${_os4MachDep}")
set(CMAKE_CXX_FLAGS_INIT "${_os4MachDep}")
# The SDK's own libraries (SDL2, libpthread, zlib, ...) live outside the compiler's default search path
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_os4MachDep} -L${_os4Sdk}/local/newlib/lib")

set(CMAKE_FIND_ROOT_PATH "${_os4Sdk}/local/newlib" "${_os4Sdk}/local/common" "${_os4Sdk}/newlib" "${OS4_INST}/ppc-amigaos")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_SYSTEM_PREFIX_PATH ${CMAKE_FIND_ROOT_PATH})

set(PLATFORM_AMIGAOS4 TRUE)
set(AMIGA TRUE)
