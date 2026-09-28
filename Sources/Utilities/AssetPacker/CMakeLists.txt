cmake_minimum_required(VERSION 3.15)
project(AssetPacker CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(AssetPacker)

target_include_directories(AssetPacker PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../..
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared
	${CMAKE_CURRENT_SOURCE_DIR}/../../Dependencies
)

# The packer does no networking, and WebRequest would drag in libcurl for nothing
set(ASSET_PACKER_SHARED_SOURCES ${SHARED_SOURCES})
list(REMOVE_ITEM ASSET_PACKER_SHARED_SOURCES ${NCINE_SOURCE_DIR}/Shared/IO/WebRequest.cpp)

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

ncine_assign_source_group(PATH_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/../../ ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

target_sources(AssetPacker PRIVATE ${SOURCES})

# Set explicitly rather than through ncine_apply_compiler_options(): the tool reports its progress through the
# usual logging macros, so DEATH_TRACE has to be on whatever the game is configured with, and it must be on
# WITHOUT DEATH_TRACE_VERBOSE_IO - a conversion touches thousands of files, and logging each one buries the
# output. NCINE_VERSION matches the game so the cache index it writes isn't seen as coming from another build.
target_compile_definitions(AssetPacker PRIVATE "CMAKE_BUILD" "DEATH_NO_THREADS" "DEATH_TRACE" "NCINE_VERSION=\"${NCINE_VERSION}\"")

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
