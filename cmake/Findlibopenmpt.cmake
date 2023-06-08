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
	elseif(NCINE_DOWNLOAD_DEPENDENCIES)
		# Try to build `libopenmpt` from source
		set(LIBOPENMPT_URL "https://github.com/OpenMPT/openmpt/archive/libopenmpt-0.7.1.tar.gz")
		message(STATUS "Downloading dependencies from \"${LIBOPENMPT_URL}\"...")
		
		include(FetchContent)
		FetchContent_Declare(
			libopenmpt_git
			DOWNLOAD_EXTRACT_TIMESTAMP TRUE
			URL ${LIBOPENMPT_URL}
		)
		FetchContent_MakeAvailable(libopenmpt_git)
		
		add_library(libopenmpt_src STATIC)
		target_compile_features(libopenmpt_src PUBLIC cxx_std_20)
		set_target_properties(libopenmpt_src PROPERTIES CXX_EXTENSIONS OFF)
		set(LIBOPENMPT_INCLUDE_DIR "${libopenmpt_git_SOURCE_DIR}/libopenmpt/")
		set_target_properties(libopenmpt_src PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES ${LIBOPENMPT_INCLUDE_DIR})

		if(EMSCRIPTEN)
			target_compile_definitions(libopenmpt_src PRIVATE "LIBOPENMPT_BUILD" "MPT_WITH_ZLIB" "MPT_WITH_MPG123" "MPT_WITH_VORBIS" "MPT_WITH_VORBISFILE" "MPT_BUILD_WASM")
		
			target_compile_options(libopenmpt_src PUBLIC "SHELL:-s USE_ZLIB=1 -s USE_MPG123=1 -s USE_OGG=1 -s USE_VORBIS=1")
			target_link_options(libopenmpt_src PUBLIC "SHELL:-s USE_ZLIB=1 -s USE_MPG123=1 -s USE_OGG=1 -s USE_VORBIS=1")
		else()
			find_package(ZLIB)

			# TODO: Add MPT_WITH_MPG123 and MPT_WITH_VORBIS support
			target_compile_definitions(libopenmpt_src PRIVATE "LIBOPENMPT_BUILD" "MPT_WITH_ZLIB")
		endif()
		
		if(MSVC)
			# Build with Multiple Processes and force UTF-8
			target_compile_options(libopenmpt_src PRIVATE /MP /utf-8)
			# Extra optimizations in release
			target_compile_options(libopenmpt_src PRIVATE $<$<CONFIG:Release>:/O2 /Oi /Qpar /Gy>)
			target_link_options(libopenmpt_src PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:NOICF>)
			# Specifies the architecture for code generation (IA32, SSE, SSE2, AVX, AVX2, AVX512)
			if(NCINE_ARCH_EXTENSIONS)
				target_compile_options(libopenmpt_src PRIVATE /arch:${NCINE_ARCH_EXTENSIONS})
			endif()
			# Enabling Whole Program Optimization
			if(NCINE_LINKTIME_OPTIMIZATION)
				target_compile_options(libopenmpt_src PRIVATE $<$<CONFIG:Release>:/GL>)
				target_link_options(libopenmpt_src PRIVATE $<$<CONFIG:Release>:/LTCG>)
			endif()
		else()
			if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
				if(NCINE_LINKTIME_OPTIMIZATION AND NOT (MINGW OR MSYS OR ANDROID))
					target_compile_options(libopenmpt_src PRIVATE $<$<CONFIG:Release>:-flto=auto>)
					target_link_options(libopenmpt_src PRIVATE $<$<CONFIG:Release>:-flto=auto>)
				endif()
			elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
				if(NOT EMSCRIPTEN)
					# Extra optimizations in release
					target_compile_options(libopenmpt_src PRIVATE $<$<CONFIG:Release>:-Ofast>)
				endif()
				# Enabling ThinLTO of Clang 4
				if(NCINE_LINKTIME_OPTIMIZATION)
					if(EMSCRIPTEN)
						target_compile_options(libopenmpt_src PRIVATE $<$<CONFIG:Release>:-flto>)
						target_link_options(libopenmpt_src PRIVATE $<$<CONFIG:Release>:-flto>)
					elseif(NOT (MINGW OR MSYS OR ANDROID))
						target_compile_options(libopenmpt_src PRIVATE $<$<CONFIG:Release>:-flto=thin>)
						target_link_options(libopenmpt_src PRIVATE $<$<CONFIG:Release>:-flto=thin>)
					endif()
				endif()
			endif()
		endif()
		
		set(LIBOPENMPT_SOURCES)
		file(GLOB_RECURSE _files "${libopenmpt_git_SOURCE_DIR}/common/*.cpp")
		list(APPEND LIBOPENMPT_SOURCES ${_files})
		file(GLOB_RECURSE _files "${libopenmpt_git_SOURCE_DIR}/sounddsp/*.cpp")
		list(APPEND LIBOPENMPT_SOURCES ${_files})
		file(GLOB_RECURSE _files "${libopenmpt_git_SOURCE_DIR}/soundlib/*.cpp")
		list(APPEND LIBOPENMPT_SOURCES ${_files})
		list(APPEND LIBOPENMPT_SOURCES "${libopenmpt_git_SOURCE_DIR}/libopenmpt/libopenmpt_c.cpp")
		list(APPEND LIBOPENMPT_SOURCES "${libopenmpt_git_SOURCE_DIR}/libopenmpt/libopenmpt_cxx.cpp")
		list(APPEND LIBOPENMPT_SOURCES "${libopenmpt_git_SOURCE_DIR}/libopenmpt/libopenmpt_impl.cpp")
		list(APPEND LIBOPENMPT_SOURCES "${libopenmpt_git_SOURCE_DIR}/libopenmpt/libopenmpt_ext_impl.cpp")
		target_sources(libopenmpt_src PRIVATE ${LIBOPENMPT_SOURCES})
		target_include_directories(libopenmpt_src PRIVATE "${libopenmpt_git_SOURCE_DIR}" "${libopenmpt_git_SOURCE_DIR}/common" "${libopenmpt_git_SOURCE_DIR}/src" "${ZLIB_INCLUDE_DIRS}")

		file(WRITE "${libopenmpt_git_SOURCE_DIR}/common/svn_version.h" "#pragma once\n#define OPENMPT_VERSION_REVISION 0")

		add_library(libopenmpt::libopenmpt ALIAS libopenmpt_src)
		set(LIBOPENMPT_STATIC TRUE)
		mark_as_advanced(LIBOPENMPT_STATIC)
	endif()
endif()

if(NOT LIBOPENMPT_STATIC)
	include(${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake)
	find_package_handle_standard_args(libopenmpt REQUIRED_VARS LIBOPENMPT_LIBRARY LIBOPENMPT_INCLUDE_DIR)
endif()