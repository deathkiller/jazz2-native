include(CMakeDependentOption)

# nCine options
cmake_dependent_option(NCINE_BUILD_ANDROID "Build Android version of the game" OFF "NOT EMSCRIPTEN;NOT NINTENDO_SWITCH" OFF)
option(NCINE_PROFILING "Enable runtime profiling" OFF)
cmake_dependent_option(NCINE_N64_SIZE_OPTIMIZATION "Build the cold parts of the Nintendo 64 port for size (-Os) to leave more RDRAM to the game" ON "PLATFORM_N64" OFF)
option(NCINE_DOWNLOAD_DEPENDENCIES "Download all build dependencies" ON)

# LTO goes through CMake's INTERPROCEDURAL_OPTIMIZATION, so static libraries get the LTO-aware archivers.
# Probed once here, before any dependency target, and turned off if the toolchain can't do it. Excluded:
#  - Dreamcast: GCC 15.2 sh-elf crashes (ICE in gen_reg_rtx) during the LTO link
#  - Nintendo 64: libdragon never tests LTO with its linker script and wrapped constructors
#  - Amiga PowerPC (MorphOS, AmigaOS 4): their GCCs can't produce a usable LTO archive
#  - iOS: CMake's IPO probe always builds against the device SDK, so it fails for simulator builds
cmake_dependent_option(NCINE_LINKTIME_OPTIMIZATION "Compile the game with link-time optimization when in release" ON "NOT NCINE_BUILD_ANDROID;NOT PLATFORM_DREAMCAST;NOT PLATFORM_N64;NOT PLATFORM_AMIGA;NOT PLATFORM_MORPHOS;NOT PLATFORM_AMIGAOS4;NOT IOS" OFF)
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
	# Only Software (through retro_video_refresh) and OpenGL (into the frontend's FBO) can present through libretro
	set(NCINE_PREFERRED_RHI "Software" CACHE STRING "Rendering backend for the libretro core: Software or OpenGL")
	set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "Software;OpenGL")
	if(NOT NCINE_PREFERRED_RHI MATCHES "^(Software|OpenGL)$")
		message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" for the libretro core (expected Software or OpenGL)")
	endif()
	if(NCINE_PREFERRED_RHI STREQUAL "OpenGL")
		# OpenGL|ES 3.0 is the common denominator of RetroArch's GPU platforms
		set(_NCINE_RHI_GL_PROFILE_FORCED "ES3")
	endif()
endif()

# ── Window/input backend family ───────────────────────────────────────────────────────────────────────
# Most platforms get their window, context and input from a library (GLFW, SDL2, SDL3), chosen by
# NCINE_PREFERRED_BACKEND. The consoles and the classic Amiga have a native backend instead, named by
# NCINE_NATIVE_WINDOW_BACKEND (empty elsewhere); the rest of the build only looks for a windowing library,
# desktop OpenGL or OpenAL where it is empty.
if(WINDOWS_PHONE OR WINDOWS_STORE)
	set(NCINE_NATIVE_WINDOW_BACKEND "Uwp")
	set(NCINE_PLATFORM_SUMMARY "Windows RT")
elseif(PLATFORM_N64)
	set(NCINE_NATIVE_WINDOW_BACKEND "N64")
	set(NCINE_PLATFORM_SUMMARY "Nintendo 64 (libdragon backend)")
elseif(NINTENDO_WII OR NINTENDO_GAMECUBE)
	set(NCINE_NATIVE_WINDOW_BACKEND "Ogc")
	set(NCINE_PLATFORM_SUMMARY "Nintendo Wii/GameCube (libogc backend)")
elseif(NINTENDO_3DS)
	set(NCINE_NATIVE_WINDOW_BACKEND "Ctr")
	set(NCINE_PLATFORM_SUMMARY "Nintendo 3DS (libctru backend)")
elseif(PLATFORM_DREAMCAST)
	set(NCINE_NATIVE_WINDOW_BACKEND "Dc")
	set(NCINE_PLATFORM_SUMMARY "Sega Dreamcast (KallistiOS backend)")
elseif(PLATFORM_PSP)
	set(NCINE_NATIVE_WINDOW_BACKEND "Psp")
	set(NCINE_PLATFORM_SUMMARY "PlayStation Portable (PSPSDK backend)")
elseif(PLATFORM_PS2)
	set(NCINE_NATIVE_WINDOW_BACKEND "Ps2")
	set(NCINE_PLATFORM_SUMMARY "PlayStation 2 (PS2SDK backend)")
