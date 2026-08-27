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
# The Nintendo 64 is excluded conservatively: libdragon's own build never exercises LTO against its
# n64.ld linker script and --wrap'ed constructor sequencing, so it is the least-tested combination
# The two PowerPC Amiga targets join the list for a toolchain reason rather than a code-size one:
# neither GCC can produce a usable LTO archive here, so CMake's own IPO probe fails - on MorphOS with
# "-fno-fat-lto-objects are supported only with linker plugin", on AmigaOS 4 when it archives the probe
cmake_dependent_option(NCINE_LINKTIME_OPTIMIZATION "Compile the game with link-time optimization when in release" ON "NOT NCINE_BUILD_ANDROID;NOT PLATFORM_DREAMCAST;NOT PLATFORM_N64;NOT PLATFORM_AMIGA;NOT PLATFORM_MORPHOS;NOT PLATFORM_AMIGAOS4" OFF)
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

# ── Window/input backend family ───────────────────────────────────────────────────────────────────────
# There are two kinds of platform in this project. Most of them get their window, their graphics context
# and their input devices from a windowing library - GLFW, SDL2, SDL3 or Qt5 - which NCINE_PREFERRED_BACKEND
# below chooses between. The consoles and the classic Amiga have a backend of their own instead, because
# their SDKs either ship no such library at all, or ship one that is layered on the very interface the
# backend drives directly; the per-platform arms in ncine_extra_sources.cmake spell out each one's reason.
#
# NCINE_NATIVE_WINDOW_BACKEND names that backend and is empty on every platform that uses a library, which
# makes it the single answer the rest of the build keys on: a windowing library, a desktop OpenGL or an
# OpenAL is then looked for only where something could actually use one (see ncine_imported_targets.cmake),
# instead of every platform probing for all of them. NCINE_PLATFORM_SUMMARY is the label the configure
# prints for such a platform, so the list of them lives here and nowhere else.
if(WINDOWS_PHONE OR WINDOWS_STORE)
	set(NCINE_NATIVE_WINDOW_BACKEND "Uwp")
	set(NCINE_PLATFORM_SUMMARY "Windows RT")
elseif(PLATFORM_N64)
	set(NCINE_NATIVE_WINDOW_BACKEND "N64")
	set(NCINE_PLATFORM_SUMMARY "Nintendo 64 (libdragon backend)")
elseif(NINTENDO_WII OR NINTENDO_GAMECUBE)
	set(NCINE_NATIVE_WINDOW_BACKEND "Ogc")
	set(NCINE_PLATFORM_SUMMARY "Nintendo Wii/GameCube (libogc backend)")
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
	# Only the classic 68k Amiga: the PowerPC ones (AmigaOS 4.1, MorphOS) have a maintained SDL2 and use it
	set(NCINE_NATIVE_WINDOW_BACKEND "Amiga")
	set(NCINE_PLATFORM_SUMMARY "Amiga (AmigaOS 3.x, Intuition/CyberGraphX backend)")
else()
	set(NCINE_NATIVE_WINDOW_BACKEND "")
endif()

