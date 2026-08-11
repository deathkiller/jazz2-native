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

	if(PLATFORM_PS3)
		# PSL1GHT's libstdc++ is built without gthreads, so it has no std::mutex - and libopenmpt reaches for
		# one unless it believes the platform is single-threaded. It decides that in `mpt/base/detect_quirks.hpp`,
		# which sets MPT_PLATFORM_MULTITHREADED to 1 and then clears it again for the platforms it knows about
		# (DJGPP, Emscripten without pthreads). The PS3 is not on that list and there is no way in from outside,
		# because the header assigns the macro unconditionally before anything can override it - so the value is
		# patched into the fetched copy, adding the same kind of block upstream already has for the others.
		# `mpt/mutex/mutex.hpp` then selects MPT_MUTEX_NONE and <mutex> is never included. Nothing else is
		# affected: decoding a module is single-threaded work anyway, and the engine only ever pulls from one
		# thread (see AudioReaderMpt).
		set(_quirksHeader "${libopenmptgit_SOURCE_DIR}/src/mpt/base/detect_quirks.hpp")
		file(READ "${_quirksHeader}" _quirksSource)
		if(NOT _quirksSource MATCHES "MPT_PLATFORM_SINGLETHREADED_OVERRIDE")
			string(REPLACE
				"#if (MPT_OS_EMSCRIPTEN && !defined(__EMSCRIPTEN_PTHREADS__))"
				"#if defined(MPT_PLATFORM_SINGLETHREADED_OVERRIDE)\n#undef MPT_PLATFORM_MULTITHREADED\n#define MPT_PLATFORM_MULTITHREADED 0\n#endif\n\n#if (MPT_OS_EMSCRIPTEN && !defined(__EMSCRIPTEN_PTHREADS__))"
				_quirksPatched "${_quirksSource}")
			if(_quirksPatched STREQUAL _quirksSource)
				message(FATAL_ERROR "Could not make libopenmpt single-threaded: the anchor in \"${_quirksHeader}\" changed, so the PlayStation 3 build would fail on the missing std::mutex")
			endif()
			file(WRITE "${_quirksHeader}" "${_quirksPatched}")
			message(STATUS "Patched libopenmpt for single-threaded operation (no std::mutex on PSL1GHT)")
		endif()
		# The second define only silences the note the library emits when a single-threaded platform still has
		# GCC's threadsafe static initialization, which is exactly this configuration and is harmless
		target_compile_definitions(Libopenmpt PRIVATE "MPT_PLATFORM_SINGLETHREADED_OVERRIDE"
			"MPT_CHECK_CXX_IGNORE_WARNING_SINGLETHREADED_THREADSAFE_STATICS")
	endif()

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

	ncine_assign_source_group(PATH_PREFIX ${libopenmptgit_SOURCE_DIR} FILES ${LIBOPENMPT_SOURCES} SKIP_EXTERNAL)
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