elseif(PLATFORM_PS3)
	set(NCINE_NATIVE_WINDOW_BACKEND "Ps3")
	set(NCINE_PLATFORM_SUMMARY "PlayStation 3 (PSL1GHT backend)")
elseif(PLATFORM_AMIGA)
	# Only the classic 68k Amiga, the PowerPC ones (AmigaOS 4.1, MorphOS) use SDL2
	set(NCINE_NATIVE_WINDOW_BACKEND "Amiga")
	set(NCINE_PLATFORM_SUMMARY "Amiga (AmigaOS 3.x, Intuition/CyberGraphX backend)")
else()
	set(NCINE_NATIVE_WINDOW_BACKEND "")
endif()

if(NOT NCINE_BUILD_ANDROID AND NOT WINDOWS_PHONE AND NOT WINDOWS_STORE AND NOT NCINE_BUILD_LIBRETRO)
	# Only platforms with a windowing library can choose one
	if(NOT NCINE_NATIVE_WINDOW_BACKEND)
		if(NINTENDO_SWITCH OR VITA OR PLATFORM_AMIGAOS4 OR PLATFORM_MORPHOS OR IOS)
			set(_NCINE_DEFAULT_BACKEND "SDL2")
		else()
			set(_NCINE_DEFAULT_BACKEND "GLFW")
		endif()
		set(NCINE_PREFERRED_BACKEND ${_NCINE_DEFAULT_BACKEND} CACHE STRING "Specify preferred core backend")
		set_property(CACHE NCINE_PREFERRED_BACKEND PROPERTY STRINGS "GLFW;SDL2;SDL3")
	endif()

	if(IOS AND NOT NCINE_PREFERRED_BACKEND STREQUAL "SDL2")
		# GLFW has no iOS port and the SDL3 backend hasn't been built there
		message(FATAL_ERROR "Invalid NCINE_PREFERRED_BACKEND \"${NCINE_PREFERRED_BACKEND}\" for iOS (expected SDL2)")
	endif()

	if((PLATFORM_AMIGAOS4 OR PLATFORM_MORPHOS) AND NOT NCINE_PREFERRED_BACKEND STREQUAL "SDL2")
		# SDL2 is the only windowing library on the PowerPC Amigas
		message(FATAL_ERROR "Invalid NCINE_PREFERRED_BACKEND \"${NCINE_PREFERRED_BACKEND}\" for AmigaOS 4.1/MorphOS (expected SDL2)")
	endif()

	if(PLATFORM_MORPHOS)
		# TinyGL is a fixed-function OpenGL 1.x, driven by the LegacyGL backend (needs a supported 3D card);
		# Software is the fallback, also for emulators without 3D. See Docs/Amiga.dox.
		set(NCINE_PREFERRED_RHI "LegacyGL" CACHE STRING "Rendering backend on MorphOS: LegacyGL or Software")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "LegacyGL;Software")
		if(NOT NCINE_PREFERRED_RHI MATCHES "^(Software|LegacyGL)$")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on MorphOS (expected LegacyGL or Software)")
		endif()
	elseif(PLATFORM_AMIGAOS4)
		# MiniGL (fixed-function OpenGL 1.x over Warp3D) through LegacyGL, as on MorphOS; it has no FBOs, so render
		# targets are copied from the back buffer (see LegacyGlRenderTarget). Software is the fallback.
		set(NCINE_PREFERRED_RHI "LegacyGL" CACHE STRING "Rendering backend on AmigaOS 4: LegacyGL or Software")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "LegacyGL;Software")
		if(NOT NCINE_PREFERRED_RHI MATCHES "^(Software|LegacyGL)$")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on AmigaOS 4 (expected LegacyGL or Software)")
		endif()
	elseif(PLATFORM_AMIGA)
		# Classic Amiga has no texturing hardware, the software rasterizer presents into an RTG framebuffer
		set(NCINE_PREFERRED_RHI "Software" CACHE STRING "Rendering backend on Amiga: Software")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "Software")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "Software")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on Amiga (expected Software)")
		endif()
	elseif(PLATFORM_N64)
		# Fixed-function RDP through libdragon's rdpq (its OpenGL is layered on the same pipeline)
		set(NCINE_PREFERRED_RHI "RDP" CACHE STRING "Rendering backend on Nintendo 64: RDP")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "RDP")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "RDP")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on Nintendo 64 (expected RDP)")
		endif()
	elseif(NINTENDO_WII OR NINTENDO_GAMECUBE)
		# Fixed-function GX (Flipper/Hollywood)
		set(NCINE_PREFERRED_RHI "GX" CACHE STRING "Rendering backend on Nintendo Wii/GameCube: GX")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "GX")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "GX")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on Nintendo Wii/GameCube (expected GX)")
		endif()
	elseif(NINTENDO_3DS)
		# Fixed-function PICA200 through citro3d (programmable vertex stage only, a passthrough built by picasso)
		set(NCINE_PREFERRED_RHI "PICA" CACHE STRING "Rendering backend on Nintendo 3DS: PICA")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "PICA")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "PICA")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on Nintendo 3DS (expected PICA)")
		endif()
	elseif(PLATFORM_DREAMCAST)
		# Fixed-function PowerVR
		set(NCINE_PREFERRED_RHI "PVR" CACHE STRING "Rendering backend on Sega Dreamcast: PVR")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "PVR")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "PVR")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on Sega Dreamcast (expected PVR)")
		endif()
	elseif(PLATFORM_PS2)
		# Fixed-function GS, driven by GIF packets directly (gsKit is layered on the same interface)
		set(NCINE_PREFERRED_RHI "GS" CACHE STRING "Rendering backend on PlayStation 2: GS")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "GS")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "GS")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on PlayStation 2 (expected GS)")
		endif()
	elseif(PLATFORM_PSP)
		# Fixed-function GU through sceGu (pspgl is layered on the same GE)
		set(NCINE_PREFERRED_RHI "GU" CACHE STRING "Rendering backend on PlayStation Portable: GU")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "GU")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "GU")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on PlayStation Portable (expected GU)")
		endif()
	elseif(PLATFORM_PS3)
		# Programmable RSX through PSL1GHT, with shaders compiled offline by cgcomp (no runtime compiler)
		set(NCINE_PREFERRED_RHI "RSX" CACHE STRING "Rendering backend on PlayStation 3: RSX")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "RSX")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "RSX")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on PlayStation 3 (expected RSX)")
		endif()
	elseif(VITA)
		# GXM (native sceGxm, default), OpenGL (ES 2.0 through vitaGL) or Software; both GPU backends compile
		# shaders on the console, so libshacccg.suprx is required. The window backend is always SDL2.
		set(NCINE_PREFERRED_RHI "GXM" CACHE STRING "Rendering backend on PS Vita: GXM (native sceGxm), OpenGL (ES 2.0 via vitaGL) or Software")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "GXM;OpenGL;Software")

		if(NCINE_PREFERRED_RHI STREQUAL "OpenGL")
			set(_NCINE_RHI_GL_PROFILE_FORCED "ES2")
		elseif(NOT NCINE_PREFERRED_RHI MATCHES "^(GXM|Software)$")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on PS Vita (expected GXM, OpenGL or Software)")
		endif()
	else()
		# Desktop rendering backend, OpenGL by default (Metal on iOS). D3D11 needs Windows + MSVC, Metal needs Apple,
		# Vulkan loads its runtime dynamically (MoltenVK on macOS). LegacyGL (fixed-function, for MorphOS) is offered
		# here only to develop it. iOS offers Metal, OpenGL (ES 3.0) and Software.
		if(IOS)
			set(_NCINE_DEFAULT_RHI "Metal")
		else()
			set(_NCINE_DEFAULT_RHI "OpenGL")
		endif()
		set(NCINE_PREFERRED_RHI "${_NCINE_DEFAULT_RHI}" CACHE STRING "Rendering backend: OpenGL, LegacyGL, Software, D3D11, Vulkan, or Metal")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "OpenGL;LegacyGL;Software;D3D11;Vulkan;Metal")

		if(NCINE_PREFERRED_RHI STREQUAL "D3D11" AND NOT (WIN32 AND MSVC))
			message(FATAL_ERROR "NCINE_PREFERRED_RHI=D3D11 requires Windows with the MSVC toolchain")
		elseif(NCINE_PREFERRED_RHI STREQUAL "Metal" AND NOT APPLE)
			message(FATAL_ERROR "NCINE_PREFERRED_RHI=Metal requires macOS or iOS")
		elseif(IOS AND NOT NCINE_PREFERRED_RHI MATCHES "^(Metal|OpenGL|Software)$")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on iOS (expected Metal, OpenGL or Software)")
		elseif(NOT NCINE_PREFERRED_RHI MATCHES "^(OpenGL|LegacyGL|Software|D3D11|Vulkan|Metal)$")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" (expected OpenGL, LegacyGL, Software, D3D11, Vulkan, or Metal)")
		endif()

		# The non-OpenGL backends present through the SDL window, so force SDL2 unless SDL3 was chosen
		if(NOT NCINE_PREFERRED_RHI STREQUAL "OpenGL" AND NOT NCINE_PREFERRED_BACKEND MATCHES "^(SDL2|SDL3)$")
			set(NCINE_PREFERRED_BACKEND "SDL2" CACHE STRING "Specify preferred core backend" FORCE)
		endif()
	endif()
