cmake_minimum_required(VERSION 3.15)
project(AssetPacker C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Only the vendored codecs are C; libxm needs the GNU extensions on top of C11
set(CMAKE_C_STANDARD 11)

# The Nintendo 64 formats (VADPCM and ULC `.wav64`, `.xm64`, MPEG-1 cinematics) can be produced by encoders built into
# the tool instead of libdragon's converters, see N64Content.h - the web build has nothing else. They are built on C
# codecs vendored in Sources/Dependencies that are only known to compile with GCC and Clang (libxm uses GNU statement
# expressions), so they are left out of MSVC builds, which keep needing libdragon's converters for that profile.
if(MSVC AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
	set(_assetPackerN64EncodersDefault OFF)
else()
	set(_assetPackerN64EncodersDefault ON)
endif()
option(ASSETPACKER_WITH_N64_ENCODERS "Build the encoders for the Nintendo 64 formats into AssetPacker" ${_assetPackerN64EncodersDefault})
unset(_assetPackerN64EncodersDefault)

add_executable(AssetPacker)

target_include_directories(AssetPacker PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../..
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared
	${CMAKE_CURRENT_SOURCE_DIR}/../../Dependencies
)

# The packer does no networking, and WebRequest would drag in libcurl for nothing. Nor does it open files picked in a
# browser, which is what EmscriptenFileStream is for - and that one needs the asynchronous runtime the game is linked with.
set(ASSET_PACKER_SHARED_SOURCES ${SHARED_SOURCES})
list(REMOVE_ITEM ASSET_PACKER_SHARED_SOURCES ${NCINE_SOURCE_DIR}/Shared/IO/WebRequest.cpp ${NCINE_SOURCE_DIR}/Shared/IO/EmscriptenFileStream.cpp)

set(SOURCES
	Main.cpp
	CartridgeImage.cpp
	CartridgeImage.h
	DiscImage.cpp
	DiscImage.h
	FontPacker.cpp
	FontPacker.h
	Iso9660.cpp
	Iso9660.h
	ModuleConverter.cpp
	ModuleConverter.h
	N64Content.cpp
	N64Content.h
	PngCodec.cpp
	PngCodec.h
	SpriteRepacker.cpp
	SpriteRepacker.h
	# The base layer and the original-data converters, both shared with the game; the engine itself is
	# deliberately not linked, so the tool has no renderer and no window backend
	${ASSET_PACKER_SHARED_SOURCES}
	${CONVERTER_SOURCES}
	# Reads the JSON font descriptions, the same library the game parses its own JSON with
	${NCINE_SOURCE_DIR}/Dependencies/jsoncpp/reader.cpp
	${NCINE_SOURCE_DIR}/Dependencies/jsoncpp/value.cpp
	${NCINE_SOURCE_DIR}/Dependencies/jsoncpp/writer.cpp
)

if(ASSETPACKER_WITH_N64_ENCODERS)
	set(_dependencies "${NCINE_SOURCE_DIR}/Dependencies")
	list(APPEND SOURCES
		BigEndianWriter.h
		Mpeg1Encoder.cpp
		Mpeg1Encoder.h
		Wav64Writer.cpp
		Wav64Writer.h
		Xm64Converter.cpp
		Xm64Converter.h
		# VADPCM, the codec of the sound effects and of the samples of the modules
		${_dependencies}/vadpcm/codec/autocorr.c
		${_dependencies}/vadpcm/codec/decode.c
		${_dependencies}/vadpcm/codec/encode.c
		${_dependencies}/vadpcm/codec/error.c
		${_dependencies}/vadpcm/codec/predictor.c
		${_dependencies}/vadpcm/codec/random.c
		# ULC, the codec of the pre-rendered music
		${_dependencies}/ulc/ulcEncoder.c
		${_dependencies}/ulc/ulcEncoder_BlockTransform.c
		${_dependencies}/ulc/ulcEncoder_Encode.c
		${_dependencies}/ulc/ulcEncoder_NoiseFill.c
		${_dependencies}/ulc/ulcEncoder_Psyopt.c
		${_dependencies}/ulc/ulcEncoder_WindowControl.c
		${_dependencies}/ulc/libfourier/Fourier_ApplyAmplitude.c
		${_dependencies}/ulc/libfourier/Fourier_ApplyWindow.c
		${_dependencies}/ulc/libfourier/Fourier_CenterFFT.c
		${_dependencies}/ulc/libfourier/Fourier_DCT2.c
		${_dependencies}/ulc/libfourier/Fourier_DCT4.c
		${_dependencies}/ulc/libfourier/Fourier_IMDCT.c
		${_dependencies}/ulc/libfourier/Fourier_MDCT.c
		# libxm, which loads the modules and plays them through to size the buffers of the console's player
		${_dependencies}/libxm/context.c
		${_dependencies}/libxm/load.c
		${_dependencies}/libxm/play.c
	)
	# The VADPCM sources include their headers through the name of their own directory
	target_include_directories(AssetPacker PRIVATE "${_dependencies}/vadpcm")
	target_compile_definitions(AssetPacker PRIVATE "ASSETPACKER_WITH_N64_ENCODERS")
	unset(_dependencies)
endif()

ncine_assign_source_group(PATH_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/../../ ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

target_sources(AssetPacker PRIVATE ${SOURCES})

# Set explicitly rather than through ncine_apply_compiler_options(): the tool reports its progress through the
# usual logging macros, so DEATH_TRACE has to be on whatever the game is configured with, and it must be on
# WITHOUT DEATH_TRACE_VERBOSE_IO - a conversion touches thousands of files, and logging each one buries the
# output. NCINE_VERSION matches the game so the cache index it writes isn't seen as coming from another build,
# and NCINE_APP_NAME and NCINE_GAMECUBE_GAME_ID are the title and the game ID a GameCube disc image is given
# unless others are named.
target_compile_definitions(AssetPacker PRIVATE "CMAKE_BUILD" "DEATH_NO_THREADS" "DEATH_TRACE" "NCINE_VERSION=\"${NCINE_VERSION}\""
	"NCINE_APP_NAME=\"${NCINE_APP_NAME}\"" "NCINE_GAMECUBE_GAME_ID=\"${GAMECUBE_GAME_ID}\"")

if(WIN32)
	# Not going through ncine_apply_compiler_options() also means missing the Unicode switch it sets for the game.
	# Without it the TCHAR-generic Win32 entry points resolve to their ANSI variants, which the wide string
	# literals the shared sources pass to them don't convert to.
	target_compile_definitions(AssetPacker PRIVATE "_UNICODE" "UNICODE")
elseif(APPLE)
	# Shared/IO/FileSystem.cpp reaches for the Objective-C runtime directly in MoveToTrash() and
	# LaunchDirectoryAsync(), so the frameworks owning the classes it looks up by name have to be linked even
	# though the tool itself has no user interface (Foundation also brings in libobjc, where objc_msgSend lives)
	target_link_libraries(AssetPacker PRIVATE "-framework Foundation" "-framework AppKit")
endif()

if(TARGET ZLIB::ZLIB)
	target_link_libraries(AssetPacker PRIVATE ZLIB::ZLIB)
	target_compile_definitions(AssetPacker PRIVATE "WITH_ZLIB")

	# The prebuilt Windows zlib is a DLL, and the deployment step in "ncine_extra_sources.cmake" copies the
	# runtime dependencies next to the game only, so the tool has to bring its own copy - without it the
	# executable doesn't even start. The imported target is a static library or an interface on every other
	# platform, where `$<TARGET_FILE:>` would point at something that must not be deployed.
	get_target_property(_zlibTargetType ZLIB::ZLIB TYPE)
	if(WIN32 AND NCINE_COPY_DEPENDENCIES AND _zlibTargetType STREQUAL "SHARED_LIBRARY")
		add_custom_command(TARGET AssetPacker POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ZLIB::ZLIB>" "$<TARGET_FILE_DIR:AssetPacker>"
			VERBATIM)
	endif()
	unset(_zlibTargetType)
endif()
if(TARGET Threads::Threads)
	target_link_libraries(AssetPacker PRIVATE Threads::Threads)
endif()

# LZ4 writes the sprite sheets and tilesets of the trees for the consoles that read them that way (see
# ProfileUsesLz4Images() in Main.cpp), built from source like the game's where the game itself has none
if(NOT TARGET Lz4 AND NCINE_DOWNLOAD_DEPENDENCIES)
	find_package(Lz4)
endif()
if(TARGET Lz4)
	target_link_libraries(AssetPacker PRIVATE Lz4)
	target_compile_definitions(AssetPacker PRIVATE "WITH_LZ4")
endif()

# libopenmpt renders what cannot be translated for the Nintendo 64 - the tracker formats other than the game's own
# and XM - into recordings, and the music under the cinematics into their soundtracks (see N64Content.cpp). The
# tool works without it; those tracks are then left out of a Nintendo 64 tree.
if(TARGET libopenmpt::libopenmpt)
	target_link_libraries(AssetPacker PRIVATE libopenmpt::libopenmpt)
	target_compile_definitions(AssetPacker PRIVATE "ASSETPACKER_WITH_OPENMPT")
endif()

if(EMSCRIPTEN)
	# The web version (see Web/README.md): a module a page instantiates in a worker and runs like the command-line
	# tool, with the game's own content - the repository's "Content" - baked into its file system at "/Content",
	# and the user's files mounted next to it by the page. There are no threads (the tool is single-threaded
	# anyway, and a page that does not need SharedArrayBuffer can be hosted anywhere), memory grows as needed up
	# to what 32-bit WebAssembly can address, and the stack is made large enough for the converters' buffers.
	# libopenmpt reports errors by throwing, so exceptions have to be catchable.
	target_link_options(AssetPacker PRIVATE
		"SHELL:-s MODULARIZE=1"
		"SHELL:-s EXPORT_NAME=createAssetPacker"
		"SHELL:-s INVOKE_RUN=0"
		"SHELL:-s ENVIRONMENT=web,worker,node"
		"SHELL:-s ALLOW_MEMORY_GROWTH=1"
		"SHELL:-s MAXIMUM_MEMORY=4GB"
		"SHELL:-s STACK_SIZE=8MB"
		"SHELL:-s FORCE_FILESYSTEM=1"
		"SHELL:-s EXPORTED_RUNTIME_METHODS=callMain,FS"
		"-fexceptions"
		"-lworkerfs.js"
		"-lnodefs.js"
		"SHELL:--preload-file \"${NCINE_CONTENT_DIR}@/Content\""
		"SHELL:--exclude-file *.po")

	# The page that drives it goes next to it, so the build directory can be served as it is
	add_custom_command(TARGET AssetPacker POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Web/index.html"
			"${CMAKE_CURRENT_SOURCE_DIR}/Web/assetpacker-worker.js" "$<TARGET_FILE_DIR:AssetPacker>"
		VERBATIM)
endif()
