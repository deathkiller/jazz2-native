include(CMakeDependentOption)

# nCine options
cmake_dependent_option(NCINE_BUILD_ANDROID "Build Android version of the game" OFF "NOT EMSCRIPTEN;NOT NINTENDO_SWITCH" OFF)
option(NCINE_PROFILING "Enable runtime profiling" OFF)
option(NCINE_DOWNLOAD_DEPENDENCIES "Download all build dependencies" ON)

# Targets opt into interprocedural optimization (LTO/LTCG) through the INTERPROCEDURAL_OPTIMIZATION
# property in `ncine_apply_compiler_options()`. Letting CMake drive LTO (instead of passing `-flto`/`/GL`
# by hand) also makes it use the LTO-aware archiver wrappers (gcc-ar/gcc-ranlib, llvm-ar/llvm-ranlib) for
# static libraries, which fixes the "archive has no index; run ranlib to add one" link error. Probe once
# here (before any dependency targets in `ncine_imported_targets.cmake`) and just turn the option off if
# the toolchain can't do it.
cmake_dependent_option(NCINE_LINKTIME_OPTIMIZATION "Compile the game with link-time optimization when in release" ON "NOT NCINE_BUILD_ANDROID" OFF)
if(NCINE_LINKTIME_OPTIMIZATION)
	include(CheckIPOSupported)
	check_ipo_supported(RESULT _ipoSupported OUTPUT _ipoOutput)
	if(NOT _ipoSupported)
		message(STATUS "Link-time optimization is not supported by the toolchain, disabling it: ${_ipoOutput}")
		set(NCINE_LINKTIME_OPTIMIZATION OFF)
	endif()
endif()

option(NCINE_AUTOVECTORIZATION_REPORT "Enable report generation from compiler auto-vectorization" OFF)
option(NCINE_STRIP_BINARIES "Enable symbols stripping from libraries and executables when in release" OFF)
option(NCINE_VERSION_FROM_GIT "Try to set current game version from GIT repository" ON)
#cmake_dependent_option(NCINE_DYNAMIC_LIBRARY "Compile the engine as a dynamic library" OFF "NOT EMSCRIPTEN" OFF)

if(NOT NCINE_BUILD_ANDROID AND NOT WINDOWS_PHONE AND NOT WINDOWS_STORE)
	if(NINTENDO_SWITCH OR VITA)
		set(_NCINE_DEFAULT_BACKEND "SDL2")
	else()
		set(_NCINE_DEFAULT_BACKEND "GLFW")
	endif()
	set(NCINE_PREFERRED_BACKEND ${_NCINE_DEFAULT_BACKEND} CACHE STRING "Specify preferred core backend")
	set_property(CACHE NCINE_PREFERRED_BACKEND PROPERTY STRINGS "GLFW;SDL2;SDL3")

	if(VITA)
		# PS Vita (the VitaSDK toolchain sets VITA): the OpenGL family runs through vitaGL, which is an
		# OpenGL|ES 2.0 implementation - selecting "OpenGL" therefore force-enables the GLES path and the
		# strict ES 2.0 profile below. The CPU software renderer is the only alternative. Direct3D 11 and
		# Vulkan do not exist on the platform. The window backend is always SDL2 on Vita.
		set(NCINE_PREFERRED_RHI "OpenGL" CACHE STRING "Rendering backend on PS Vita: OpenGL (ES 2.0 via vitaGL) or Software")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "OpenGL;Software")

		if(NCINE_PREFERRED_RHI STREQUAL "OpenGL")
			set(NCINE_WITH_OPENGLES ON)
			set(_NCINE_RHI_GL_PROFILE_ES2_FORCE ON)
		elseif(NOT NCINE_PREFERRED_RHI STREQUAL "Software")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on PS Vita (expected OpenGL or Software)")
		endif()
	else()
		# Rendering backend (RHI) selection. OpenGL is the default; the software (CPU), Direct3D 11, and Vulkan
		# backends are mutually exclusive alternatives chosen through this single option. The non-OpenGL backends
		# present through the SDL2 window (software via SDL_Renderer, D3D11 via a DXGI swap chain, Vulkan via a
		# VkSwapchainKHR), so any of them forces the SDL2 window backend. Direct3D 11 requires Windows + MSVC;
		# Vulkan is header-only (Khronos Vulkan-Headers via FetchContent) with a dynamic vulkan-1.dll loader (no
		# Vulkan SDK). The rest of the build keys on this variable directly (NCINE_PREFERRED_RHI STREQUAL "...").
		set(NCINE_PREFERRED_RHI "OpenGL" CACHE STRING "Rendering backend: OpenGL, Software, D3D11, or Vulkan")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "OpenGL;Software;D3D11;Vulkan")

		if(NCINE_PREFERRED_RHI STREQUAL "D3D11" AND NOT (WIN32 AND MSVC))
			message(FATAL_ERROR "NCINE_PREFERRED_RHI=D3D11 requires Windows with the MSVC toolchain")
		elseif(NOT NCINE_PREFERRED_RHI MATCHES "^(OpenGL|Software|D3D11|Vulkan)$")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" (expected OpenGL, Software, D3D11, or Vulkan)")
		endif()

		# The non-OpenGL backends present through the SDL window, so force an SDL window backend.
		# An explicit SDL3 choice is honored; otherwise fall back to SDL2.
		if(NOT NCINE_PREFERRED_RHI STREQUAL "OpenGL" AND NOT NCINE_PREFERRED_BACKEND MATCHES "^(SDL2|SDL3)$")
			set(NCINE_PREFERRED_BACKEND "SDL2" CACHE STRING "Specify preferred core backend" FORCE)
		endif()
	endif()
