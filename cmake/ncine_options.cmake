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
# LTO is also disabled on Sega Dreamcast: GCC 15.2 sh-elf crashes with an internal compiler error
# (gen_reg_rtx) when streaming some units back in during the LTO link
cmake_dependent_option(NCINE_LINKTIME_OPTIMIZATION "Compile the game with link-time optimization when in release" ON "NOT NCINE_BUILD_ANDROID;NOT PLATFORM_DREAMCAST" OFF)
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

# Libretro core: shared library driven by the frontend, no window backend
option(NCINE_BUILD_LIBRETRO "Build as a libretro core (shared library)" OFF)
if(NCINE_BUILD_LIBRETRO)
	# Rendering backend (RHI) of the libretro core: "Software" presents the CPU rasterizer's
	# framebuffer through retro_video_refresh (works everywhere); "OpenGL" renders on the GPU into
	# the frontend's FBO via SET_HW_RENDER. The other backends cannot present through the libretro
	# API, so the generic RHI selection below is skipped for this build.
	set(NCINE_PREFERRED_RHI "Software" CACHE STRING "Rendering backend for the libretro core: Software or OpenGL")
	set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "Software;OpenGL")
	if(NOT NCINE_PREFERRED_RHI MATCHES "^(Software|OpenGL)$")
		message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" for the libretro core (expected Software or OpenGL)")
	endif()
	if(NCINE_PREFERRED_RHI STREQUAL "OpenGL")
		# The hardware core targets OpenGL|ES 3.0 only - the common denominator of RetroArch's
		# GPU platforms (KMS/GLES boards like Recalbox on Pi, desktop Mesa via EGL)
		set(_NCINE_RHI_GL_PROFILE_FORCED "ES3")
	endif()
endif()

