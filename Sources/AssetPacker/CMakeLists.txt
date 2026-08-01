cmake_minimum_required(VERSION 3.15)
project(AssetPacker CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(AssetPacker)

target_include_directories(AssetPacker PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared
	${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies
)

# The packer does no networking, and WebRequest would drag in libcurl for nothing
set(ASSET_PACKER_SHARED_SOURCES ${SHARED_SOURCES})
list(REMOVE_ITEM ASSET_PACKER_SHARED_SOURCES ${NCINE_SOURCE_DIR}/Shared/IO/WebRequest.cpp)

set(SOURCES
	Main.cpp
	FontPacker.cpp
	FontPacker.h
	PngCodec.cpp
	PngCodec.h
	# The base layer and the original-data converters, both shared with the game; the engine itself is
	# deliberately not linked, so the tool has no renderer and no window backend
	${ASSET_PACKER_SHARED_SOURCES}
	${CONVERTER_SOURCES}
)

ncine_assign_source_group(PATH_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/../ ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

target_sources(AssetPacker PRIVATE ${SOURCES})

# Set explicitly rather than through ncine_apply_compiler_options(): the tool reports its progress through the
# usual logging macros, so DEATH_TRACE has to be on whatever the game is configured with, and it must be on
# WITHOUT DEATH_TRACE_VERBOSE_IO - a conversion touches thousands of files, and logging each one buries the
# output. NCINE_VERSION matches the game so the cache index it writes isn't seen as coming from another build.
target_compile_definitions(AssetPacker PRIVATE "CMAKE_BUILD" "DEATH_NO_THREADS" "DEATH_TRACE" "NCINE_VERSION=\"${NCINE_VERSION}\"")

if(TARGET ZLIB::ZLIB)
	target_link_libraries(AssetPacker PRIVATE ZLIB::ZLIB)
	target_compile_definitions(AssetPacker PRIVATE "WITH_ZLIB")
endif()
if(TARGET Threads::Threads)
	target_link_libraries(AssetPacker PRIVATE Threads::Threads)
endif()