if(NOT NCINE_BUILD_ANDROID AND NOT WINDOWS_PHONE AND NOT WINDOWS_STORE AND NOT NCINE_BUILD_LIBRETRO)
	# Only platforms that take their window from a library get to choose which one; the rest have exactly
	# one backend, named above, and no fallback to offer
	if(NOT NCINE_NATIVE_WINDOW_BACKEND)
		if(NINTENDO_SWITCH OR VITA OR PLATFORM_AMIGAOS4 OR PLATFORM_MORPHOS)
			set(_NCINE_DEFAULT_BACKEND "SDL2")
		else()
			set(_NCINE_DEFAULT_BACKEND "GLFW")
		endif()
		set(NCINE_PREFERRED_BACKEND ${_NCINE_DEFAULT_BACKEND} CACHE STRING "Specify preferred core backend")
		set_property(CACHE NCINE_PREFERRED_BACKEND PROPERTY STRINGS "GLFW;SDL2;SDL3")
	endif()

	if((PLATFORM_AMIGAOS4 OR PLATFORM_MORPHOS) AND NOT NCINE_PREFERRED_BACKEND STREQUAL "SDL2")
		# SDL2 is the only one of them that exists on the PowerPC Amigas - there is no GLFW, no SDL3 and
		# no Qt5 - so an overridden value cannot be honoured and is rejected instead of falling back
		message(FATAL_ERROR "Invalid NCINE_PREFERRED_BACKEND \"${NCINE_PREFERRED_BACKEND}\" for AmigaOS 4.1/MorphOS (expected SDL2)")
	endif()

	if(PLATFORM_MORPHOS)
		# MorphOS (the project's own cmake/toolchains/morphos.cmake toolchain file sets PLATFORM_MORPHOS):
		# two backends are possible, and neither is the engine's shader path - MorphOS's 3D interface is
		# TinyGL, a fixed-function OpenGL 1.x with no shader support of any kind.
		#
		# "LegacyGL" drives that interface through the fixed-function GL backend, which is the same idea as
		# the PVR/GX/GU/GS/RDP console backends (it consumes the same transpiled `fixed_function` effect
		# tables) with OpenGL 1.3 texture combiners in place of each console's TEV. It needs a 3D card
		# tinygl.library supports - a Radeon on a PowerBook/Efika/Sam or a PCI Radeon/Voodoo/Permedia in a
		# Pegasos - and SDL2 creates the context for it.
		#
		# "Software" is the CPU rasterizer, and remains available for a machine whose graphics card TinyGL
		# does not support - and for emulation, where there is no 3D at all (QEMU's ATI card does not
		# implement the CCE DMA path tinygl.library drives), which is also why the GL path has not been
		# verified on this system yet. See Docs/Amiga.dox.
		#
		# A LegacyGL build does not start without tinygl.library: it is opened before main() by the SDK's
		# own initializer, which is what puts the call vectors in place (see MorphOSTinyGl.cpp).
		set(NCINE_PREFERRED_RHI "LegacyGL" CACHE STRING "Rendering backend on MorphOS: LegacyGL or Software")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "LegacyGL;Software")
		if(NOT NCINE_PREFERRED_RHI MATCHES "^(Software|LegacyGL)$")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on MorphOS (expected LegacyGL or Software)")
		endif()
	elseif(PLATFORM_AMIGAOS4)
		# AmigaOS 4.1 (the project's own cmake/toolchains/os4.cmake toolchain file sets PLATFORM_AMIGAOS4):
		# the same two backends as MorphOS, for the same reason - the hardware is a real Radeon and the
		# way to it that this SDK ships is MiniGL, a fixed-function OpenGL 1.x over Warp3D. So "LegacyGL"
		# drives it with the same backend TinyGL gets, and it is the default here as well; MiniGL
		# advertises GL_EXT_texture_env_combine, which is what that backend expresses its effects with.
		#
		# What MiniGL has no trace of is framebuffer objects, so a render target is drawn into the back
		# buffer and copied into its texture instead (see LegacyGlRenderTarget) - that path is compiled
		# in rather than probed here, because the entry points do not even exist to call.
		#
		# The other road to this hardware is Warp3D Nova's ogles2.library, which the engine's ES2 profile
		# would match (the same one the PS Vita's vitaGL build uses) - but this SDK ships no GLES2 headers
		# or link library for it, so that stays a separate project. "Software" remains available as the
		# fallback for a machine with no 3D driver.
		set(NCINE_PREFERRED_RHI "LegacyGL" CACHE STRING "Rendering backend on AmigaOS 4: LegacyGL or Software")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "LegacyGL;Software")
		if(NOT NCINE_PREFERRED_RHI MATCHES "^(Software|LegacyGL)$")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on AmigaOS 4 (expected LegacyGL or Software)")
		endif()
	elseif(PLATFORM_AMIGA)
		# Classic Amiga (the project's own cmake/toolchains/amiga.cmake toolchain file sets PLATFORM_AMIGA):
		# the only rendering backend is the CPU software rasterizer, presented through the bespoke Amiga
		# window backend into an RTG (Picasso96/CyberGraphX) chunky framebuffer. No Amiga this port can run
		# on has texturing or blending hardware - RTG cards accelerate at most blits and fills - so unlike
		# the fixed-function consoles there is no hardware pipeline to drive: the Software RHI, which the
		# desktop and Vita builds already ship, IS the renderer here (see Docs/AmigaPortDesign.md).
		set(NCINE_PREFERRED_RHI "Software" CACHE STRING "Rendering backend on Amiga: Software")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "Software")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "Software")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on Amiga (expected Software)")
		endif()
	elseif(PLATFORM_N64)
		# Nintendo 64 (the project's own cmake/toolchains/n64.cmake toolchain file sets PLATFORM_N64): the
		# only rendering backend is the fixed-function RDP one, driven through libdragon's rdpq command
		# queue, presented through the bespoke N64 window backend. libdragon's OpenGL 1.1 is layered on
		# that very same rdpq/RSP pipeline, so it could only cost performance - the same reasoning as
		# pspgl and gsKit above. The RDP has no programmable shading at all, so like PVR/GX/GU/GS this
		# backend consumes the transpiled `fixed_function` effect tables.
		set(NCINE_PREFERRED_RHI "RDP" CACHE STRING "Rendering backend on Nintendo 64: RDP")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "RDP")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "RDP")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on Nintendo 64 (expected RDP)")
		endif()
	elseif(NINTENDO_WII OR NINTENDO_GAMECUBE)
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
	elseif(PLATFORM_PS3)
		# PlayStation 3 (the project's own cmake/toolchains/ps3dev.cmake toolchain file sets PLATFORM_PS3):
		# the rendering backend is the native RSX one, driven by writing NV40 command packets into the GPU's
		# FIFO through PSL1GHT's librsx/libgcm_sys, presented through the bespoke Ps3 window backend.
		#
		# Note where this sits relative to the other consoles. The RSX is an NV47 - a fully PROGRAMMABLE part
		# with real vertex and fragment shaders - so despite being a console backend it belongs with GXM (and
		# with OpenGL/D3D11/Vulkan) rather than with the fixed-function PVR/GX/GU/GS tier: it advertises
		# RHI_CAP_SHADERS, runs the whole bloom / lighting / combine / rescale chain, and consumes no
		# `fixed_function` effect tables at all. What differs from GXM is only WHEN the shaders are compiled:
		# the Vita has SceShaccCg on the console, while the PS3 has no runtime shader compiler, so the same Cg
		# the emitter already produces is compiled offline by cgcomp into RSX microcode and embedded in the
		# executable - the arrangement the Vulkan backend uses for its SPIR-V.
		set(NCINE_PREFERRED_RHI "RSX" CACHE STRING "Rendering backend on PlayStation 3: RSX")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "RSX")
		if(NOT NCINE_PREFERRED_RHI STREQUAL "RSX")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" on PlayStation 3 (expected RSX)")
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
		# "LegacyGL" is the fixed-function OpenGL 1.x backend. Its target is MorphOS' TinyGL (see the arm
		# above); it is offered on the desktop because that is where it can be developed and looked at -
		# a desktop GL runs the same 1.3 combiner pipeline, so what renders wrongly here renders wrongly
		# there. It is not a sensible choice for an actual desktop build, where the OpenGL backend is
		# better in every respect.
		set(NCINE_PREFERRED_RHI "OpenGL" CACHE STRING "Rendering backend: OpenGL, LegacyGL, Software, D3D11, or Vulkan")
		set_property(CACHE NCINE_PREFERRED_RHI PROPERTY STRINGS "OpenGL;LegacyGL;Software;D3D11;Vulkan")

		if(NCINE_PREFERRED_RHI STREQUAL "D3D11" AND NOT (WIN32 AND MSVC))
			message(FATAL_ERROR "NCINE_PREFERRED_RHI=D3D11 requires Windows with the MSVC toolchain")
		elseif(NOT NCINE_PREFERRED_RHI MATCHES "^(OpenGL|LegacyGL|Software|D3D11|Vulkan)$")
			message(FATAL_ERROR "Invalid NCINE_PREFERRED_RHI \"${NCINE_PREFERRED_RHI}\" (expected OpenGL, LegacyGL, Software, D3D11, or Vulkan)")
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
elseif(PLATFORM_N64 OR PLATFORM_PS2 OR PLATFORM_PS3 OR PLATFORM_AMIGA OR PLATFORM_MORPHOS)
	# These consoles are single-threaded, each for its own reason, and none is a configuration change
	# away from working:
	#  - PS2: a thread created through PS2SDK's libpthreadglue never appears to be scheduled.
	#  - PS3: PSL1GHT ships no pthreads at all - newlib installs <pthread.h>, nothing implements it.
	#  - N64: libdragon ships no pthreads either (its cooperative kthread kernel is a different API),
	#    and a single 93 MHz core has nothing for a second thread to win anyway.
	#  - Amiga: bebbo's GCC has no gthreads/std::thread (AmigaOS Exec tasks are a different API), and
	#    every machine in this port's range is a single core anyway.
	#  - MorphOS: it HAS working pthreads, but its <pthread.h> includes <exec/semaphores.h>, which puts
	#    exec's `struct Task` in the global namespace - and that is also the name of the engine's
	#    coroutine type, so every one of the 264 unqualified `Task<...>` declarations in the game becomes
	#    ambiguous as soon as anything includes the header. Nothing else in the SDK does (SDL2's headers
	#    are clean), so the way out is to not use pthreads rather than to rename a type across the game.
	# TODO: The first two have cycles to spare for a background asset loader (the EE, and the Cell's
	# second PPE thread plus the SPEs).
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
if(PLATFORM_AMIGA)
	# The Amiga present path copies the screen buffer into an RTG bitmap whose native 16-bit format is
	# big-endian R5G6B5 - exactly what the native-endian FB16 buffer holds on the big-endian 68k - so
	# 16-bit is both the halved-bandwidth choice and the conversion-free one. Forced rather than
	# defaulted: the backend's present path only implements the FB16 layout.
	set(NCINE_RHI_USE_FB16 ON)