endif()

if(EMSCRIPTEN)
	option(NCINE_WITH_THREADS "Enable Emscripten Pthreads support" OFF)
else()
	option(NCINE_WITH_THREADS "Enable support for threads" ON)

	if(NCINE_BUILD_ANDROID)
		set(NCINE_NDK_ARCHITECTURES "arm64-v8a" CACHE STRING "Set NDK target architectures")
		option(NCINE_ASSEMBLE_APK "Assemble Android APK with Gradle" ON)
		option(NCINE_UNIVERSAL_APK "Configure Gradle build script to assemble an universal APK for all ABIs" OFF)
		set(NDK_DIR "" CACHE PATH "Set path to Android NDK")
	elseif(MSVC)
		if(NCINE_ARM_PROCESSOR)
			set(NCINE_ARCH_EXTENSIONS "" CACHE STRING "Specifies architecture for code generation (armv8.0 - armv8.8)")
		else()
			set(NCINE_ARCH_EXTENSIONS "" CACHE STRING "Specifies architecture for code generation (IA32, SSE, SSE2, AVX, AVX2, AVX512)")   
		endif()

		if(WINDOWS_PHONE OR WINDOWS_STORE)
			set(NCINE_UWP_CERTIFICATE_THUMBPRINT "" CACHE STRING "Code-signing certificate thumbprint (Windows RT only)")
			set(NCINE_UWP_CERTIFICATE_PATH "" CACHE STRING "Code-signing certificate path (Windows RT only)")
			set(NCINE_UWP_CERTIFICATE_PASSWORD "" CACHE STRING "Code-signing certificate password (Windows RT only)")

			# Rendering backend (RHI) on UWP (Windows Store / Xbox): Direct3D 11 is the default. Unlike the
			# desktop D3D11 build it must not force the SDL2 backend: UWP renders through UwpGfxDevice, which
			# drives a DXGI flip-model swap chain from the CoreWindow (see UwpGfxDevice / D3D11Device).
			# Selecting "OpenGL" chooses the ANGLE (OpenGL|ES) renderer instead. Picking D3D11 demotes ANGLE to
			# an opt-in fallback (its libs are then not required); "OpenGL" restores ANGLE as the renderer so
			# UWP always has a working backend. (Software and Vulkan are not supported on UWP.)
			set(NCINE_PREFERRED_RHI "D3D11" CACHE STRING "Rendering backend on UWP: D3D11 or OpenGL (ANGLE)")
			set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "D3D11;OpenGL")
			if(NCINE_PREFERRED_RHI STREQUAL "D3D11")
				set(_NCINE_WITH_ANGLE_DEFAULT OFF)
			elseif(NCINE_PREFERRED_RHI STREQUAL "OpenGL")
				set(_NCINE_WITH_ANGLE_DEFAULT ON)
			else()
				message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on UWP (expected D3D11 or OpenGL)")
			endif()
		else()
			option(NCINE_INSTALL_SYSLIBS "Install required MSVC system libraries with CMake" OFF)
			option(NCINE_COPY_DEPENDENCIES "Copy all build dependencies to target directory" ON)
			set(_NCINE_WITH_ANGLE_DEFAULT OFF)
		endif()
		option(NCINE_WITH_ANGLE "Enable Google ANGLE library support" ${_NCINE_WITH_ANGLE_DEFAULT})
	elseif(UNIX AND NOT APPLE AND NOT ANDROID AND NOT NINTENDO_SWITCH)
		set(NCINE_ARCH_EXTENSIONS "" CACHE STRING "Specifies architecture for code generation (or \"native\" for current CPU)") 
		option(NCINE_BUILD_FLATPAK "Build Flatpak version of the game" OFF)
		cmake_dependent_option(NCINE_ASSEMBLE_DEB "Assemble DEB package of the game" OFF "NOT NCINE_BUILD_FLATPAK" OFF)
		cmake_dependent_option(NCINE_ASSEMBLE_RPM "Assemble RPM package of the game" OFF "NOT NCINE_BUILD_FLATPAK" OFF)
		# Prefer OpenGL|ES on ARM/aarch64 (embedded-oriented default), OpenGL elsewhere
		if(NCINE_ARM_PROCESSOR)
			option(NCINE_WITH_OPENGLES "Use OpenGL|ES 2 library instead of OpenGL" ON)
		else()
			option(NCINE_WITH_OPENGLES "Use OpenGL|ES 2 library instead of OpenGL" OFF)
		endif()
	endif()
	
	if((WIN32 OR NOT NCINE_WITH_OPENGLES) AND NOT ANDROID AND NOT NCINE_BUILD_ANDROID AND NOT NINTENDO_SWITCH)
		option(NCINE_WITH_GLEW "Use GLEW library" ON)
	endif()
