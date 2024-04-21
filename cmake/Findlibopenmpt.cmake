include(ncine_helpers)

if(NOT NCINE_COMPILE_OPENMPT)
	find_path(LIBOPENMPT_INCLUDE_DIR DOC "Path to libopenmpt include directory"
		NAMES libopenmpt.h
		PATHS
		/usr/include/
		/usr/local/include/
		/usr/include/libopenmpt/
		/usr/local/include/libopenmpt/
		${EXTERNAL_INCLUDES_DIR}/libopenmpt/
	)
	mark_as_advanced(LIBOPENMPT_INCLUDE_DIR)

	if(EMSCRIPTEN)
		if(EXISTS "${NCINE_LIBS}/Emscripten/libopenmpt.a")
			set(LIBOPENMPT_LIBRARY "${NCINE_LIBS}/Emscripten/libopenmpt.a")
			mark_as_advanced(LIBOPENMPT_LIBRARY)
		endif()
	else()
		find_library(LIBOPENMPT_LIBRARY DOC "Path to libopenmpt library"
			NAMES libopenmpt openmpt
			NAMES_PER_DIR
			PATH_SUFFIXES
			lib64
			lib
			libx32
			lib/x64
			x86_64-w64-mingw32/lib
			PATHS
			/sw
			/opt/local
			/opt/csw
			/opt
			${NCINE_LIBS}/Linux/${CMAKE_SYSTEM_PROCESSOR}/
		)
		mark_as_advanced(LIBOPENMPT_LIBRARY)
	endif()

	if(NOT TARGET libopenmpt::libopenmpt)
		if(LIBOPENMPT_INCLUDE_DIR AND LIBOPENMPT_LIBRARY)
			if(EMSCRIPTEN)
				add_library(libopenmpt::libopenmpt STATIC IMPORTED)
				set(LIBOPENMPT_STATIC TRUE)
				mark_as_advanced(LIBOPENMPT_STATIC)
			else()
				add_library(libopenmpt::libopenmpt SHARED IMPORTED)
			endif()
		
			set_target_properties(libopenmpt::libopenmpt PROPERTIES
				IMPORTED_LOCATION ${LIBOPENMPT_LIBRARY}
				INTERFACE_INCLUDE_DIRECTORIES ${LIBOPENMPT_INCLUDE_DIR})
		else()
		endif()
	endif()
endif()

if(NOT TARGET libopenmpt::libopenmpt AND (NCINE_DOWNLOAD_DEPENDENCIES OR NCINE_COMPILE_OPENMPT))
	# Try to build `libopenmpt` from source
	set(LIBOPENMPT_URL "https://github.com/OpenMPT/openmpt/archive/libopenmpt-0.7.1.tar.gz")
	message(STATUS "Downloading dependencies from \"${LIBOPENMPT_URL}\"...")

	include(FetchContent)
	FetchContent_Declare(
		LibopenmptGit
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
		URL ${LIBOPENMPT_URL}
	)
	FetchContent_MakeAvailable(LibopenmptGit)
		
	ncine_add_dependency(Libopenmpt STATIC ALLOW_EXCEPTIONS)

	set(LIBOPENMPT_INCLUDE_DIR "${libopenmptgit_SOURCE_DIR}/libopenmpt/")
	set_target_properties(Libopenmpt PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES ${LIBOPENMPT_INCLUDE_DIR})

	target_compile_definitions(Libopenmpt PRIVATE "LIBOPENMPT_BUILD" "MPT_CHECK_CXX_IGNORE_WARNING_FINITEMATH")

	if(EMSCRIPTEN)
		target_compile_definitions(Libopenmpt PRIVATE "MPT_WITH_ZLIB" "MPT_WITH_MPG123" "MPT_WITH_VORBIS" "MPT_WITH_VORBISFILE" "MPT_BUILD_WASM")
		
		target_compile_options(Libopenmpt PUBLIC "SHELL:-s USE_ZLIB=1 -s USE_MPG123=1 -s USE_OGG=1 -s USE_VORBIS=1")
		target_link_options(Libopenmpt PUBLIC "SHELL:-s USE_ZLIB=1 -s USE_MPG123=1 -s USE_OGG=1 -s USE_VORBIS=1")
	else()
		find_package(ZLIB)

		# TODO: Add MPT_WITH_MPG123 and MPT_WITH_VORBIS support
		target_compile_definitions(Libopenmpt PRIVATE "MPT_WITH_ZLIB")
	endif()
		
	set(LIBOPENMPT_SOURCES)
	file(GLOB_RECURSE _files "${libopenmptgit_SOURCE_DIR}/common/*.cpp")
	list(APPEND LIBOPENMPT_SOURCES ${_files})
	file(GLOB_RECURSE _files "${libopenmptgit_SOURCE_DIR}/sounddsp/*.cpp")
	list(APPEND LIBOPENMPT_SOURCES ${_files})
	file(GLOB_RECURSE _files "${libopenmptgit_SOURCE_DIR}/soundlib/*.cpp")
	list(APPEND LIBOPENMPT_SOURCES ${_files})

	list(APPEND LIBOPENMPT_SOURCES
		"${libopenmptgit_SOURCE_DIR}/libopenmpt/libopenmpt_c.cpp"
		"${libopenmptgit_SOURCE_DIR}/libopenmpt/libopenmpt_cxx.cpp"
		"${libopenmptgit_SOURCE_DIR}/libopenmpt/libopenmpt_impl.cpp"
		"${libopenmptgit_SOURCE_DIR}/libopenmpt/libopenmpt_ext_impl.cpp")

	target_sources(Libopenmpt PRIVATE ${LIBOPENMPT_SOURCES})
	target_include_directories(Libopenmpt PRIVATE "${libopenmptgit_SOURCE_DIR}" "${libopenmptgit_SOURCE_DIR}/common" "${libopenmptgit_SOURCE_DIR}/src" "${ZLIB_INCLUDE_DIRS}")

	file(WRITE "${libopenmptgit_SOURCE_DIR}/common/svn_version.h" "#pragma once\n#define OPENMPT_VERSION_REVISION 0")

	add_library(libopenmpt::libopenmpt ALIAS Libopenmpt)
	set(LIBOPENMPT_STATIC TRUE)
	mark_as_advanced(LIBOPENMPT_STATIC)
endif()

if(TARGET libopenmpt::libopenmpt AND NOT LIBOPENMPT_STATIC)
	include(FindPackageHandleStandardArgs)
	find_package_handle_standard_args(libopenmpt REQUIRED_VARS LIBOPENMPT_LIBRARY LIBOPENMPT_INCLUDE_DIR)
endif()