if(NOT NCINE_BUILD_ANDROID AND NOT WINDOWS_PHONE AND NOT WINDOWS_STORE AND NOT NCINE_BUILD_LIBRETRO)
	if(NINTENDO_SWITCH OR VITA)
		set(_NCINE_DEFAULT_BACKEND "SDL2")
	else()
		set(_NCINE_DEFAULT_BACKEND "GLFW")
	endif()
	set(NCINE_PREFERRED_BACKEND ${_NCINE_DEFAULT_BACKEND} CACHE STRING "Specify preferred core backend")
	set_property(CACHE NCINE_PREFERRED_BACKEND PROPERTY STRINGS "GLFW;SDL2;SDL3")

	if(NINTENDO_WII OR NINTENDO_GAMECUBE)
		# Nintendo Wii/GameCube (the devkitPro toolchain files set NINTENDO_WII/NINTENDO_GAMECUBE): the only
		# rendering backend is the fixed-function GX one (Flipper/Hollywood), presented through the bespoke
		# Ogc window backend (no SDL/GLFW on these consoles in this engine).
		set(NCINE_PREFERRED_RHI "GX" CACHE STRING "Rendering backend on Nintendo Wii/GameCube: GX")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "GX")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "GX")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on Nintendo Wii/GameCube (expected GX)")
		endif()
	elseif(PLATFORM_DREAMCAST)
		# Sega Dreamcast (the KallistiOS toolchain file sets PLATFORM_DREAMCAST): the only rendering backend
		# is the fixed-function PowerVR one, presented through the bespoke Dc window backend.
		set(NCINE_PREFERRED_RHI "PVR" CACHE STRING "Rendering backend on Sega Dreamcast: PVR")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "PVR")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "PVR")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on Sega Dreamcast (expected PVR)")
		endif()
	elseif(PLATFORM_PS2)
		# PlayStation 2 (the ps2dev.cmake toolchain file sets PLATFORM_PS2): the only rendering backend is
		# the fixed-function GS one, driven by writing GIF packets to the Graphics Synthesizer directly,
		# presented through the bespoke Ps2 window backend. gsKit ships with the toolchain and is layered on
		# the same GIF interface, so it could only cost performance. The GS has no programmable shading at
		# all, so this is a fixed-function backend in the same sense as PVR/GX/GU and consumes the same
		# transpiled `fixed_function` effect tables.
		set(NCINE_PREFERRED_RHI "GS" CACHE STRING "Rendering backend on PlayStation 2: GS")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "GS")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "GS")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on PlayStation 2 (expected GS)")
		endif()
	elseif(PLATFORM_PSP)
		# PlayStation Portable (the pspdev toolchain file sets PLATFORM_PSP/PSP): the only rendering backend
		# is the fixed-function GU one (the Allegrex GE, driven through sceGu), presented through the bespoke
		# Psp window backend. There is an OpenGL|ES wrapper on the platform (libGL/pspgl), but it is layered
		# on the very same GE this backend drives directly, so it could only cost performance.
		set(NCINE_PREFERRED_RHI "GU" CACHE STRING "Rendering backend on PlayStation Portable: GU")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "GU")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "GU")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on PlayStation Portable (expected GU)")
		endif()
	elseif(VITA)
		# PS Vita (the VitaSDK toolchain sets VITA): three backends are possible. "GXM" drives the console's
		# own graphics API (sceGxm) directly, which is the same idea as the PVR/GX/GU backends on the older
		# consoles - except that the Vita's PowerVR SGX is a fully programmable part, so GXM is a SHADER
		# backend and keeps the whole post-processing chain; it only removes the OpenGL translation layer.
		# That makes it the default. "OpenGL" runs through vitaGL, an OpenGL|ES 2.0 implementation layered on
		# that very same sceGxm, so selecting it pins NCINE_RHI_GL_PROFILE to ES2 below. Note that the
		# "libshacccg.suprx" firmware module is a requirement of the *platform* rather than of either backend:
		# both compile their shaders on the console through SceShaccCg (GXM the generated Cg for sceGxm,
		# vitaGL the GLSL handed to glCompileShader), so neither runs without it. The CPU software renderer is
		# the third alternative. Direct3D 11 and Vulkan do not exist on the platform. The window backend is
		# always SDL2 on Vita.
		set(NCINE_PREFERRED_RHI "GXM" CACHE STRING "Rendering backend on PS Vita: GXM (native sceGxm), OpenGL (ES 2.0 via vitaGL) or Software")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "GXM;OpenGL;Software")

		if(NCINE_PREFERRED_RHI STREQUAL "OpenGL")
			# vitaGL IS an OpenGL|ES 2.0 implementation, so the ES2 profile is not a choice here
			set(_NCINE_RHI_GL_PROFILE_FORCED "ES2")
		elseif(NOT NCINE_PREFERRED_RHI MATCHES "^(GXM|Software)$")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on PS Vita (expected GXM, OpenGL or Software)")
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
elseif(PLATFORM_PSP)
	# PlayStation Portable: pspdev does ship a pthreads implementation (pthread-embedded on top of the
	# firmware's own threads), but the engine's threading layer wants more than the bare create/join -
	# thread names, affinity and priorities, none of which map onto sceKernelCreateThread as they stand.
	# The game is single-threaded everywhere those are unavailable (see the console arms in Thread.cpp),
	# so it starts out that way here too rather than half-supported.
	# TODO: Revisit once the engine's console threading arms cover the PSP; the Allegrex has a second core
	# (the Media Engine) that a background asset loader could use.
	option(NCINE_WITH_THREADS "Enable support for threads" OFF)
elseif(PLATFORM_PS2)
	# PlayStation 2: PS2SDK ships libpthreadglue over the EE kernel's own threads and the engine links and
	# runs against it, but a thread created through it does not appear to be scheduled - the asynchronous
	# trace worker never drained its queue (DEATH_TRACE_ASYNC is excluded below for exactly that), and the
	# content-verification thread never sets IsVerified, which leaves the game parked in its loading state
	# with nothing to draw. Single-threaded is the honest configuration until that is understood.
	# TODO: Find out whether this needs an explicit priority/stack setup through the EE kernel calls that
	# pthreadglue does not expose, and revisit - the EE has cycles to spare for a background asset loader.
	option(NCINE_WITH_THREADS "Enable support for threads" OFF)
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
	endif()
endif()

# The Android host build never runs the platform selection above (it only drives the on-device build through
# Gradle), but it still has to decide the profile it passes down, and Android is a GL-only platform here
if(NCINE_BUILD_ANDROID AND NOT NCINE_PREFERRED_RHI)
	set(NCINE_PREFERRED_RHI "OpenGL")
endif()