endif()

cmake_dependent_option(NCINE_WITH_BACKWARD "Enable integration with Backward library for exception handling" ON "(APPLE OR LINUX OR (WIN32 AND NOT WINDOWS_PHONE AND NOT WINDOWS_STORE)) AND NOT EMSCRIPTEN AND NOT NCINE_BUILD_ANDROID AND NOT VITA" OFF)
#option(NCINE_WITH_LZ4 "Enable LZ4 compression support" OFF)
#option(NCINE_WITH_ZSTD "Enable Zstd compression support" OFF)
option(NCINE_WITH_WEBP "Enable WebP image file support" OFF)
option(NCINE_WITH_AUDIO "Enable OpenAL support and thus sound" ON)
cmake_dependent_option(NCINE_WITH_VORBIS "Enable Ogg Vorbis audio file support" ON "NCINE_WITH_AUDIO" OFF)
# The game's music is entirely tracker modules, and there are two libraries here that can play them.
# They are alternatives, never both: whichever is enabled provides the module path (see the audio
# section of "ncine_extra_sources.cmake").
#
#  - libopenmpt is the reference decoder - every format the game ships, including the four ".mo3"
#    tracks, and the most accurate playback. It is also the heavy one: floating-point mixing written
#    for desktop CPUs.
#  - libxmp is an order of magnitude lighter. Its mixer is integer throughout (the only double-
#    precision arithmetic in it is the resonant-filter coefficient setup, run per filter change rather
#    than per sample), it is plain C89 that builds with every toolchain here, and its Galaxy Music
#    System loaders read the original ".j2b" modules directly. What it costs is coverage: it has no
#    MO3 support, so 4 of the 56 shipped tracks are silent wherever it is used, and its playback is
#    less exact on the edge cases libopenmpt is famous for getting right. Converting those four files
#    offline in the AssetPacker would remove that last difference.
#
# So NCINE_WITH_XMP is available everywhere - it is how a platform that cannot afford libopenmpt gets
# music at all, and it is a legitimate choice on any other target that would rather spend the CPU
# elsewhere. It defaults on exactly where libopenmpt cannot be used and the machine can still hold a
# module: a loaded module costs libxmp between 0.5 MB and 4 MB of RAM (measured over this game's
# tracks; the largest is "grabbag.it"), plus about 0.2 MB of player state.
# The Wii, the GameCube and the Dreamcast are on that list for memory rather than for CPU. Measured
# over this game's tracks, one loaded module costs libopenmpt 4.6-12.5 MB against libxmp's 0.5-4 MB -
# two to three times more - and the GameCube has 24 MB with no second pool to hide it in (the Wii
# shares the port and the content, so it follows the same choice). Neither SDK packages libopenmpt
# either, so that path also compiles the whole of it into a console binary.
# The Dreamcast is the same trade one size tighter: the heap it has to fit a module into is the window
# between the loaded ELF and the top of its 16 MB, about 13.8 MB (see Docs/Consoles.dox), and with a
# module holding up to 12.5 MB of that the larger levels ran the allocator out. libopenmpt does work
# there - it plays the whole soundtrack, and it has to be compiled from source like on the devkitPro
# consoles - but not at a size the game can afford next to a big level, which is what decides it.
#
# (Not the PlayStation 2, even though libxmp builds for its toolchain and the machine has the memory:
# that port has no audio backend at all yet, so there is nothing for a decoder to play through. When
# one appears, this is the line to add it to. Not the PS Vita either - VitaSDK packages libxmp, and
# turning this on there does pick the SDK's copy up, but that console runs libopenmpt comfortably and
# the fuller coverage is worth more than the CPU it saves.)
if(PLATFORM_AMIGA OR PLATFORM_PSP OR PLATFORM_DREAMCAST OR NINTENDO_WII OR NINTENDO_GAMECUBE)
	set(_ncineXmpDefault ON)
