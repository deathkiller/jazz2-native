cmake_minimum_required(VERSION 3.7)
project(nCine-Android-${ANDROID_ABI})

# Check if we can use IFUNC for CPU dispatch. This part is already in `ncine_options.cmake`, but we need to check it also against Android NDK toolchain.
if(DEATH_CPU_USE_RUNTIME_DISPATCH)
	include(CheckCXXSourceCompiles)
	set(CMAKE_REQUIRED_QUIET ON)
	check_cxx_source_compiles("\
int fooImplementation() { return 42; }
#if defined(__ANDROID_API__) && __ANDROID_API__ < 30
#error need Android API 30+ to have AT_HWCAP passed into the resolver
#endif
extern \"C\" int(*fooDispatcher())() {
	return fooImplementation;
}
int foo() __attribute__((ifunc(\"fooDispatcher\")));
int main() { return foo() - 42; }\
		" _DEATH_CPU_CAN_USE_IFUNC)
	set(CMAKE_REQUIRED_QUIET OFF)
	if(_DEATH_CPU_CAN_USE_IFUNC)
		# DEATH_CPU_USE_RUNTIME_DISPATCH is already enabled
		set(DEATH_CPU_USE_IFUNC ON)
	else()
		set(DEATH_CPU_USE_RUNTIME_DISPATCH OFF)
		message(STATUS "GNU IFUNC is not supported on Android ${ANDROID_PLATFORM}")
	endif()
endif()

include(ncine_imported_targets)

if(NCINE_DYNAMIC_LIBRARY)
	add_library(${NCINE_APP} SHARED)
else()
	add_library(${NCINE_APP} STATIC)
endif()
add_library(ncine_main STATIC main.cpp)
add_library(jnicall_functions STATIC jnicall_functions.cpp)

target_link_libraries(${NCINE_APP} PRIVATE ${ANDROID_LIBRARY} ${EGL_LIBRARY} ${GLES3_LIBRARY} ${LOG_LIBRARY})
if(ANDROID_PLATFORM GREATER_EQUAL 30)
	target_link_libraries(${NCINE_APP} PRIVATE ${NATIVEWINDOW_LIBRARY})
endif()
target_link_libraries(ncine_main PRIVATE ${NCINE_APP})

include(ncine_compiler_options)

set(NCINE_SOURCE_DIR "${NCINE_ROOT}/Sources/")

include(ncine_headers)
include(ncine_sources)
include(ncine_extra_sources)

# Convert strings back to the original lists
string(REPLACE " " ";" GENERATED_SOURCES "${GENERATED_SOURCES}")
string(REPLACE " " ";" ANDROID_GENERATED_FLAGS "${ANDROID_GENERATED_FLAGS}")

target_sources(${NCINE_APP} PRIVATE ${SOURCES} ${HEADERS} ${SHADER_FILES} ${GENERATED_SOURCES})

target_include_directories(${NCINE_APP} PRIVATE ${NCINE_SOURCE_DIR}/Shared)
target_include_directories(ncine_main PRIVATE ${NCINE_SOURCE_DIR} ${NCINE_SOURCE_DIR}/Shared)
if(IS_DIRECTORY ${GENERATED_INCLUDE_DIR})
	target_include_directories(${NCINE_APP} PRIVATE ${GENERATED_INCLUDE_DIR})
	# Internal sources can access a generated header with or without the parent directory
	get_filename_component(PARENT_GENERATED_INCLUDE_DIR ${GENERATED_INCLUDE_DIR} DIRECTORY)
	target_include_directories(${NCINE_APP} PRIVATE ${PARENT_GENERATED_INCLUDE_DIR})
	target_include_directories(ncine_main PRIVATE ${PARENT_GENERATED_INCLUDE_DIR})
endif()

target_include_directories(${NCINE_APP} PRIVATE ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue)
target_include_directories(ncine_main PRIVATE ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue)
#if(NCINE_WITH_IMGUI)
#	target_include_directories(${NCINE_APP}
#		INTERFACE $<BUILD_INTERFACE:${IMGUI_INCLUDE_ONLY_DIR}>
#		PRIVATE $<BUILD_INTERFACE:${IMGUI_INCLUDE_ONLY_DIR}/ncine>)
#endif()
#if(NCINE_WITH_NUKLEAR)
#	target_include_directories(${NCINE_APP}
#		INTERFACE $<BUILD_INTERFACE:${NUKLEAR_INCLUDE_ONLY_DIR}>
#		PRIVATE $<BUILD_INTERFACE:${NUKLEAR_INCLUDE_ONLY_DIR}/ncine>)
#endif()
if(NCINE_WITH_TRACY)
	target_include_directories(${NCINE_APP}
		PUBLIC $<BUILD_INTERFACE:${TRACY_INCLUDE_ONLY_DIR}/tracy>
		PUBLIC $<INSTALL_INTERFACE:include/tracy>)
endif()

target_compile_definitions(${NCINE_APP} PRIVATE ${ANDROID_GENERATED_FLAGS})
target_link_libraries(${NCINE_APP} PRIVATE ${PRIVATE_LINK_LIBRARIES})

# Merged "native_app_glue" into "game" target
add_library(game SHARED ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
set(JNICALL_FUNCTIONS_LINKER_OPTIONS -Wl,--whole-archive jnicall_functions -Wl,--no-whole-archive)
target_link_libraries(game PRIVATE ${ANDROID_LIBRARY} ${LOG_LIBRARY} ${JNICALL_FUNCTIONS_LINKER_OPTIONS} ncine_main ${NCINE_APP})
target_include_directories(game PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue)
if(IS_DIRECTORY ${GENERATED_INCLUDE_DIR})
	get_filename_component(PARENT_GENERATED_INCLUDE_DIR ${GENERATED_INCLUDE_DIR} DIRECTORY)
	target_include_directories(game PRIVATE ${PARENT_GENERATED_INCLUDE_DIR}) # for ncine_config.h
endif()
#if(NOT NCINE_DYNAMIC_LIBRARY)
#	target_include_directories(game PRIVATE ${NCINE_ROOT}/include/ncine)
#endif()
target_compile_definitions(game PRIVATE ${ANDROID_GENERATED_FLAGS})

# Export ANativeActivity_onCreate() - https://github.com/android-ndk/ndk/issues/381
set_property(TARGET game APPEND_STRING PROPERTY LINK_FLAGS "-u ANativeActivity_onCreate")

include(ncine_strip_binaries)
