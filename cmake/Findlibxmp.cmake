include(ncine_helpers)

# libxmp, the lightweight tracker player the targets that cannot run libopenmpt in real time use
# instead (today the classic Amiga, see `NCINE_WITH_XMP` in "ncine_options.cmake").
#
# An SDK or distribution that already packages it is used as it is - VitaSDK ships one, and so do most
# Linux distributions. Only the FULL library counts: "libxmp-lite", which some of them also package,
# drops the Galaxy Music System loaders that read the game's original ".j2b" modules, so it is never
# searched for. Everything else builds it from source, because the platforms that need it most (the
# classic Amiga, the consoles) have no package manager to install it from. The release tarball is
# pinned by hash, so that path is reproducible and a substituted archive fails the download rather
# than the compile.
if(NOT TARGET Libxmp)
	find_path(LIBXMP_INCLUDE_DIR "xmp.h")
	find_library(LIBXMP_LIBRARY NAMES xmp)
	mark_as_advanced(LIBXMP_INCLUDE_DIR LIBXMP_LIBRARY)
endif()

if(NOT TARGET Libxmp AND LIBXMP_INCLUDE_DIR AND LIBXMP_LIBRARY)
	message(STATUS "Using libxmp from \"${LIBXMP_LIBRARY}\"")

	add_library(Libxmp UNKNOWN IMPORTED)
	set_target_properties(Libxmp PROPERTIES
		IMPORTED_LOCATION "${LIBXMP_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${LIBXMP_INCLUDE_DIR}")
	get_filename_component(_libxmpExtension "${LIBXMP_LIBRARY}" LAST_EXT)
	if(_libxmpExtension STREQUAL "${CMAKE_STATIC_LIBRARY_SUFFIX}")
		# What the header keys its import/export attributes off - see the note below
		set_target_properties(Libxmp PROPERTIES
			INTERFACE_COMPILE_DEFINITIONS "LIBXMP_STATIC")
	endif()

	set(LIBXMP_FOUND TRUE)
elseif(NOT TARGET Libxmp)
	set(LIBXMP_VERSION "4.6.3")
	set(LIBXMP_URL "https://github.com/libxmp/libxmp/releases/download/libxmp-${LIBXMP_VERSION}/libxmp-${LIBXMP_VERSION}.tar.gz")
	message(STATUS "Downloading dependencies from \"${LIBXMP_URL}\"...")

	include(FetchContent)
	FetchContent_Declare(
		LibxmpGit
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
		URL ${LIBXMP_URL}
		URL_HASH SHA256=b189a2ff3f3eef0008512e0fb27c2cdc27480bc1066b82590a84d02548fab96d
		# libxmp ships a CMakeLists of its own, which this must NOT add: it configures its own library,
		# tools and tests, probes the host and installs. Naming a subdirectory that does not exist is the
		# documented way to populate the sources and nothing else - the target below is built from them
		# the way this project builds its other vendored C dependencies.
		SOURCE_SUBDIR do-not-configure-upstream
	)
	FetchContent_MakeAvailable(LibxmpGit)

	ncine_add_dependency(Libxmp STATIC)

	set(LIBXMP_DIR "${libxmpgit_SOURCE_DIR}")
	set(LIBXMP_INCLUDE_DIR "${LIBXMP_DIR}/include")
	set_target_properties(Libxmp PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES ${LIBXMP_INCLUDE_DIR})
	# What the header keys its import/export attributes off; without it a Windows build would declare
	# every entry point `__declspec(dllimport)` and link against an import library that is not there
	target_compile_definitions(Libxmp PUBLIC "LIBXMP_STATIC")

	# The dependency's own file set (117 C files) is globbed rather than listed: it is taken verbatim
	# from the release and never edited here
	file(GLOB LIBXMP_SOURCES
		"${LIBXMP_DIR}/src/*.c"
		"${LIBXMP_DIR}/src/loaders/*.c"
		"${LIBXMP_DIR}/src/loaders/prowizard/*.c"
		"${LIBXMP_DIR}/src/depackers/*.c"
		"${LIBXMP_DIR}/src/depackers/lhasa/*.c")
	# Dead code upstream: libxmp's own Makefile leaves these five ProWizard files out of the build
	# (PROWIZ_OBJS2), and one of them includes a header that exists nowhere
	foreach(_xmpDead pm.c pm01.c pm20.c pm40.c pp30.c)
		list(REMOVE_ITEM LIBXMP_SOURCES "${LIBXMP_DIR}/src/loaders/prowizard/${_xmpDead}")
	endforeach()
	# Not translation units: lhasa's Makefile lists these as sources the decoder files #include
	foreach(_xmpInline bit_stream_reader.c lh_new_decoder.c pma_common.c tree_decode.c)
		list(REMOVE_ITEM LIBXMP_SOURCES "${LIBXMP_DIR}/src/depackers/lhasa/${_xmpInline}")
	endforeach()

	ncine_assign_source_group(PATH_PREFIX ${LIBXMP_DIR} FILES ${LIBXMP_SOURCES} SKIP_EXTERNAL)
	target_sources(Libxmp PRIVATE ${LIBXMP_SOURCES})
	target_include_directories(Libxmp PRIVATE ${LIBXMP_INCLUDE_DIR} "${LIBXMP_DIR}/src")

	set(LIBXMP_FOUND TRUE)
	set(LIBXMP_STATIC TRUE)
	mark_as_advanced(LIBXMP_STATIC)
endif()