# ── OpenGL family profile ─────────────────────────────────────────────────────────────────────────────
# Which profile of the OpenGL family the GL backend targets. This is the single switch that used to be two
# (an "is this a GLES build" flag plus a separate "strict ES 2.0" flag), and it drives everything downstream:
# which client library is imported (desktop GL / GLES2+EGL), which headers `CommonHeaders.h` includes, the
# context version `AppConfiguration` requests, and the shader dialect the GL backend consumes.
#
#   Core  OpenGL 3.3 core profile - desktop GL, GLSL 330, UBOs, gl_VertexID
#   ES3   OpenGL|ES 3.0 (and WebGL 2 on Emscripten) - ESSL 300 es, UBOs, gl_VertexID
#   ES2   OpenGL|ES 2.0 - ESSL 100, no UBOs, no gl_VertexID, no buffer mapping, no MRT
#
# Only the OpenGL backend has a profile; the other `NCINE_PREFERRED_RHI` values ignore this and the variable
# is not offered for them. Every profile is buildable on every platform that can provide the corresponding
# client library, so an ES2 (or ES3) build on desktop or Android is a normal configuration and not a special
# case - it is how the low-end paths get tested without the target hardware.
if(NCINE_PREFERRED_RHI STREQUAL "OpenGL")
	if(DEFINED _NCINE_RHI_GL_PROFILE_FORCED)
		# Platforms whose only GL implementation fixes the profile (PS Vita's vitaGL is ES 2.0, the
		# libretro core targets ES 3.0): not a choice, so it is set rather than offered.
		set(NCINE_RHI_GL_PROFILE "${_NCINE_RHI_GL_PROFILE_FORCED}")
	else()
		if(EMSCRIPTEN)
			# WebGL 2 is OpenGL|ES 3.0; WebGL 1 (ES2) is below what the pipeline needs from a browser
			set(_NCINE_RHI_GL_PROFILE_DEFAULT "ES3")
		elseif(ANDROID OR NCINE_BUILD_ANDROID OR NINTENDO_SWITCH OR NCINE_WITH_ANGLE)
			# GLES-only platforms, and ANGLE (which is an OpenGL|ES implementation on top of D3D/Vulkan)
			set(_NCINE_RHI_GL_PROFILE_DEFAULT "ES3")
		elseif(NCINE_ARM_PROCESSOR AND UNIX AND NOT APPLE)
			# ARM/aarch64 Linux boards: GLES is the implementation that is actually accelerated there
			set(_NCINE_RHI_GL_PROFILE_DEFAULT "ES3")
		else()
			set(_NCINE_RHI_GL_PROFILE_DEFAULT "Core")
		endif()
		set(NCINE_RHI_GL_PROFILE "${_NCINE_RHI_GL_PROFILE_DEFAULT}" CACHE STRING "OpenGL family profile: Core (OpenGL 3.3), ES3 (OpenGL|ES 3.0) or ES2 (OpenGL|ES 2.0)")
		set_property(CACHE NCINE_RHI_GL_PROFILE PROPERTY STRINGS "Core;ES3;ES2")

		if(NOT NCINE_RHI_GL_PROFILE MATCHES "^(Core|ES3|ES2)$")
			message(FATAL_ERROR "Invalid NCINE_RHI_GL_PROFILE \"${NCINE_RHI_GL_PROFILE}\" (expected Core, ES3 or ES2)")
		endif()

		# The GLES-only platforms above have no desktop GL to fall back to
		if(NCINE_RHI_GL_PROFILE STREQUAL "Core" AND (EMSCRIPTEN OR ANDROID OR NCINE_BUILD_ANDROID OR NINTENDO_SWITCH OR NCINE_WITH_ANGLE))
			message(FATAL_ERROR "NCINE_RHI_GL_PROFILE=Core is not available on this platform (no desktop OpenGL; use ES3 or ES2)")
		endif()
	endif()
endif()

# GLEW loads desktop OpenGL entry points, so it is only relevant to a Core profile build (on Windows it is
# also how the desktop GL functions beyond 1.1 are reached at all)
if(NCINE_RHI_GL_PROFILE STREQUAL "Core" AND NOT ANDROID AND NOT NCINE_BUILD_ANDROID AND NOT NINTENDO_SWITCH AND NOT EMSCRIPTEN)
	option(NCINE_WITH_GLEW "Use GLEW library" ON)
endif()

