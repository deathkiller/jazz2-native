cmake_minimum_required(VERSION 3.13)
project(Jazz2)

# Project metadata
set(NCINE_ROOT ${CMAKE_SOURCE_DIR})
set(NCINE_SOURCE_DIR "${NCINE_ROOT}/Sources/")
set(NCINE_APP "jazz2")
set(NCINE_APP_NAME "Jazz² Resurrection")
set(NCINE_APP_DESCRIPTION "Open-source reimplementation of Jazz Jackrabbit 2")
set(NCINE_APP_DESCRIPTION_FULL "Jazz² Resurrection is reimplementation of the game Jazz Jackrabbit 2 released in 1998. Supports various versions of the game (Shareware Demo, Holiday Hare '98, The Secret Files and Christmas Chronicles). Also, it partially supports some features of JJ2+ extension and MLLE.\n\nFurther information can be found here: http://deat.tk/jazz2/")
set(NCINE_APP_VENDOR "Dan R.")
set(NCINE_REVERSE_DNS "jazz2.resurrection")
set(NCINE_VERSION "2.2.2")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)
get_filename_component(PARENT_SOURCE_DIR ${CMAKE_SOURCE_DIR} DIRECTORY)

if(NOT CMAKE_GENERATOR_PLATFORM OR "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "${CMAKE_SYSTEM_PROCESSOR}")
	if(EMSCRIPTEN)
		message(STATUS "Compiling for architecture: WASM (${CMAKE_SYSTEM_PROCESSOR})")
	elseif(NINTENDO_SWITCH)
		message(STATUS "Compiling for architecture: ${CMAKE_SYSTEM_PROCESSOR} (Nintendo Switch)")
	elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
		message(STATUS "Compiling for architecture: ${CMAKE_OSX_ARCHITECTURES} (${CMAKE_SYSTEM_PROCESSOR})")
	else()
		message(STATUS "Compiling for architecture: ${CMAKE_SYSTEM_PROCESSOR}")
	endif()
else()
	message(STATUS "Compiling for architecture: ${CMAKE_GENERATOR_PLATFORM} (${CMAKE_SYSTEM_PROCESSOR})")
endif()

if(APPLE AND CMAKE_OSX_ARCHITECTURES)
	if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
		set(NCINE_ARM_PROCESSOR TRUE)
	elseif(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
		# Default architecture
	else()
		message(FATAL_ERROR "Architecture \"${CMAKE_OSX_ARCHITECTURES}\" is not supported. Only one architecture (arm64 or x86_64) could be specified at build time.")
	endif()
else()
	string(FIND ${CMAKE_SYSTEM_PROCESSOR} "arm" ARM_SUBSTRING_FOUND)
	string(FIND ${CMAKE_SYSTEM_PROCESSOR} "aarch64" AARCH64_SUBSTRING_FOUND)
	if (ARM_SUBSTRING_FOUND GREATER -1 OR AARCH64_SUBSTRING_FOUND GREATER -1)
		set(NCINE_ARM_PROCESSOR TRUE)
	endif()
endif()
	
include(ncine_options)

if(NOT IS_DIRECTORY ${NCINE_DATA_DIR})
	message(WARNING "Content directory not found at: ${NCINE_DATA_DIR}")
else()
	message(STATUS "Content directory: ${NCINE_DATA_DIR}")
endif()

include(ncine_get_version)
include(ncine_imported_targets)
include(ncine_imgui)
include(ncine_tracy)

if(NCINE_BUILD_ANDROID)
	add_library(${NCINE_APP} STATIC)
else()
	add_executable(${NCINE_APP})
	if(WINDOWS_PHONE OR WINDOWS_STORE)
		message(STATUS "Compiling for Windows RT")
	else()
		# Falling back to either GLFW or SDL2 if the other one is not available
		if(NOT GLFW_FOUND AND NOT SDL2_FOUND AND NOT Qt5_FOUND)
			message(FATAL_ERROR "No backend between SDL2, GLFW, and QT5 has been found")
		elseif(GLFW_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "GLFW")
			message(STATUS "Using GLFW as the preferred backend")
		elseif(SDL2_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "SDL2")
			message(STATUS "Using SDL2 as the preferred backend")
		elseif(Qt5_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "QT5")
			message(STATUS "Using QT5 as the preferred backend")
		elseif(SDL2_FOUND AND NOT GLFW_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "GLFW")
			set(NCINE_PREFERRED_BACKEND "SDL2")
			message(WARNING "Using SDL2 as backend because GLFW cannot be found")
		elseif(GLFW_FOUND AND NOT SDL2_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "SDL2")
			set(NCINE_PREFERRED_BACKEND "GLFW")
			message(WARNING "Using GLFW as backend because SDL2 cannot be found")
		endif()
	endif()
endif()

include(ncine_compiler_options)
include(ncine_headers)
include(ncine_sources)
include(ncine_extra_sources)
include(ncine_generated_sources)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(PATH_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/Sources")
foreach(FILE ${HEADERS} ${SOURCES}) 
	get_filename_component(PARENT_DIR "${FILE}" DIRECTORY)
	string(LENGTH "${PATH_PREFIX}" PATH_PREFIX_LENGTH)
	string(SUBSTRING "${PARENT_DIR}" 0 ${PATH_PREFIX_LENGTH} PATH_START)
	if(PATH_START STREQUAL "${PATH_PREFIX}")
		string(SUBSTRING "${PARENT_DIR}" ${PATH_PREFIX_LENGTH} -1 GROUP)
		string(REPLACE "/" "\\" GROUP "${GROUP}")

		# Group into "Source Files" and "Header Files"
		if("${FILE}" MATCHES ".*\\.cpp")
			set(GROUP "Source Files${GROUP}")
		elseif("${FILE}" MATCHES ".*\\.h")
			set(GROUP "Header Files${GROUP}")
		endif()
	else()
		# Path doesn't have common prefix, put it into "Dependencies"
		set(GROUP "Dependencies")
	endif()

	source_group("${GROUP}" FILES "${FILE}")
endforeach()

foreach(SOURCE_FILE IN LISTS SHADER_FILES)
	source_group("Shaders" FILES ${SOURCE_FILE})
endforeach()
foreach(SOURCE_FILE ${GENERATED_SOURCES})
	source_group("Generated Files" FILES ${SOURCE_FILE})
endforeach()

if(OPENGL_FOUND)
	target_link_libraries(${NCINE_APP} PRIVATE OpenGL::GL)
endif()
target_sources(${NCINE_APP} PRIVATE ${SOURCES} ${HEADERS} ${SHADER_FILES} ${GENERATED_SOURCES})

if(NCINE_WITH_IMGUI)
	target_include_directories(${NCINE_APP}
		INTERFACE $<BUILD_INTERFACE:${IMGUI_INCLUDE_ONLY_DIR}>
		PRIVATE $<BUILD_INTERFACE:${IMGUI_INCLUDE_ONLY_DIR}>)
endif()
if(NCINE_WITH_TRACY)
	target_include_directories(${NCINE_APP}
		PUBLIC $<BUILD_INTERFACE:${TRACY_INCLUDE_ONLY_DIR}/tracy>
		PUBLIC $<INSTALL_INTERFACE:include/tracy>)
endif()
target_include_directories(${NCINE_APP} PRIVATE "${NCINE_SOURCE_DIR}/Shared")
if(IS_DIRECTORY ${GENERATED_INCLUDE_DIR})
	get_filename_component(PARENT_GENERATED_INCLUDE_DIR ${GENERATED_INCLUDE_DIR} DIRECTORY)
	target_include_directories(${NCINE_APP}
		INTERFACE $<BUILD_INTERFACE:${PARENT_GENERATED_INCLUDE_DIR}>
		# Internal sources can access a generated header with or without the parent directory
		PRIVATE $<BUILD_INTERFACE:${PARENT_GENERATED_INCLUDE_DIR}>
		PRIVATE $<BUILD_INTERFACE:${GENERATED_INCLUDE_DIR}>)
endif()

# Windows RT uses custom packaging, enable it only for other platforms
if(NOT WINDOWS_PHONE AND NOT WINDOWS_STORE AND NOT ANDROID AND NOT NCINE_BUILD_ANDROID AND NOT NINTENDO_SWITCH)
	include(ncine_installation)
endif()
include(ncine_build_android)
include(ncine_strip_binaries)