else()
	set(_ncineXmpDefault OFF)
endif()
cmake_dependent_option(NCINE_WITH_XMP "Enable module (libxmp) audio file support instead of libopenmpt" ${_ncineXmpDefault} "NCINE_WITH_AUDIO" OFF)

if(NCINE_WITH_XMP)
	# The two decoders serve the same purpose, so enabling libxmp turns libopenmpt off rather than
	# building both into the binary
	set(NCINE_WITH_OPENMPT OFF)
elseif(PLATFORM_N64 OR PLATFORM_PSP OR PLATFORM_PS2 OR PLATFORM_AMIGA)
	# Turning libxmp off on one of these does NOT bring libopenmpt back - it cannot be used on any of
	# them, each for a reason outside this project. They are left with sound effects and no soundtrack:
	#  - PSP: the library builds and plays correctly, but the FIRST module a process loads costs a fixed
	#    ~29 s inside it (every later one ~1.8 s), which points at the Allegrex having no
	#    double-precision unit. There is nowhere to hide that on a handheld.
	#  - PS2: it does not compile for the EE toolchain - `mpt/format/default_floatingpoint.hpp` calls
	#    `std::to_chars(char*, char*, const double&)`, ambiguous against newlib's overloads on GCC 15.
	#    libxmp does build there, but the console has no audio backend yet, so neither is of any use.
	#  - N64: a decoded module's runtime state plus the streaming buffers do not fit next to the game
	#    in 8 MB of RDRAM, so it is not even worth the code size. That is also why libxmp is not the
	#    default there despite being far lighter - its own per-module cost (up to 4 MB, see above) is
	#    most of the console's memory, and the ROM packaging drops the whole "Music" directory to stay
	#    inside the 64 MB cartridge ceiling anyway.
	#  - Amiga: libopenmpt's mixer is written for machines two orders of magnitude faster than a 68060;
	#    there is no port and no prospect of one being real-time on the classic tier.
	# The shared way out for all of them would be pre-rendering the modules offline - the AssetPacker
	# already re-encodes cinematics for the Dreamcast, so it is the natural place for it.
	# (The PS3 used to be on this list. It is not any more - see Findlibopenmpt.cmake, which puts the
	# library into its single-threaded mode so it never reaches the std::mutex PSL1GHT lacks.)
	set(NCINE_WITH_OPENMPT OFF)