endif()

# OpenGL|ES 2.0 profile of the GL backend. Requests a real ES 2.0 context (ESSL 100 shaders, no uniform
# buffer objects, no gl_VertexID) instead of the ES 3.0 context the ANGLE/GLES path otherwise uses. This
# targets the PS Vita (ES 2.0). It defaults ON when building against ANGLE for desktop testing and is only
# available on OpenGL|ES builds - it is force-OFF (and thus must never affect) the desktop GL 3.3 and the
# software (NCINE_PREFERRED_RHI=Software) builds.
if(_NCINE_RHI_GL_PROFILE_ES2_FORCE)
	# PS Vita: vitaGL IS an OpenGL|ES 2.0 implementation, so the strict profile is not optional there
	set(NCINE_RHI_GL_PROFILE_ES2 ON)
else()
	if(NCINE_WITH_ANGLE)
		set(_NCINE_RHI_GL_PROFILE_ES2_DEFAULT ON)
	else()
		set(_NCINE_RHI_GL_PROFILE_ES2_DEFAULT OFF)
	endif()
	cmake_dependent_option(NCINE_RHI_GL_PROFILE_ES2 "Request OpenGL|ES 2.0 profile (ESSL 100, no UBOs, no gl_VertexID)" ${_NCINE_RHI_GL_PROFILE_ES2_DEFAULT} "NCINE_WITH_ANGLE OR NCINE_WITH_OPENGLES" OFF)
endif()

cmake_dependent_option(NCINE_WITH_BACKWARD "Enable integration with Backward library for exception handling" ON "(APPLE OR LINUX OR (WIN32 AND NOT WINDOWS_PHONE AND NOT WINDOWS_STORE)) AND NOT EMSCRIPTEN AND NOT NCINE_BUILD_ANDROID AND NOT VITA" OFF)
#option(NCINE_WITH_LZ4 "Enable LZ4 compression support" OFF)
#option(NCINE_WITH_ZSTD "Enable Zstd compression support" OFF)
option(NCINE_WITH_WEBP "Enable WebP image file support" OFF)
option(NCINE_WITH_AUDIO "Enable OpenAL support and thus sound" ON)
cmake_dependent_option(NCINE_WITH_VORBIS "Enable Ogg Vorbis audio file support" ON "NCINE_WITH_AUDIO" OFF)
cmake_dependent_option(NCINE_WITH_OPENMPT "Enable module (libopenmpt) audio file support" ON "NCINE_WITH_AUDIO" OFF)
option(NCINE_WITH_ANGELSCRIPT "Enable AngelScript scripting support" OFF)
option(NCINE_WITH_IMGUI "Enable integration with Dear ImGui" OFF)
option(NCINE_WITH_TRACY "Enable integration with Tracy frame profiler" OFF)
#option(NCINE_WITH_RENDERDOC "Enable integration with RenderDoc" OFF)

