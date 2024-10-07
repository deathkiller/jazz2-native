#
# FindBackward.cmake
# Copyright 2013-2024 Google Inc. All Rights Reserved.
# Copyright © 2024 Dan R.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

include(ncine_helpers)
include(FindPackageHandleStandardArgs)
include(CMakeDependentOption)

cmake_dependent_option(STACK_DETAILS_AUTO_DETECT "Auto detect Backward's stack details dependencies" ON "NOT WIN32" OFF)

set(STACK_WALKING_UNWIND ON CACHE BOOL "Use compiler's unwind API")
set(STACK_WALKING_BACKTRACE OFF CACHE BOOL "Use backtrace from (e)glibc for stack walking")
set(STACK_WALKING_LIBUNWIND OFF CACHE BOOL "Use libunwind for stack walking")

set(STACK_DETAILS_BACKTRACE_SYMBOL OFF CACHE BOOL "Use backtrace from (e)glibc for symbols resolution")
set(STACK_DETAILS_DW OFF CACHE BOOL "Use libdw to read debug info")
set(STACK_DETAILS_BFD OFF CACHE BOOL "Use libbfd to read debug info")
set(STACK_DETAILS_DWARF OFF CACHE BOOL "Use libdwarf/libelf to read debug info")

if(STACK_WALKING_LIBUNWIND)
	# libunwind works on the macOS without having to add special include paths or libraries
	if(NOT APPLE)
		find_path(LIBUNWIND_INCLUDE_DIR NAMES "libunwind.h")
		find_library(LIBUNWIND_LIBRARY unwind)

		if(LIBUNWIND_LIBRARY)
			include(CheckSymbolExists)
			check_symbol_exists(UNW_INIT_SIGNAL_FRAME libunwind.h HAVE_UNW_INIT_SIGNAL_FRAME)
			if(NOT HAVE_UNW_INIT_SIGNAL_FRAME)
				message(STATUS "libunwind does not support unwinding from signal handler frames")
			endif()
		endif()

		set(LIBUNWIND_INCLUDE_DIRS ${LIBUNWIND_INCLUDE_DIR})
		set(LIBDWARF_LIBRARIES ${LIBUNWIND_LIBRARY})
		find_package_handle_standard_args(libunwind DEFAULT_MSG LIBUNWIND_LIBRARY LIBUNWIND_INCLUDE_DIR)
		mark_as_advanced(LIBUNWIND_INCLUDE_DIR LIBUNWIND_LIBRARY)
		list(APPEND _BACKWARD_LIBRARIES ${LIBUNWIND_LIBRARY})
	endif()

	# Disable other unwinders if libunwind is found
	set(STACK_WALKING_UNWIND OFF)
	set(STACK_WALKING_BACKTRACE OFF)
endif()