endif()

if(EMSCRIPTEN)
	option(NCINE_WITH_THREADS "Enable Emscripten Pthreads support" OFF)
elseif(PLATFORM_N64 OR PLATFORM_PS2 OR PLATFORM_PS3 OR PLATFORM_AMIGA OR PLATFORM_MORPHOS)
	# Single-threaded:
	#  - PS2: threads from libpthreadglue never get scheduled
	#  - PS3, N64: no pthreads in the SDK
	#  - Amiga: no std::thread in m68k-amigaos-gcc, and single-core machines anyway
	#  - MorphOS: <pthread.h> pulls exec's global `struct Task`, which clashes with the engine's Task<...>
	# TODO: PS2 and PS3 have cycles to spare for a background asset loader
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

			# D3D11 renders through UwpGfxDevice (no SDL2), OpenGL means ANGLE. Software and Vulkan aren't supported.
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

		# Packages install under "/usr", and the prefix is baked into the executable, so set it before it's used
		if((NCINE_ASSEMBLE_DEB OR NCINE_ASSEMBLE_RPM) AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
			set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install path prefix, prepended onto install directories" FORCE)
		endif()

		if(NCINE_ASSEMBLE_DEB OR NCINE_ASSEMBLE_RPM)
			string(TOLOWER "${NCINE_APP}" _NCINE_LINUX_PACKAGE_DEFAULT)
			set(NCINE_LINUX_PACKAGE "${_NCINE_LINUX_PACKAGE_DEFAULT}" CACHE STRING "Package name used for Linux install paths")
		endif()
	endif()
endif()

# The Android host build skips the selection above, but still passes the profile down, and Android is GL-only
if(NCINE_BUILD_ANDROID AND NOT NCINE_PREFERRED_RHI)
	set(NCINE_PREFERRED_RHI "OpenGL")
endif()

# ── OpenGL family profile ─────────────────────────────────────────────────────────────────────────────
# Only for the OpenGL backend; it selects the client library, headers, context version and shader dialect.
#   Core  OpenGL 3.3 core profile - desktop GL, GLSL 330, UBOs, gl_VertexID
#   ES3   OpenGL|ES 3.0 (and WebGL 2 on Emscripten) - ESSL 300 es, UBOs, gl_VertexID
#   ES2   OpenGL|ES 2.0 - ESSL 100, no UBOs, no gl_VertexID, no buffer mapping, no MRT
# Any profile with an available client library is a normal configuration, e.g. ES2 on desktop for testing.
if(NCINE_PREFERRED_RHI STREQUAL "OpenGL")
	if(DEFINED _NCINE_RHI_GL_PROFILE_FORCED)
		# Fixed by the platform's only GL implementation (vitaGL, the libretro core)
		set(NCINE_RHI_GL_PROFILE "${_NCINE_RHI_GL_PROFILE_FORCED}")
	else()
		if(EMSCRIPTEN)
			# WebGL 2, WebGL 1 is below what the pipeline needs
			set(_NCINE_RHI_GL_PROFILE_DEFAULT "ES3")
		elseif(ANDROID OR NCINE_BUILD_ANDROID OR NINTENDO_SWITCH OR IOS OR NCINE_WITH_ANGLE)
			# GLES-only platforms and ANGLE
			set(_NCINE_RHI_GL_PROFILE_DEFAULT "ES3")
		elseif(NCINE_ARM_PROCESSOR AND UNIX AND NOT APPLE)
			# GLES is what's accelerated on ARM Linux boards
			set(_NCINE_RHI_GL_PROFILE_DEFAULT "ES3")
		else()
			set(_NCINE_RHI_GL_PROFILE_DEFAULT "Core")
		endif()
		set(NCINE_RHI_GL_PROFILE "${_NCINE_RHI_GL_PROFILE_DEFAULT}" CACHE STRING "OpenGL family profile: Core (OpenGL 3.3), ES3 (OpenGL|ES 3.0) or ES2 (OpenGL|ES 2.0)")
		set_property(CACHE NCINE_RHI_GL_PROFILE PROPERTY STRINGS "Core;ES3;ES2")

		if(NOT NCINE_RHI_GL_PROFILE MATCHES "^(Core|ES3|ES2)$")
			message(FATAL_ERROR "Invalid NCINE_RHI_GL_PROFILE \"${NCINE_RHI_GL_PROFILE}\" (expected Core, ES3 or ES2)")
		endif()

		if(NCINE_RHI_GL_PROFILE STREQUAL "Core" AND (EMSCRIPTEN OR ANDROID OR NCINE_BUILD_ANDROID OR NINTENDO_SWITCH OR IOS OR NCINE_WITH_ANGLE))
			message(FATAL_ERROR "NCINE_RHI_GL_PROFILE=Core is not available on this platform (no desktop OpenGL; use ES3 or ES2)")
		endif()
		if(IOS AND NCINE_RHI_GL_PROFILE STREQUAL "ES2")
			# ES2 resolves its VAO entry points through EGL, which iOS doesn't have
			message(FATAL_ERROR "NCINE_RHI_GL_PROFILE=ES2 is not available on iOS (use ES3)")
		endif()
	endif()
endif()

# GLEW loads desktop OpenGL entry points, so only for the Core profile
if(NCINE_RHI_GL_PROFILE STREQUAL "Core" AND NOT ANDROID AND NOT NCINE_BUILD_ANDROID AND NOT NINTENDO_SWITCH AND NOT EMSCRIPTEN)
	option(NCINE_WITH_GLEW "Use GLEW library" ON)
endif()

# ── 16-bit color surfaces ─────────────────────────────────────────────────────────────────────────────
# RGB565 instead of RGBA8, without destination alpha. OpenGL uses it for the framebuffer and render targets,
# Software only for the screen buffer (its rasterizer works in RGBA8). Consoles have fixed formats.
cmake_dependent_option(NCINE_RHI_USE_FB16 "Use 16-bit (RGB565) color surfaces instead of RGBA8" OFF "NCINE_PREFERRED_RHI STREQUAL Software OR NCINE_PREFERRED_RHI STREQUAL OpenGL" OFF)
if(PLATFORM_AMIGA)
	# The Amiga present path only implements the native big-endian R5G6B5 layout
	set(NCINE_RHI_USE_FB16 ON)
endif()

cmake_dependent_option(NCINE_WITH_BACKWARD "Enable integration with Backward library for exception handling" ON "(APPLE OR LINUX OR (WIN32 AND NOT WINDOWS_PHONE AND NOT WINDOWS_STORE)) AND NOT IOS AND NOT EMSCRIPTEN AND NOT NCINE_BUILD_ANDROID AND NOT VITA" OFF)
# Only for consoles that get game data from the asset packer, which stores sprites and tilesets in LZ4
cmake_dependent_option(NCINE_WITH_LZ4 "Enable LZ4 compression support" ON "PLATFORM_N64 OR PLATFORM_DREAMCAST OR PLATFORM_PS2 OR NINTENDO_GAMECUBE" OFF)
#option(NCINE_WITH_ZSTD "Enable Zstd compression support" OFF)
option(NCINE_WITH_WEBP "Enable WebP image file support" OFF)
option(NCINE_WITH_AUDIO "Enable OpenAL support and thus sound" ON)
cmake_dependent_option(NCINE_WITH_VORBIS "Enable Ogg Vorbis audio file support" ON "NCINE_WITH_AUDIO" OFF)
# Music is tracker modules, played by either libopenmpt (accurate, heavy float mixer) or libxmp (integer mixer,
# 0.5-4 MB per module vs 4.6-12.5 MB, but no MO3 - 4 of 56 tracks are silent). libxmp is the default on:
#  - Wii, GameCube, Dreamcast: memory
#  - PS Vita, 3DS: CPU (and memory on the 3DS)
#  - PS2: libopenmpt doesn't compile there
#  - PSP, Amiga: see below
if(PLATFORM_AMIGA OR PLATFORM_PSP OR PLATFORM_PS2 OR PLATFORM_DREAMCAST OR NINTENDO_WII OR NINTENDO_GAMECUBE OR NINTENDO_3DS OR VITA)
	set(_ncineXmpDefault ON)
else()
	set(_ncineXmpDefault OFF)
endif()
cmake_dependent_option(NCINE_WITH_XMP "Enable module (libxmp) audio file support instead of libopenmpt" ${_ncineXmpDefault} "NCINE_WITH_AUDIO" OFF)

if(NCINE_WITH_XMP)
	# Never both decoders
	set(NCINE_WITH_OPENMPT OFF)
elseif(PLATFORM_N64 OR PLATFORM_PSP OR PLATFORM_PS2 OR PLATFORM_AMIGA)
	# libopenmpt can't be used here, so without libxmp there's no music:
	#  - PSP: the first module takes ~29 s to load (no double-precision unit)
	#  - PS2: doesn't compile (ambiguous std::to_chars in newlib on GCC 15)
	#  - N64: doesn't fit in 8 MB, and the ROM drops the music anyway
	#  - Amiga: far too slow for a 68060
	set(NCINE_WITH_OPENMPT OFF)
else()
	cmake_dependent_option(NCINE_WITH_OPENMPT "Enable module (libopenmpt) audio file support" ON "NCINE_WITH_AUDIO" OFF)
endif()

# Only where AngelScript has native calling conventions (Emscripten through native thunks), the scripting API isn't registered for the generic one
if(WIN32 OR APPLE OR ANDROID OR NCINE_BUILD_ANDROID OR EMSCRIPTEN OR NINTENDO_SWITCH OR VITA OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|aarch64|arm64|arm.*|riscv64)$"))
	set(_ncineAngelscriptDefault ON)
else()
	set(_ncineAngelscriptDefault OFF)
endif()
option(NCINE_WITH_ANGELSCRIPT "Enable AngelScript scripting support" ${_ncineAngelscriptDefault})
option(NCINE_WITH_IMGUI "Enable integration with Dear ImGui" OFF)
option(NCINE_WITH_TRACY "Enable integration with Tracy frame profiler" OFF)
#option(NCINE_WITH_RENDERDOC "Enable integration with RenderDoc" OFF)

cmake_dependent_option(NCINE_COMPILE_OPENMPT "Compile libopenmpt from sources instead of using library" OFF "NCINE_WITH_OPENMPT" OFF)
if((PLATFORM_PSP OR PLATFORM_PS3) AND NCINE_WITH_OPENMPT)
	# No packaged libopenmpt and no dynamic loader for the runtime fallback (PS3 also needs single-threaded mode)
	set(NCINE_COMPILE_OPENMPT ON)
endif()

set(NCINE_CONTENT_DIR "${CMAKE_SOURCE_DIR}/Content" CACHE PATH "Set path to the game data directory")
cmake_dependent_option(NCINE_CREATE_CONTENT_SYMLINK "Create symbolic link to the game data directory in target directory" OFF "(APPLE OR LINUX OR (WIN32 AND NOT WINDOWS_PHONE AND NOT WINDOWS_STORE)) AND NOT IOS AND NOT EMSCRIPTEN AND NOT NCINE_BUILD_ANDROID AND NOT VITA" OFF)

# ── iOS ───────────────────────────────────────────────────────────────────────────────────────────────
# Identification and signing of the bundle (see Docs/Building.dox); a personal team needs its own identifier
if(IOS)
	set(NCINE_IOS_BUNDLE_IDENTIFIER "${NCINE_REVERSE_DNS}" CACHE STRING "Bundle identifier of the iOS application")
	set(NCINE_IOS_DEVELOPMENT_TEAM "" CACHE STRING "Apple Developer team ID used to sign the iOS application (empty: unsigned, simulator only)")
	set(NCINE_IOS_CODESIGN_IDENTITY "" CACHE STRING "codesign identity for Ninja/Makefile device builds (e.g. \"Apple Development\"), Xcode signs on its own")
	set(NCINE_IOS_PROVISIONING_PROFILE "" CACHE FILEPATH "Provisioning profile (.mobileprovision) embedded into Ninja/Makefile device builds")
	mark_as_advanced(NCINE_IOS_CODESIGN_IDENTITY NCINE_IOS_PROVISIONING_PROFILE)
endif()

# ── GameCube ──────────────────────────────────────────────────────────────────────────────────────────
# Game ID of the disc image, declared everywhere because the host AssetPacker uses it as a default too
set(GAMECUBE_GAME_ID "GJJEJR" CACHE STRING "Game ID of the GameCube disc image, six characters")
if(NOT NINTENDO_GAMECUBE)
	mark_as_advanced(GAMECUBE_GAME_ID)
endif()

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
		# Needed for PIE link options (CMP0083), must be called in directory scope
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
# Synchronous where the trace is the only debugging channel (a queued line is lost on a crash), where thread
# scheduling can't be relied on, and in the libretro core (the worker races with core load/unload)
cmake_dependent_option(DEATH_TRACE_ASYNC "Enable asynchronous processing of event tracing" ON "DEATH_TRACE;NCINE_WITH_THREADS;NOT VITA;NOT PLATFORM_N64;NOT NINTENDO_WII;NOT NINTENDO_GAMECUBE;NOT PLATFORM_DREAMCAST;NOT PLATFORM_PSP;NOT NINTENDO_3DS;NOT PLATFORM_PS2;NOT NCINE_BUILD_LIBRETRO" OFF)
# A trace line per file operation; turn off to save the cost on consoles with synchronous tracing
cmake_dependent_option(DEATH_TRACE_VERBOSE_IO "Trace every file I/O operation" ON "DEATH_TRACE" OFF)
if(DEATH_TRACE)
	set(DEATH_TRACE_LOG_PATH "" CACHE PATH "Override path to trace log file if specified (and force writing traces to file on some platforms)")
endif()
# Off by default: it breaks isnan()/isinf() and has caused platform-specific differences before
option(DEATH_USE_FAST_MATH "Enable relaxed floating-point optimizations when in release" OFF)
option(DEATH_USE_RUNTIME_CAST "Enable runtime_cast<T>() optimization" ON)
cmake_dependent_option(DEATH_WITH_VC_LTL "Build with VC-LTL on Windows" ON "WIN32" OFF)

# IFUNC for CPU dispatch needs ELF, glibc (not musl), Android API 30+ and a capable toolchain, so it's tested
if(NCINE_BUILD_ANDROID)
	# Checked later against the NDK toolchain (see "/android/app/src/main/cpp/CMakeLists.txt")
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
		# IFUNC dispatchers segfault with --coverage on GCC 4.8
		if(NCINE_CODE_COVERAGE AND CMAKE_CXX_COMPILER_ID AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.9")
			if(NOT DEFINED DEATH_CPU_USE_IFUNC)
				message(WARNING "Disabling DEATH_CPU_USE_IFUNC by default as it may crash when used together with --coverage on GCC 4.8.")
			endif()
			set(_DEATH_CPU_USE_IFUNC_DEFAULT OFF)
		endif()

		# IFUNC dispatchers crash with sanitizers (https://github.com/google/sanitizers/issues/342)
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

# Runtime CPU dispatch, except on Emscripten where calls through a function pointer are too slow
if(EMSCRIPTEN)
	set(_DEATH_CPU_USE_RUNTIME_DISPATCH_DEFAULT OFF)
else()
	set(_DEATH_CPU_USE_RUNTIME_DISPATCH_DEFAULT ON)
endif()
option(DEATH_CPU_USE_RUNTIME_DISPATCH "Build with runtime dispatch for CPU-dependent functionality" ${_DEATH_CPU_USE_RUNTIME_DISPATCH_DEFAULT})

# Jazz² Resurrection options
option(SHAREWARE_DEMO_ONLY "Show only Shareware Demo episode" OFF)
cmake_dependent_option(DISABLE_RESCALE_SHADERS "Disable all rescaling options" OFF "NOT NCINE_PREFERRED_RHI STREQUAL Software;NOT NCINE_PREFERRED_RHI STREQUAL GX;NOT NCINE_PREFERRED_RHI STREQUAL PVR;NOT NCINE_PREFERRED_RHI STREQUAL GU;NOT NCINE_PREFERRED_RHI STREQUAL GS;NOT NCINE_PREFERRED_RHI STREQUAL RDP;NOT NCINE_PREFERRED_RHI STREQUAL PICA;NOT NCINE_PREFERRED_RHI STREQUAL LegacyGL;NOT VITA" ON)
# Off for Software, which rasterizes per tile
cmake_dependent_option(TILEMAP_USE_SINGLE_DRAW "Aggregate draw calls for each tilemap layer" ON "NOT NCINE_PREFERRED_RHI STREQUAL Software" OFF)

# Needs threads even for local splitscreen (NetworkManagerBase owns a thread), Emscripten has its own path
cmake_dependent_option(WITH_MULTIPLAYER "Enable multiplayer support" ON "NCINE_WITH_THREADS OR EMSCRIPTEN" OFF)
# Online transport (ENet) is unavailable on:
#  - libogc consoles, N64, Dreamcast: no sockets stack wired up
#  - PS2: the bundled IXWebSocket needs <netinet/ip.h>, which PS2SDK lacks
#  - AmigaOS 4, MorphOS: IPv4-only stacks (TODO: ENET_IPV6=0 as on the PSP, then only threads are missing)
# PSP, Vita and 3DS work with ENET_IPV6=0 and small shims (see ncine_extra_sources.cmake)
cmake_dependent_option(WITH_ONLINE_MULTIPLAYER "Enable online multiplayer transport (requires WITH_MULTIPLAYER)" ON "WITH_MULTIPLAYER;NCINE_WITH_THREADS OR EMSCRIPTEN;NOT PLATFORM_N64;NOT NINTENDO_WII;NOT NINTENDO_GAMECUBE;NOT PLATFORM_DREAMCAST;NOT PLATFORM_PS2;NOT PLATFORM_AMIGAOS4;NOT PLATFORM_MORPHOS" OFF)
cmake_dependent_option(DEDICATED_SERVER "Build dedicated server only" OFF "WITH_ONLINE_MULTIPLAYER;NOT NCINE_BUILD_ANDROID;NOT EMSCRIPTEN;NOT NINTENDO_SWITCH;NOT WINDOWS_PHONE;NOT WINDOWS_STORE" OFF)
# IXWebSocket needs <netinet/ip.h> and <poll.h>: no <netinet/ip.h> on Switch, Vita, 3DS and PSP, no <poll.h>
# on AmigaOS 4 and PSP (MorphOS is off through WITH_ONLINE_MULTIPLAYER). ENet carries online play there.
cmake_dependent_option(WITH_WEBSOCKET "Enable WebSocket transport for multiplayer" ON "WITH_ONLINE_MULTIPLAYER;NOT EMSCRIPTEN;NOT NINTENDO_SWITCH;NOT NINTENDO_3DS;NOT PLATFORM_PSP;NOT VITA;NOT PLATFORM_AMIGAOS4;NOT PLATFORM_MORPHOS" OFF)
if(WITH_WEBSOCKET AND NOT EMSCRIPTEN)
	# OS-native TLS on Apple (SecureTransport) and Windows (Schannel, also UWP), so no OpenSSL is needed there
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

# Built-in level editor (see Docs/Editor.dox), not where nothing can be saved (cartridge/disc-only consoles)
# or on a dedicated server. In the Shareware Demo, it opens only unlocked episodes (web: once one is imported).
cmake_dependent_option(WITH_EDITOR "Enable the built-in level editor" ON "NOT DEDICATED_SERVER;NOT PLATFORM_N64;NOT PLATFORM_DREAMCAST;NOT NINTENDO_GAMECUBE;NOT PLATFORM_PS2" OFF)

# Level packs (see Docs/LevelPacks.dox), only with an HTTP backend and zlib, checked in ncine_extra_sources.cmake
cmake_dependent_option(WITH_LEVEL_PACKS "Enable installing level packs" ON "NOT EMSCRIPTEN" OFF)

option(NCINE_BUILD_GAME "Build the game itself, turn off to build only the offline tools" ON)

# Offline tools (see `Sources/Utilities`) are host executables, so off when cross-compiling (except the web
# AssetPacker). Packaging, server and libretro builds don't need them by default.
if(DEDICATED_SERVER OR NCINE_BUILD_FLATPAK OR NCINE_BUILD_LIBRETRO)
	set(_ncineBuildOfflineTools OFF)
else()
	set(_ncineBuildOfflineTools ON)
endif()
if(NOT CMAKE_CROSSCOMPILING OR (EMSCRIPTEN AND NOT NCINE_BUILD_GAME))
	set(_ncineCanBuildAssetPacker ON)
else()
	set(_ncineCanBuildAssetPacker OFF)
endif()
cmake_dependent_option(NCINE_BUILD_ASSET_PACKER "Build the offline AssetPacker tool" ${_ncineBuildOfflineTools} "_ncineCanBuildAssetPacker" OFF)
cmake_dependent_option(NCINE_BUILD_SHADER_COMPILER "Build the offline ShaderCompiler tool" ${_ncineBuildOfflineTools} "NOT CMAKE_CROSSCOMPILING" OFF)
unset(_ncineBuildOfflineTools)
unset(_ncineCanBuildAssetPacker)
if(NOT NCINE_BUILD_GAME AND NOT NCINE_BUILD_ASSET_PACKER AND NOT NCINE_BUILD_SHADER_COMPILER)
	message(WARNING "Nothing to build: the game is turned off (NCINE_BUILD_GAME) and so are both offline tools")
endif()

# Movement trajectory probe for comparing against the original game (see `Sources/Jazz2/Tests`), developers only
option(WITH_PHYSICS_PROBE "Build the movement trajectory probe (see Sources/Jazz2/Tests)" OFF)
