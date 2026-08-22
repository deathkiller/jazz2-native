# CMake toolchain file for the Nintendo 64 (libdragon)
#
# Like PSL1GHT, libdragon is a Makefile-based SDK ("$N64_INST/include/n64.mk") that ships no CMake
# toolchain file of its own, so this one lives with the project and reproduces what n64.mk sets up:
# the mips64-elf cross compiler, the VR4300 machine flags libdragon builds its own libraries with,
# and the newlib override headers. The libdragon libraries themselves are linked by the platform arm
# in ncine_extra_sources.cmake, and the ELF -> .z64 ROM conversion is a POST_BUILD step there too.
#
# Set N64_INST in the environment to the libdragon toolchain prefix. The OpenGL headers require
# a libdragon built from the "preview" branch.

if(DEFINED ENV{N64_INST})
	set(N64_INST $ENV{N64_INST})
else()
	message(FATAL_ERROR "The environment variable N64_INST needs to be defined.")
endif()

if(NOT EXISTS "${N64_INST}/mips64-elf/include/libdragon.h")
	message(FATAL_ERROR "libdragon not found at \"${N64_INST}\" (install the toolchain and run `make install` in libdragon)")
endif()

set(ENV{N64_INST} "${N64_INST}")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR mips)

set(CMAKE_C_COMPILER "${N64_INST}/bin/mips64-elf-gcc")
set(CMAKE_CXX_COMPILER "${N64_INST}/bin/mips64-elf-g++")
set(CMAKE_ASM_COMPILER "${N64_INST}/bin/mips64-elf-gcc")
set(CMAKE_AR "${N64_INST}/bin/mips64-elf-gcc-ar" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "${N64_INST}/bin/mips64-elf-gcc-ranlib" CACHE FILEPATH "Ranlib")
set(CMAKE_STRIP "${N64_INST}/bin/mips64-elf-strip" CACHE FILEPATH "Strip")
set(CMAKE_OBJCOPY "${N64_INST}/bin/mips64-elf-objcopy" CACHE FILEPATH "Objcopy")

# There is no N64 host to run a link test on, so CMake's compiler probe has to stop at the object file
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# The machine flags libdragon's n64.mk builds its own libraries with. -falign-functions=32 is load-bearing:
# libdragon's backtrace() assumes it. ktls.h and the newlib_overrides directory are libdragon's kernel-TLS
# and newlib patch layer and must come before the regular include path. -ftrapping-math/-fno-associative-math
# temper -ffast-math the same way the SDK does. LIBDRAGON_PREVIEW=2 unlocks the preview APIs (OpenGL).
set(_n64MachDep "-march=vr4300 -mtune=vr4300 -mabi=o64 -falign-functions=32 -ffunction-sections -fdata-sections -ffast-math -ftrapping-math -fno-associative-math -I${N64_INST}/mips64-elf/include/newlib_overrides -include ktls.h -DN64 -DLIBDRAGON_PREVIEW=2")

set(CMAKE_C_FLAGS_INIT "${_n64MachDep}")
set(CMAKE_CXX_FLAGS_INIT "${_n64MachDep}")
# n64.ld is found through -L; --wrap __do_global_ctors is how libdragon sequences C++ static constructors
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L${N64_INST}/mips64-elf/lib -Wl,-Tn64.ld -Wl,--gc-sections -Wl,--wrap,__do_global_ctors")

set(CMAKE_FIND_ROOT_PATH "${N64_INST}" "${N64_INST}/mips64-elf")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_SYSTEM_PREFIX_PATH ${CMAKE_FIND_ROOT_PATH})

include_directories("${N64_INST}/mips64-elf/include")
link_directories("${N64_INST}/mips64-elf/lib")

set(PLATFORM_N64 TRUE)
set(N64 TRUE)

# Host tools the packaging step in ncine_extra_sources.cmake invokes
set(N64_TOOL "${N64_INST}/bin/n64tool" CACHE FILEPATH "Path to n64tool (ELF -> .z64 ROM)")
set(N64_SYM "${N64_INST}/bin/n64sym" CACHE FILEPATH "Path to n64sym (symbol table for on-console backtraces)")
set(N64_ELFCOMPRESS "${N64_INST}/bin/n64elfcompress" CACHE FILEPATH "Path to n64elfcompress")
set(N64_MKDFS "${N64_INST}/bin/mkdfs" CACHE FILEPATH "Path to mkdfs (DragonFS image writer)")
set(N64_ED64ROMCONFIG "${N64_INST}/bin/ed64romconfig" CACHE FILEPATH "Path to ed64romconfig (save type in ROM header)")