if(${STACK_DETAILS_AUTO_DETECT})
	if(NOT CMAKE_VERSION VERSION_LESS 3.17)
		set(_name_mismatched_arg NAME_MISMATCHED)
	endif()
	# Find libdw
	find_path(LIBDW_INCLUDE_DIR NAMES "elfutils/libdw.h" "elfutils/libdwfl.h")
	find_library(LIBDW_LIBRARY dw)
	# In case it's statically linked, look for all the possible dependencies
	find_library(LIBELF_LIBRARY elf)
	find_library(LIBPTHREAD_LIBRARY pthread)
	find_library(LIBZ_LIBRARY z)
	find_library(LIBBZ2_LIBRARY bz2)
	find_library(LIBLZMA_LIBRARY lzma)
	find_library(LIBZSTD_LIBRARY zstd)
	set(LIBDW_INCLUDE_DIRS ${LIBDW_INCLUDE_DIR})
	set(LIBDW_LIBRARIES ${LIBDW_LIBRARY}
		$<$<BOOL:${LIBELF_LIBRARY}>:${LIBELF_LIBRARY}>
		$<$<BOOL:${LIBPTHREAD_LIBRARY}>:${LIBPTHREAD_LIBRARY}>
		$<$<BOOL:${LIBZ_LIBRARY}>:${LIBZ_LIBRARY}>
		$<$<BOOL:${LIBBZ2_LIBRARY}>:${LIBBZ2_LIBRARY}>
		$<$<BOOL:${LIBLZMA_LIBRARY}>:${LIBLZMA_LIBRARY}>
		$<$<BOOL:${LIBZSTD_LIBRARY}>:${LIBZSTD_LIBRARY}>)
	find_package_handle_standard_args(libdw ${_name_mismatched_arg} REQUIRED_VARS LIBDW_LIBRARY LIBDW_INCLUDE_DIR)
	mark_as_advanced(LIBDW_INCLUDE_DIR LIBDW_LIBRARY)

	# Find libbfd
	find_path(LIBBFD_INCLUDE_DIR NAMES "bfd.h")
	find_path(LIBDL_INCLUDE_DIR NAMES "dlfcn.h")
	find_library(LIBBFD_LIBRARY bfd)
	find_library(LIBDL_LIBRARY dl)
	set(LIBBFD_INCLUDE_DIRS ${LIBBFD_INCLUDE_DIR} ${LIBDL_INCLUDE_DIR})
	set(LIBBFD_LIBRARIES ${LIBBFD_LIBRARY} ${LIBDL_LIBRARY})
	find_package_handle_standard_args(libbfd ${_name_mismatched_arg} REQUIRED_VARS LIBBFD_LIBRARY LIBBFD_INCLUDE_DIR
		LIBDL_LIBRARY LIBDL_INCLUDE_DIR)
	mark_as_advanced(LIBBFD_INCLUDE_DIR LIBBFD_LIBRARY LIBDL_INCLUDE_DIR LIBDL_LIBRARY)

	# Find libdwarf
	find_path(LIBDWARF_INCLUDE_DIR NAMES "libdwarf.h" PATH_SUFFIXES libdwarf)
	find_path(LIBELF_INCLUDE_DIR NAMES "libelf.h")
	find_path(LIBDL_INCLUDE_DIR NAMES "dlfcn.h")
	find_library(LIBDWARF_LIBRARY dwarf)
	find_library(LIBELF_LIBRARY elf)
	find_library(LIBDL_LIBRARY dl)
	set(LIBDWARF_INCLUDE_DIRS ${LIBDWARF_INCLUDE_DIR} ${LIBELF_INCLUDE_DIR} ${LIBDL_INCLUDE_DIR})
	set(LIBDWARF_LIBRARIES ${LIBDWARF_LIBRARY} ${LIBELF_LIBRARY} ${LIBDL_LIBRARY})
	find_package_handle_standard_args(libdwarf ${_name_mismatched_arg} REQUIRED_VARS LIBDWARF_LIBRARY LIBDWARF_INCLUDE_DIR
		LIBELF_LIBRARY LIBELF_INCLUDE_DIR LIBDL_LIBRARY LIBDL_INCLUDE_DIR)
	mark_as_advanced(LIBDWARF_INCLUDE_DIR LIBDWARF_LIBRARY LIBELF_INCLUDE_DIR LIBELF_LIBRARY LIBDL_INCLUDE_DIR LIBDL_LIBRARY)

	if(LIBDW_FOUND)
		LIST(APPEND _BACKWARD_INCLUDE_DIRS ${LIBDW_INCLUDE_DIRS})
		LIST(APPEND _BACKWARD_LIBRARIES ${LIBDW_LIBRARIES})
		set(STACK_DETAILS_DW ON)
		set(STACK_DETAILS_BFD OFF)
		set(STACK_DETAILS_DWARF OFF)
		set(STACK_DETAILS_BACKTRACE_SYMBOL OFF)
	elseif(LIBBFD_FOUND)
		LIST(APPEND _BACKWARD_INCLUDE_DIRS ${LIBBFD_INCLUDE_DIRS})
		LIST(APPEND _BACKWARD_LIBRARIES ${LIBBFD_LIBRARIES})

		# If we attempt to link against static bfd, make sure to link its dependencies too
		get_filename_component(bfd_lib_ext "${LIBBFD_LIBRARY}" EXT)
		if(bfd_lib_ext STREQUAL "${CMAKE_STATIC_LIBRARY_SUFFIX}")
			find_library(LIBSFRAME_LIBRARY NAMES sframe)
			if(LIBSFRAME_LIBRARY)
				list(APPEND _BACKWARD_LIBRARIES ${LIBSFRAME_LIBRARY})
			endif()

			list(APPEND _BACKWARD_LIBRARIES iberty z)
		endif()

		set(STACK_DETAILS_DW OFF)
		set(STACK_DETAILS_BFD ON)
		set(STACK_DETAILS_DWARF OFF)
		set(STACK_DETAILS_BACKTRACE_SYMBOL OFF)
	elseif(LIBDWARF_FOUND)
		LIST(APPEND _BACKWARD_INCLUDE_DIRS ${LIBDWARF_INCLUDE_DIRS})
		LIST(APPEND _BACKWARD_LIBRARIES ${LIBDWARF_LIBRARIES})

		set(STACK_DETAILS_DW OFF)
		set(STACK_DETAILS_BFD OFF)
		set(STACK_DETAILS_DWARF ON)
		set(STACK_DETAILS_BACKTRACE_SYMBOL OFF)
	else()
		set(STACK_DETAILS_DW OFF)
		set(STACK_DETAILS_BFD OFF)
		set(STACK_DETAILS_DWARF OFF)
		set(STACK_DETAILS_BACKTRACE_SYMBOL ON)
	endif()