else()
	cmake_dependent_option(NCINE_WITH_OPENMPT "Enable module (libopenmpt) audio file support" ON "NCINE_WITH_AUDIO" OFF)
endif()

option(NCINE_WITH_ANGELSCRIPT "Enable AngelScript scripting support" OFF)
option(NCINE_WITH_IMGUI "Enable integration with Dear ImGui" OFF)
option(NCINE_WITH_TRACY "Enable integration with Tracy frame profiler" OFF)
#option(NCINE_WITH_RENDERDOC "Enable integration with RenderDoc" OFF)

cmake_dependent_option(NCINE_COMPILE_OPENMPT "Compile libopenmpt from sources instead of using library" OFF "NCINE_WITH_OPENMPT" OFF)
if((PLATFORM_PSP OR PLATFORM_PS3) AND NCINE_WITH_OPENMPT)
	# Neither pspdev nor ps3dev packages libopenmpt, and the fallback the other platforms take when the
	# library is missing - resolving it at run time - cannot work on a console that has no dynamic loader.
	# So the library has to come from sources; it cross-compiles and links for both platforms as it stands
	# (the PS3 additionally needs the single-threaded switch, see Findlibopenmpt.cmake).
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
# The PSP is excluded for the second half of that reason alone - it does have working threads - because the
# trace stream is the only debugging channel the console has, and a queued line is a line that is not there
# after a crash.
cmake_dependent_option(DEATH_TRACE_ASYNC "Enable asynchronous processing of event tracing" ON "DEATH_TRACE;NCINE_WITH_THREADS;NOT VITA;NOT PLATFORM_N64;NOT NINTENDO_WII;NOT NINTENDO_GAMECUBE;NOT PLATFORM_DREAMCAST;NOT PLATFORM_PSP;NOT PLATFORM_PS2;NOT NCINE_BUILD_LIBRETRO" OFF)
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
cmake_dependent_option(DISABLE_RESCALE_SHADERS "Disable all rescaling options" OFF "NOT NCINE_PREFERRED_RHI STREQUAL Software;NOT NCINE_PREFERRED_RHI STREQUAL GX;NOT NCINE_PREFERRED_RHI STREQUAL PVR;NOT NCINE_PREFERRED_RHI STREQUAL GU;NOT NCINE_PREFERRED_RHI STREQUAL GS;NOT NCINE_PREFERRED_RHI STREQUAL RDP;NOT NCINE_PREFERRED_RHI STREQUAL LegacyGL;NOT VITA" ON)
# The single-draw tilemap mesh stays off where the backend cannot consume it: the software backend
# rasterizes per tile. The GX, PVR, GU and LegacyGL backends all consume the whole-layer mesh directly
# (see GxDevice::DispatchTileMesh and its counterparts), so they keep it on - on the GU it is also what
# lets a whole layer go out as one GE draw call
cmake_dependent_option(TILEMAP_USE_SINGLE_DRAW "Aggregate draw calls for each tilemap layer" ON "NOT NCINE_PREFERRED_RHI STREQUAL Software" OFF)