cmake_dependent_option(NCINE_COMPILE_OPENMPT "Compile libopenmpt from sources instead of using library" OFF "NCINE_WITH_OPENMPT" OFF)

set(NCINE_CONTENT_DIR "${CMAKE_SOURCE_DIR}/Content" CACHE PATH "Set path to the game data directory")
cmake_dependent_option(NCINE_CREATE_CONTENT_SYMLINK "Create symbolic link to the game data directory in target directory" OFF "(APPLE OR LINUX OR (WIN32 AND NOT WINDOWS_PHONE AND NOT WINDOWS_STORE)) AND NOT EMSCRIPTEN AND NOT NCINE_BUILD_ANDROID AND NOT VITA" OFF)

if(NCINE_WITH_RENDERDOC)
	set(RENDERDOC_DIR "" CACHE PATH "Set path to RenderDoc directory")
endif()

option(NCINE_ADDRESS_SANITIZER "Enable AddressSanitizer memory error detector" OFF)
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	option(NCINE_UNDEFINED_SANITIZER "Enable UndefinedBehaviorSanitizer detector of GCC and Clang" OFF)
	option(NCINE_THREAD_SANITIZER "Enable the ThreadSanitizer detector of GCC and Clang" OFF)
	option(NCINE_CODE_COVERAGE "Enable gcov instrumentation for testing code coverage" OFF)
endif()
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	option(NCINE_GCC_HARDENING "Enable memory corruption mitigation methods of GCC" OFF)
endif()

#set(NCINE_WITH_FIXED_BATCH_SIZE "0" CACHE PATH "Set custom fixed batch size (unsafe)")

# Shared library options
option(DEATH_DEBUG_SYMBOLS "Create debug symbols for executable" ${WIN32})
option(DEATH_TRACE "Enable runtime event tracing" ON)
cmake_dependent_option(DEATH_TRACE_ASYNC "Enable asynchronous processing of event tracing" ON "DEATH_TRACE;NCINE_WITH_THREADS;NOT VITA" OFF)
if(DEATH_TRACE)
	set(DEATH_TRACE_LOG_PATH "" CACHE PATH "Override path to trace log file if specified (and force writing traces to file on some platforms)")
endif()
option(DEATH_USE_RUNTIME_CAST "Enable runtime_cast<T>() optimization" ON)
cmake_dependent_option(DEATH_WITH_VC_LTL "Build with VC-LTL on Windows" ON "WIN32" OFF)

# Check if we can use IFUNC for CPU dispatch. Linux with glibc and Android with API 18+ has it,
# but e.g. Alpine Linux with musl doesn't, and on Android with API < 30 we don't get AT_HWCAP passed
# into the resolver and can't call getauxval() ourselves because it's too early at that point,
# which makes it pretty useless. Plus it also needs a certain binutils version and a capable compiler,
# so it's easiest to just verify the whole thing. The feature is supported on ELF platforms only,
# so general Linux/BSD but not Apple.
if(NCINE_BUILD_ANDROID)
	# Support is checked later against Android NDK toolchain (see "/android/app/src/main/cpp/CMakeLists.txt").
	set(_DEATH_CPU_CAN_USE_IFUNC ON)
	set(_DEATH_CPU_USE_IFUNC_DEFAULT ON)
