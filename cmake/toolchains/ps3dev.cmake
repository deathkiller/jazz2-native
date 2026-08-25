# CMake toolchain file for the PlayStation 3 (ps3toolchain + PSL1GHT)
#
# Unlike pspdev, ps2dev and VitaSDK - each of which ships a toolchain file inside the SDK that the build
# scripts point CMAKE_TOOLCHAIN_FILE at - PSL1GHT is a purely Makefile-based SDK ("$PSL1GHT/ppu_rules"), so
# there is no upstream file to use and this one lives with the project instead. It reproduces what those
# rules set up: the powerpc64-ps3-elf cross compiler, the PSL1GHT include/library paths, and the machine
# flags the SDK builds its own libraries with.
#
# Set PS3DEV in the environment; PSL1GHT defaults to "$PS3DEV/psl1ght", which is where
# ps3toolchain's step 8 installs it.

if(DEFINED ENV{PS3DEV})
	set(PS3DEV $ENV{PS3DEV})
else()
	message(FATAL_ERROR "The environment variable PS3DEV needs to be defined.")
endif()

if(DEFINED ENV{PSL1GHT})
	set(PSL1GHT $ENV{PSL1GHT})
else()
	set(PSL1GHT "${PS3DEV}/psl1ght")
endif()

if(NOT EXISTS "${PSL1GHT}/ppu/include/ppu-lv2.h")
	message(FATAL_ERROR "PSL1GHT not found at \"${PSL1GHT}\" (build it with ps3toolchain step 8)")
endif()

# The ppu-gcc driver reads $PSL1GHT itself and refuses to run without it ("fatal error: environment
# variable 'PSL1GHT' not defined"), so it has to be in the environment of every compile - not merely a
# CMake variable. Setting it here covers the configure step and CMake's own compiler probes; it has to be
# exported for the build itself as well, since the build commands inherit the environment of `cmake --build`.
set(ENV{PS3DEV} "${PS3DEV}")
set(ENV{PSL1GHT} "${PSL1GHT}")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR powerpc64)

set(CMAKE_C_COMPILER "${PS3DEV}/ppu/bin/ppu-gcc")
set(CMAKE_CXX_COMPILER "${PS3DEV}/ppu/bin/ppu-g++")
set(CMAKE_ASM_COMPILER "${PS3DEV}/ppu/bin/ppu-gcc")
set(CMAKE_AR "${PS3DEV}/ppu/bin/ppu-ar" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "${PS3DEV}/ppu/bin/ppu-ranlib" CACHE FILEPATH "Ranlib")
set(CMAKE_STRIP "${PS3DEV}/ppu/bin/ppu-strip" CACHE FILEPATH "Strip")
set(CMAKE_OBJCOPY "${PS3DEV}/ppu/bin/ppu-objcopy" CACHE FILEPATH "Objcopy")

# There is no PS3 host to run a link test on, so CMake's compiler probe has to stop at the object file
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# The machine flags PSL1GHT's `ppu_rules` builds its own libraries with (MACHDEP). The Cell PPE has a
# hardware FPU, and -fmodulo-sched pays off on an in-order core; the section flags let --gc-sections drop
# what the game never calls, which matters because the whole ELF is loaded into memory at once.
set(_ps3MachDep "-mcpu=cell -mhard-float -fmodulo-sched -ffunction-sections -fdata-sections")

# This toolchain's libstdc++ declares none of the C99 maths functions in `std::` (see the header for why
# the obvious macro is not the answer), which breaks any C++ that calls std::round or std::log2 - including
# third-party sources this project only fetches, like libopenmpt. Force-including the shim covers every
# translation unit rather than only the ones that happen to include a project header.
set(_ps3C99Shim "${CMAKE_CURRENT_LIST_DIR}/ps3-libstdc++-c99.h")

set(CMAKE_C_FLAGS_INIT "${_ps3MachDep} -I${PSL1GHT}/ppu/include -I${PSL1GHT}/ppu/include/simdmath")
set(CMAKE_CXX_FLAGS_INIT "${_ps3MachDep} -I${PSL1GHT}/ppu/include -I${PSL1GHT}/ppu/include/simdmath -include ${_ps3C99Shim}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L${PSL1GHT}/ppu/lib -Wl,--gc-sections")

set(CMAKE_FIND_ROOT_PATH "${PS3DEV}" "${PS3DEV}/ppu" "${PSL1GHT}/ppu" "${PS3DEV}/portlibs/ppu")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_SYSTEM_PREFIX_PATH ${CMAKE_FIND_ROOT_PATH})

include_directories("${PSL1GHT}/ppu/include" "${PSL1GHT}/ppu/include/simdmath")
link_directories("${PSL1GHT}/ppu/lib")

# The PSL1GHT headers and the SDK's own sources key on __PS3__; PSL1GHT's `ppu_rules` also sets this one,
# which turns on libstdc++'s C99 stdio wrappers against this newlib
add_definitions("-D__PS3__" "-D_GLIBCXX11_USE_C99_STDIO")

set(PLATFORM_PS3 TRUE)
set(PS3 TRUE)

# Host tools the packaging steps in ncine_extra_sources.cmake invoke. cgcomp additionally needs NVIDIA's
# Cg Toolkit shared library at run time, so its directory has to be on LD_LIBRARY_PATH (see Docs/Consoles.dox).
set(PS3_CGCOMP "${PS3DEV}/bin/cgcomp" CACHE FILEPATH "Path to cgcomp (Cg -> RSX microcode)")
set(PS3_FSELF "${PS3DEV}/bin/fself" CACHE FILEPATH "Path to fself (ELF -> fake SELF)")
set(PS3_SELF "${PS3DEV}/bin/make_self" CACHE FILEPATH "Path to make_self (ELF -> signed SELF)")
set(PS3_SELF_NPDRM "${PS3DEV}/bin/make_self_npdrm" CACHE FILEPATH "Path to make_self_npdrm (ELF -> EBOOT.BIN)")
set(PS3_SFO "${PS3DEV}/bin/sfo" CACHE FILEPATH "Path to sfo (PARAM.SFO writer)")
set(PS3_SFO_XML "${PS3DEV}/bin/sfo.xml" CACHE FILEPATH "Path to the sfo tool's field template")
set(PS3_PKG "${PS3DEV}/bin/pkg" CACHE FILEPATH "Path to pkg (NPDRM package writer)")
set(PS3_PACKAGE_FINALIZE "${PS3DEV}/bin/package_finalize" CACHE FILEPATH "Path to package_finalize")
set(PS3_SPRXLINKER "${PS3DEV}/bin/sprxlinker" CACHE FILEPATH "Path to sprxlinker")
set(PS3_ICON0 "${PS3DEV}/bin/ICON0.PNG" CACHE FILEPATH "Fallback package icon shipped with PSL1GHT")