# ── 16-bit color surfaces ─────────────────────────────────────────────────────────────────────────────
# Trades color depth for memory and bandwidth: RGB565 instead of RGBA8, with no destination alpha (blend
# factors reading it see opaque). This is a performance option, so each backend applies it as far as it pays
# off there, which is not the same depth in both:
#
#   OpenGL   every color surface it can - a 5/6/5 default framebuffer is requested from the window system
#            AND the scene, blur and rescale render targets become RGB565 (see Texture::ColorTargetFormat),
#            which halves the bandwidth each post-processing pass reads and writes them with
#   Software  the screen buffer only, where it halves the per-frame upload to the present surface. Its
#            render targets stay 4-byte RGBA on purpose: the rasterizer's inner loops work on RGBA8 either
#            way, so packed targets would only add a pack/unpack per span and cost time rather than save it
#
# The console backends present through a format their hardware fixes - the Dreamcast's PowerVR is RGB565
# either way, GX copies out to a YUV XFB - so there is nothing to switch there, and a D3D11/Vulkan swap
# chain in 5/6/5 is not something drivers reliably offer.
cmake_dependent_option(NCINE_RHI_USE_FB16 "Use 16-bit (RGB565) color surfaces instead of RGBA8" OFF "NCINE_PREFERRED_RHI STREQUAL Software OR NCINE_PREFERRED_RHI STREQUAL OpenGL" OFF)

cmake_dependent_option(NCINE_WITH_BACKWARD "Enable integration with Backward library for exception handling" ON "(APPLE OR LINUX OR (WIN32 AND NOT WINDOWS_PHONE AND NOT WINDOWS_STORE)) AND NOT EMSCRIPTEN AND NOT NCINE_BUILD_ANDROID AND NOT VITA" OFF)
#option(NCINE_WITH_LZ4 "Enable LZ4 compression support" OFF)
#option(NCINE_WITH_ZSTD "Enable Zstd compression support" OFF)
option(NCINE_WITH_WEBP "Enable WebP image file support" OFF)
option(NCINE_WITH_AUDIO "Enable OpenAL support and thus sound" ON)
cmake_dependent_option(NCINE_WITH_VORBIS "Enable Ogg Vorbis audio file support" ON "NCINE_WITH_AUDIO" OFF)
if(PLATFORM_PSP)
	# PlayStation Portable: sound effects work (see NCINE_WITH_AUDIO above - pspdev ships an OpenAL Soft
	# whose output backend drives sceAudio), but the game's music is entirely tracker modules, and
	# libopenmpt is not usable on this CPU yet. It does build for the platform and it does play - the
	# soundtrack came out correctly in an emulator capture, and with the tuning in AudioLoaderMpt and
	# AudioStream the steady-state cost is 0.2 FPS - but the FIRST module a process loads costs a fixed
	# ~29 seconds inside the library (measured twice: 28.1 s when the intro loaded first, 29.8 s when the
	# menu did, independent of which module it was), and every load after that only ~1.8 s. Something in
	# there initialises once and does it at roughly 10000x the cost it has on a desktop, which points at
	# the Allegrex having no double-precision unit; until that is found there is nowhere to hide a
	# half-minute freeze on a handheld, so module music stays off.
	# TODO: Either find that initialisation, or pre-render the modules to a streamable form offline (the
	# AssetPacker already re-encodes cinematics for the Dreamcast, so it is the natural place for it).
	# libmodplug, which pspdev does package, is not an alternative: its J2B loader is compiled out for want
	# of zlib (`load_j2b.cpp.obj` in the archive is empty) so it cannot read one of the game's modules.
	set(NCINE_WITH_OPENMPT OFF)
elseif(PLATFORM_PS2)
	# PlayStation 2: PS2SDK packages no libopenmpt either, and compiling it from sources does not get through
	# the EE toolchain as it stands - `mpt/format/default_floatingpoint.hpp` calls `std::to_chars(char*, char*,
	# const double&)`, which is ambiguous against newlib's overload set on GCC 15. Module music is therefore
	# off for now, exactly as on the PSP and for a similarly external reason.
	# TODO: Either patch that overload (the library only needs the shortest round-trip form) or take the same
	# offline pre-render route the PSP note above proposes - the AssetPacker is the natural place for both.
	set(NCINE_WITH_OPENMPT OFF)
else()
	cmake_dependent_option(NCINE_WITH_OPENMPT "Enable module (libopenmpt) audio file support" ON "NCINE_WITH_AUDIO" OFF)