else()
	if(STACK_DETAILS_DW)
		LIST(APPEND _BACKWARD_LIBRARIES dw)
	endif()

	if(STACK_DETAILS_BFD)
		LIST(APPEND _BACKWARD_LIBRARIES bfd dl)
	endif()

	if(STACK_DETAILS_DWARF)
		LIST(APPEND _BACKWARD_LIBRARIES dwarf elf)
	endif()
endif()

macro(map_definitions var_prefix define_prefix)
	foreach(def ${ARGN})
		if(${${var_prefix}${def}})
			LIST(APPEND _BACKWARD_DEFINITIONS "${define_prefix}${def}")
		endif()
	endforeach()
endmacro()

if(NOT _BACKWARD_DEFINITIONS)
	map_definitions("STACK_WALKING_" "BACKWARD_HAS_" UNWIND LIBUNWIND BACKTRACE)
	map_definitions("STACK_DETAILS_" "BACKWARD_HAS_" BACKTRACE_SYMBOL DW BFD DWARF)
endif()

if(WIN32)
	list(APPEND _BACKWARD_LIBRARIES dbghelp psapi)
	if(MINGW)
		include(CheckCXXCompilerFlag)
		check_cxx_compiler_flag(-gcodeview SUPPORT_WINDOWS_DEBUG_INFO)	
		if(SUPPORT_WINDOWS_DEBUG_INFO)
			set(CMAKE_EXE_LINKER_FLAGS "-Wl,--pdb=")
			add_compile_options(-gcodeview)
		else()
			set(MINGW_MSVCR_LIBRARY "msvcr90$<$<CONFIG:DEBUG>:d>" CACHE STRING "MinGW MSVC runtime import library")
			list(APPEND _BACKWARD_LIBRARIES ${MINGW_MSVCR_LIBRARY})
		endif()
	endif()	
endif()

#list(APPEND _BACKWARD_INCLUDE_DIRS ${BACKWARD_INCLUDE_DIR})
#list(APPEND _BACKWARD_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND _BACKWARD_INCLUDE_DIRS "${NCINE_SOURCE_DIR}/Shared")

set(BACKWARD_INCLUDE_DIRS ${_BACKWARD_INCLUDE_DIRS} CACHE INTERNAL "BACKWARD_INCLUDE_DIRS")
set(BACKWARD_DEFINITIONS ${_BACKWARD_DEFINITIONS} CACHE INTERNAL "BACKWARD_DEFINITIONS")
set(BACKWARD_LIBRARIES ${_BACKWARD_LIBRARIES} CACHE INTERNAL "BACKWARD_LIBRARIES")
mark_as_advanced(BACKWARD_INCLUDE_DIRS BACKWARD_DEFINITIONS BACKWARD_LIBRARIES)

# Expand each definition in BACKWARD_DEFINITIONS to its own cmake var and export to outer scope
foreach(var ${BACKWARD_DEFINITIONS})
	set(${var} ON)
	mark_as_advanced(${var})
endforeach()

ncine_add_dependency(Backward INTERFACE)

target_sources(Backward PUBLIC "${NCINE_SOURCE_DIR}/Shared/Core/Backward.h")

source_group("Header Files" FILES "${NCINE_SOURCE_DIR}/Shared/Core/Backward.h")

target_compile_definitions(Backward INTERFACE ${BACKWARD_DEFINITIONS})
target_include_directories(Backward INTERFACE ${BACKWARD_INCLUDE_DIRS})
if(BACKWARD_LIBRARIES)
	target_link_libraries(Backward INTERFACE ${BACKWARD_LIBRARIES})
endif()