elseif(UNIX AND NOT APPLE)
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
		set(_DEATH_CPU_USE_IFUNC_DEFAULT ON)
		# On GCC 4.8, if --coverage or -fprofile-arcs is enabled, the ifunc dispatchers cause a segfault.
		# On Ubuntu 20.04 at least. Not the case with GCC 5 there, not the case with GCC 4.8 on Arch.
		# Can't find any upstream bug report or commit that would be related to this.
		if(NCINE_CODE_COVERAGE AND CMAKE_CXX_COMPILER_ID AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.9")
			if(NOT DEFINED DEATH_CPU_USE_IFUNC)
				message(WARNING "Disabling DEATH_CPU_USE_IFUNC by default as it may crash when used together with --coverage on GCC 4.8.")
			endif()
			set(_DEATH_CPU_USE_IFUNC_DEFAULT OFF)
		endif()

		# If sanitizers are enabled, call into the dispatch function crashes. Upstream bugreport
		# https://github.com/google/sanitizers/issues/342 suggests using __attribute__((no_sanitize_address)),
		# but that doesn't work / can't be used because it would mean marking basically everything including
		# the actual implementation that's being dispatched to.
		if(NCINE_ADDRESS_SANITIZER OR NCINE_UNDEFINED_SANITIZER)
			if(NOT DEFINED DEATH_CPU_USE_IFUNC)
				message(WARNING "Disabling DEATH_CPU_USE_IFUNC by default as it crashes when used together with sanitizers. See https://github.com/google/sanitizers/issues/342 for more information.")
			endif()
			set(_DEATH_CPU_USE_IFUNC_DEFAULT OFF)
		endif()
	else()
		set(_DEATH_CPU_USE_IFUNC_DEFAULT OFF)
	endif()
else()
	set(_DEATH_CPU_CAN_USE_IFUNC OFF)
	set(_DEATH_CPU_USE_IFUNC_DEFAULT OFF)
endif()
cmake_dependent_option(DEATH_CPU_USE_IFUNC "Allow using GNU IFUNC for runtime CPU dispatch" ${_DEATH_CPU_USE_IFUNC_DEFAULT} "_DEATH_CPU_CAN_USE_IFUNC" OFF)

# Runtime CPU dispatch. Because going through a function pointer may have negative perf consequences,
# enable it by default only on platforms that have IFUNC, and thus can avoid the function pointer indirection.
option(DEATH_CPU_USE_RUNTIME_DISPATCH "Build with runtime dispatch for CPU-dependent functionality" ${_DEATH_CPU_CAN_USE_IFUNC})

# Jazz² Resurrection options
option(SHAREWARE_DEMO_ONLY "Show only Shareware Demo episode" OFF)
cmake_dependent_option(DISABLE_RESCALE_SHADERS "Disable all rescaling options" OFF "NOT NCINE_PREFERRED_RHI STREQUAL Software;NOT VITA" ON)
cmake_dependent_option(TILEMAP_USE_SINGLE_DRAW "Aggregate draw calls for each tilemap layer" ON "NOT NCINE_PREFERRED_RHI STREQUAL Software" OFF)

option(WITH_MULTIPLAYER "Enable multiplayer support" ON)
cmake_dependent_option(WITH_ONLINE_MULTIPLAYER "Enable online multiplayer transport (requires WITH_MULTIPLAYER)" ON "WITH_MULTIPLAYER;NCINE_WITH_THREADS OR EMSCRIPTEN" OFF)
cmake_dependent_option(DEDICATED_SERVER "Build dedicated server only" OFF "WITH_ONLINE_MULTIPLAYER;NOT NCINE_BUILD_ANDROID;NOT EMSCRIPTEN;NOT NINTENDO_SWITCH;NOT WINDOWS_PHONE;NOT WINDOWS_STORE" OFF)
# IXWebSocket requires a full BSD sockets stack (e.g. <netinet/ip.h>), which the Nintendo Switch and
# PS Vita toolchains don't provide, so WebSocket transport is unavailable there (enet is still used).
cmake_dependent_option(WITH_WEBSOCKET "Enable WebSocket transport for multiplayer" ON "WITH_ONLINE_MULTIPLAYER;NOT EMSCRIPTEN;NOT NINTENDO_SWITCH;NOT VITA" OFF)
if(WITH_WEBSOCKET AND NOT EMSCRIPTEN)
	# Default to the OS-native TLS backend on Apple (SecureTransport, a system framework) to avoid depending on
	# a Homebrew OpenSSL whose architecture must match the build — the x86_64 cross-build on Apple Silicon runners
	# otherwise links the arm64-only Homebrew OpenSSL and fails with undefined symbols.
	if(APPLE)
		set(WITH_WEBSOCKET_TLS_BACKEND "SecureTransport" CACHE STRING "TLS backend for WebSocket transport (None, SecureTransport, OpenSSL, mbedTLS)")
	else()
		set(WITH_WEBSOCKET_TLS_BACKEND "OpenSSL" CACHE STRING "TLS backend for WebSocket transport (None, SecureTransport, OpenSSL, mbedTLS)")
	endif()
	set_property(CACHE WITH_WEBSOCKET_TLS_BACKEND PROPERTY STRINGS "None" "SecureTransport" "OpenSSL" "mbedTLS")
endif()

cmake_dependent_option(SHAREWARE_DEMO_ALLOW_MULTIPLAYER "Enable multiplayer support also in Shareware Demo" ON "SHAREWARE_DEMO_ONLY;WITH_ONLINE_MULTIPLAYER" OFF)