endif()
option(NCINE_WITH_ANGELSCRIPT "Enable AngelScript scripting support" OFF)
option(NCINE_WITH_IMGUI "Enable integration with Dear ImGui" OFF)
option(NCINE_WITH_TRACY "Enable integration with Tracy frame profiler" OFF)
#option(NCINE_WITH_RENDERDOC "Enable integration with RenderDoc" OFF)

cmake_dependent_option(NCINE_COMPILE_OPENMPT "Compile libopenmpt from sources instead of using library" OFF "NCINE_WITH_OPENMPT" OFF)
if(PLATFORM_PSP AND NCINE_WITH_OPENMPT)
	# pspdev packages no libopenmpt, and the fallback the other platforms take when the library is missing -
	# resolving it at run time - cannot work on a console that has no dynamic loader. So if module music is
	# turned back on here (see the note above), the library has to come from sources; it does cross-compile
	# and link for the platform as it stands.
	set(NCINE_COMPILE_OPENMPT ON)
endif()

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
	if(NCINE_GCC_HARDENING)
		# Required for `POSITION_INDEPENDENT_CODE` to also add the PIE link options to executables (CMP0083).
		# It has to be called in directory scope, so it can't be part of `ncine_apply_compiler_options()`.
		include(CheckPIESupported)
		check_pie_supported(OUTPUT_VARIABLE _pieSupportOutput LANGUAGES C CXX)
		if(NOT CMAKE_CXX_LINK_PIE_SUPPORTED)
			message(WARNING "Position independent executables are not supported: ${_pieSupportOutput}")
		endif()
	endif()
endif()

#set(NCINE_WITH_FIXED_BATCH_SIZE "0" CACHE PATH "Set custom fixed batch size (unsafe)")

# Shared library options
option(DEATH_DEBUG_SYMBOLS "Create debug symbols for executable" ${WIN32})
option(DEATH_TRACE "Enable runtime event tracing" ON)
# In the libretro core the async trace logger thread races with the frontend's core load/unload cycle
# PlayStation 2 is excluded for the same reason as the other consoles: the trace worker is a thread whose
# scheduling cannot be relied on there, and a log line that never leaves the queue is worse than a slow one -
# it makes the platform undebuggable. Synchronous tracing writes straight to the EE's serial port instead
# (see Application::OnTraceReceived).
cmake_dependent_option(DEATH_TRACE_ASYNC "Enable asynchronous processing of event tracing" ON "DEATH_TRACE;NCINE_WITH_THREADS;NOT VITA;NOT NINTENDO_WII;NOT NINTENDO_GAMECUBE;NOT PLATFORM_DREAMCAST;NOT PLATFORM_PSP;NOT PLATFORM_PS2;NOT NCINE_BUILD_LIBRETRO" OFF)
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
elseif(UNIX AND NOT APPLE AND NOT NCINE_BUILD_LIBRETRO)
	# IFUNC resolvers hang when the libretro core is dlopen'd by the frontend, so plain dispatch is used there
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
cmake_dependent_option(DISABLE_RESCALE_SHADERS "Disable all rescaling options" OFF "NOT NCINE_PREFERRED_RHI STREQUAL Software;NOT NCINE_PREFERRED_RHI STREQUAL GX;NOT NCINE_PREFERRED_RHI STREQUAL PVR;NOT NCINE_PREFERRED_RHI STREQUAL GU;NOT NCINE_PREFERRED_RHI STREQUAL GS;NOT VITA" ON)
# The single-draw tilemap mesh stays off where the backend cannot consume it: the software backend
# rasterizes per tile. The GX, PVR and GU backends all consume the whole-layer mesh directly (see
# GxDevice::DispatchTileMesh, PvrDevice::DispatchTileMesh and GuDevice::DispatchTileMesh), so they
# keep it on - on the GU it is also what lets a whole layer go out as one GE draw call
cmake_dependent_option(TILEMAP_USE_SINGLE_DRAW "Aggregate draw calls for each tilemap layer" ON "NOT NCINE_PREFERRED_RHI STREQUAL Software" OFF)