# Even the local (splitscreen) half of multiplayer is built on NetworkManagerBase, which owns an
# `nCine::Thread` unconditionally on every non-Emscripten platform, so the whole feature needs threads.
# Emscripten reaches NetworkManagerBase through its own thread-free arm instead.
cmake_dependent_option(WITH_MULTIPLAYER "Enable multiplayer support" ON "NCINE_WITH_THREADS OR EMSCRIPTEN" OFF)
# The libogc consoles keep local splitscreen (WITH_MULTIPLAYER) but have no online transport (no enet/BSD
# sockets stack wired up) - the transport split keeps the engine+local path fully functional without it
# PlayStation 2 is excluded because PS2SDK has a socket stack (ps2ip), but the bundled IXWebSocket includes
# <netinet/ip.h>, which PS2SDK does not ship - so the WebSocket transport cannot compile there as it stands.
# Local splitscreen (WITH_MULTIPLAYER) is unaffected.
# AmigaOS 4 and MorphOS are excluded for the same reason as each other: both bsdsocket stacks are
# IPv4-only - neither SDK has a <netinet/in6.h> or a `struct in6_addr` anywhere - while the bundled ENet
# is built around IPv6 addresses with IPv4-mapped ones inside them. Local splitscreen (WITH_MULTIPLAYER)
# is unaffected.
# TODO: ENET_IPV6=0 is what the PSP arm below uses to get past exactly that, so these two are no longer
# blocked on the address family - what is left for them is threads (see NCINE_WITH_THREADS above).
# Neither Sony handheld is excluded, although both stacks are IPv4-only: ENET_IPV6=0 covers the address
# family for them (see ncine_extra_sources.cmake), and what each one needs on top of that lives with the
# code it belongs to. The PSP has three pieces of the sockets API missing from pspdev's newlib, filled in
# inside the vendored enet.h - poll() over select(), getaddrinfo()/getnameinfo() over
# gethostbyname()/gethostbyaddr(). The Vita needs none of that; what it needs is nCine/Backends/Vita/
# VitaLibcCompat.cpp, because VitaSDK's inet_pton() answers -1 where POSIX says 0 and both libcurl and
# ENet read that as "yes, an address" (see the file). The <sys/ioctl.h> that used to be given as the Vita's
# blocker turned out to be an unused include - ENet only ever calls ioctl() in its _WIN32 arm.
# Both verified against a real ENet peer, under PPSSPP and Vita3K respectively.
cmake_dependent_option(WITH_ONLINE_MULTIPLAYER "Enable online multiplayer transport (requires WITH_MULTIPLAYER)" ON "WITH_MULTIPLAYER;NCINE_WITH_THREADS OR EMSCRIPTEN;NOT PLATFORM_N64;NOT NINTENDO_WII;NOT NINTENDO_GAMECUBE;NOT PLATFORM_DREAMCAST;NOT PLATFORM_PS2;NOT PLATFORM_AMIGAOS4;NOT PLATFORM_MORPHOS" OFF)
cmake_dependent_option(DEDICATED_SERVER "Build dedicated server only" OFF "WITH_ONLINE_MULTIPLAYER;NOT NCINE_BUILD_ANDROID;NOT EMSCRIPTEN;NOT NINTENDO_SWITCH;NOT WINDOWS_PHONE;NOT WINDOWS_STORE" OFF)
# IXWebSocket requires a full BSD sockets stack (e.g. <netinet/ip.h>), which the Nintendo Switch and
# PS Vita toolchains don't provide, so WebSocket transport is unavailable there (enet is still used, and
# on the Vita that is now the transport that carries online play rather than a fallback).
# AmigaOS 4 is excluded for one header rather than the stack: its bsdsocket headers cover everything
# else IXWebSocket wants, but there is no <poll.h> anywhere in the SDK - the OS offers select() and
# the library's own WaitSelect - and IXWebSocket includes it unconditionally. MorphOS does ship <poll.h>
# and is named here only to keep the exclusion explicit: this option follows WITH_ONLINE_MULTIPLAYER,
# which is already off there for the ENet reason above.
# The PSP is excluded for both of the reasons above at once: pspdev has neither <netinet/ip.h> nor a
# <poll.h> header (the shim that gets ENet past that is a private engine header, not something the
# library's own `#include <poll.h>` can find). The ENet transport carries online play there.
cmake_dependent_option(WITH_WEBSOCKET "Enable WebSocket transport for multiplayer" ON "WITH_ONLINE_MULTIPLAYER;NOT EMSCRIPTEN;NOT NINTENDO_SWITCH;NOT PLATFORM_PSP;NOT VITA;NOT PLATFORM_AMIGAOS4;NOT PLATFORM_MORPHOS" OFF)
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

# The two offline tools (see `Sources/Utilities`) are executables for the build machine, which is why both
# are forced off for every cross-compiled target. Beyond that they are never installed, never packaged and
# never invoked by the game's own build - the headers ShaderCompiler produces are committed to the
# repository - so a build whose only product is a package, a container image or a headless server compiles
# them just to throw them away. Those configurations default to off and can still ask for the tools
# explicitly; on a normal desktop build they stay on, because that is where they get used.
if(DEDICATED_SERVER OR NCINE_BUILD_FLATPAK OR NCINE_BUILD_LIBRETRO)
	set(_ncineBuildOfflineTools OFF)
else()
	set(_ncineBuildOfflineTools ON)
endif()
cmake_dependent_option(NCINE_BUILD_ASSET_PACKER "Build the offline AssetPacker tool" ${_ncineBuildOfflineTools} "NOT CMAKE_CROSSCOMPILING" OFF)
cmake_dependent_option(NCINE_BUILD_SHADER_COMPILER "Build the offline ShaderCompiler tool" ${_ncineBuildOfflineTools} "NOT CMAKE_CROSSCOMPILING" OFF)
unset(_ncineBuildOfflineTools)