# Even the local (splitscreen) half of multiplayer is built on NetworkManagerBase, which owns an
# `nCine::Thread` unconditionally on every non-Emscripten platform, so the whole feature needs threads.
# This only ever excludes a target that has none at all (the PSP today) - every other platform defaults
# NCINE_WITH_THREADS to ON, and Emscripten reaches NetworkManagerBase through its own thread-free arm.
cmake_dependent_option(WITH_MULTIPLAYER "Enable multiplayer support" ON "NCINE_WITH_THREADS OR EMSCRIPTEN" OFF)
# The libogc consoles keep local splitscreen (WITH_MULTIPLAYER) but have no online transport (no enet/BSD
# sockets stack wired up) - the transport split keeps the engine+local path fully functional without it
# PS Vita is excluded for a different reason than the other consoles: it has threads and it has sockets, but
# the bundled ENet has no Vita arm - its POSIX branch includes <sys/ioctl.h>, which VitaSDK does not ship at
# all - so the transport cannot compile there as it stands. Local splitscreen (WITH_MULTIPLAYER) is unaffected.
# TODO: Give ENet a Vita arm (sceNet has the equivalent of the ioctl the non-blocking setup needs) and drop
# the exclusion again.
# PlayStation 2 is excluded for the same shape of reason as the Vita: PS2SDK has a socket stack (ps2ip), but
# the bundled IXWebSocket includes <netinet/ip.h>, which PS2SDK does not ship - so the WebSocket transport
# cannot compile there as it stands. Local splitscreen (WITH_MULTIPLAYER) is unaffected.
cmake_dependent_option(WITH_ONLINE_MULTIPLAYER "Enable online multiplayer transport (requires WITH_MULTIPLAYER)" ON "WITH_MULTIPLAYER;NCINE_WITH_THREADS OR EMSCRIPTEN;NOT NINTENDO_WII;NOT NINTENDO_GAMECUBE;NOT PLATFORM_DREAMCAST;NOT PLATFORM_PSP;NOT PLATFORM_PS2;NOT VITA" OFF)
cmake_dependent_option(DEDICATED_SERVER "Build dedicated server only" OFF "WITH_ONLINE_MULTIPLAYER;NOT NCINE_BUILD_ANDROID;NOT EMSCRIPTEN;NOT NINTENDO_SWITCH;NOT WINDOWS_PHONE;NOT WINDOWS_STORE" OFF)
# IXWebSocket requires a full BSD sockets stack (e.g. <netinet/ip.h>), which the Nintendo Switch and
# PS Vita toolchains don't provide, so WebSocket transport is unavailable there (enet is still used).
cmake_dependent_option(WITH_WEBSOCKET "Enable WebSocket transport for multiplayer" ON "WITH_ONLINE_MULTIPLAYER;NOT EMSCRIPTEN;NOT NINTENDO_SWITCH;NOT VITA" OFF)
if(WITH_WEBSOCKET AND NOT EMSCRIPTEN)
	# Default to the OS-native TLS backend on Apple (SecureTransport, a system framework) to avoid depending on
	# a Homebrew OpenSSL whose architecture must match the build — the x86_64 cross-build on Apple Silicon runners
	# otherwise links the arm64-only Homebrew OpenSSL and fails with undefined symbols. Windows defaults to its
	# own native provider (Schannel) for the same reason, as it additionally needs no OpenSSL runtime libraries
	# next to the executable. This includes UWP, where every entry point it uses is exported by `WindowsApp`
	# (only the headers hide them behind the desktop API family, which `IXSocketSChannel.h` works around).
	if(APPLE)
		set(WITH_WEBSOCKET_TLS_BACKEND "SecureTransport" CACHE STRING "TLS backend for WebSocket transport (None, SecureTransport, Schannel, OpenSSL, mbedTLS)")
	elseif(WIN32)
		set(WITH_WEBSOCKET_TLS_BACKEND "Schannel" CACHE STRING "TLS backend for WebSocket transport (None, SecureTransport, Schannel, OpenSSL, mbedTLS)")
	else()
		set(WITH_WEBSOCKET_TLS_BACKEND "OpenSSL" CACHE STRING "TLS backend for WebSocket transport (None, SecureTransport, Schannel, OpenSSL, mbedTLS)")
	endif()
	set_property(CACHE WITH_WEBSOCKET_TLS_BACKEND PROPERTY STRINGS "None" "SecureTransport" "Schannel" "OpenSSL" "mbedTLS")
endif()

cmake_dependent_option(SHAREWARE_DEMO_ALLOW_MULTIPLAYER "Enable multiplayer support also in Shareware Demo" ON "SHAREWARE_DEMO_ONLY;WITH_ONLINE_MULTIPLAYER" OFF)