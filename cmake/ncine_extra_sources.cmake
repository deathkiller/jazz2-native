target_include_directories(${NCINE_APP} PRIVATE
	"${NCINE_SOURCE_DIR}/Dependencies"
	"${NCINE_SOURCE_DIR}/Shared"
)

if(ATOMIC_FOUND)
	target_link_libraries(${NCINE_APP} PRIVATE Atomic::Atomic)
endif()

if(TARGET Backward)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_BACKWARD")
	target_link_libraries(${NCINE_APP} PRIVATE Backward)
endif()

# Client library and profile macros of the OpenGL family backend. Only this backend has a GL dependency at
# all: the software rasterizer draws on the CPU, the console backends talk to their own hardware API, and
# D3D11 / Vulkan bring their own (d3d11/dxgi/d3dcompiler, resp. a dynamically loaded vulkan-1). None of them
# must drag in a GL runtime - pspdev even ships a libGL (pspgl) wrapping the very GE the GU backend drives
# itself, which find_package(OpenGL) happily locates.
if(NCINE_PREFERRED_RHI STREQUAL "OpenGL")
	# Exactly one profile macro is defined, plus RHI_GL_PROFILE_ES for the two ES ones (they share the
	# client library, the EGL/GLES headers and everything but the context version and shader dialect)
	if(NCINE_RHI_GL_PROFILE STREQUAL "ES2")
		target_compile_definitions(${NCINE_APP} PRIVATE "RHI_GL_PROFILE_ES2" "RHI_GL_PROFILE_ES")
	elseif(NCINE_RHI_GL_PROFILE STREQUAL "ES3")
		target_compile_definitions(${NCINE_APP} PRIVATE "RHI_GL_PROFILE_ES3" "RHI_GL_PROFILE_ES")
	else()
		target_compile_definitions(${NCINE_APP} PRIVATE "RHI_GL_PROFILE_CORE")
	endif()

	if(NCINE_RHI_GL_PROFILE MATCHES "^ES")
		if(ANGLE_FOUND)
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_ANGLE")
		endif()

		if(VITA)
			# PS Vita renders through vitaGL, a static OpenGL|ES 2.0 implementation that is linked (together with
			# its Sce stub libraries) further below. find_package(OpenGLES2) cannot locate it - that module only
			# probes for GLES2/gl2.h and libGLESv2/libEGL.
		elseif(ANDROID OR EMSCRIPTEN)
			# Android links libGLESv2/libGLESv3 and libEGL from the NDK (see the Android bridge CMakeLists);
			# Emscripten's WebGL implementation is part of the runtime the linker provides itself
		elseif(TARGET OpenGLES2::GLES2)
			target_link_libraries(${NCINE_APP} PRIVATE EGL::EGL OpenGLES2::GLES2)
		else()
			message(FATAL_ERROR "NCINE_RHI_GL_PROFILE=${NCINE_RHI_GL_PROFILE} needs an OpenGL|ES client library (libGLESv2 + libEGL), which was not found. Install it, or build the Core profile against desktop OpenGL.")
		endif()

		# ETC1-compressed textures (".pkm") are an Android-only asset form, and the loader guards itself the
		# same way (see TextureLoaderPkm.h) - other ES builds used to compile it as dead code
		if(ANDROID OR NCINE_BUILD_ANDROID)
			list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPkm.h)
			list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPkm.cpp)
		endif()
	elseif(OPENGL_FOUND)
		if(TARGET OpenGL::OpenGL)
			target_link_libraries(${NCINE_APP} PRIVATE OpenGL::OpenGL)
		else()
			target_link_libraries(${NCINE_APP} PRIVATE OpenGL::GL)
		endif()
	elseif(NOT EMSCRIPTEN)
		message(FATAL_ERROR "NCINE_RHI_GL_PROFILE=Core needs a desktop OpenGL library, which was not found. Install it, or build an ES profile against OpenGL|ES.")
	endif()
endif()

if(GLEW_FOUND)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_GLEW")
	target_link_libraries(${NCINE_APP} PRIVATE GLEW::GLEW)
endif()

if(NCINE_PREFERRED_RHI STREQUAL "Software")
	# Selects the CPU software backend in RhiFwd.h/Rhi.h instead of the default OpenGL family backend
	message(STATUS "Rendering backend: Software (CPU rasterizer)")
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RHI_SOFTWARE")
elseif(NCINE_PREFERRED_RHI STREQUAL "GX")
	# Selects the Nintendo GameCube/Wii fixed-function GX backend in RhiFwd.h/Rhi.h
	message(STATUS "Rendering backend: GX (Nintendo GameCube/Wii)")
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RHI_GX")
elseif(NCINE_PREFERRED_RHI STREQUAL "PVR")
	# Selects the Sega Dreamcast fixed-function PowerVR backend in RhiFwd.h/Rhi.h - the KOS toolchain
	# environment links the PVR/maple libraries itself
	message(STATUS "Rendering backend: PowerVR (Sega Dreamcast)")
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RHI_PVR")
elseif(NCINE_PREFERRED_RHI STREQUAL "GS")
	# Selects the PlayStation 2 fixed-function Graphics Synthesizer backend in RhiFwd.h/Rhi.h. The GS is
	# driven by GIF packets built on the EE, so there is no graphics library to link - PS2SDK's kernel and
	# DMA libraries come in with the platform packaging below
	message(STATUS "Rendering backend: GS (PlayStation 2)")
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RHI_GS")
elseif(NCINE_PREFERRED_RHI STREQUAL "GU")
	# Selects the PlayStation Portable fixed-function GE backend in RhiFwd.h/Rhi.h - the PSPSDK graphics
	# libraries it calls into are linked with the platform packaging below
	message(STATUS "Rendering backend: GU (PlayStation Portable)")
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RHI_GU")
elseif(NCINE_PREFERRED_RHI STREQUAL "RDP")
	# Selects the Nintendo 64 fixed-function RDP backend in RhiFwd.h/Rhi.h. The RDP is driven through
	# libdragon's rdpq command queue (the RSP assembles and feeds the display list), so the graphics
	# stack is libdragon itself, linked with the platform packaging below
	message(STATUS "Rendering backend: RDP (Nintendo 64)")
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RHI_RDP")
elseif(NCINE_PREFERRED_RHI STREQUAL "GXM")
	# Selects the PS Vita's native sceGxm backend in RhiFwd.h/Rhi.h. Unlike the other console backends this
	# is a SHADER backend - the Vita's PowerVR SGX has no fixed-function pipeline - so it keeps the whole
	# post-processing chain; what it drops is the OpenGL translation layer (vitaGL) between the engine and
	# sceGxm. Its shaders are the baked Cg sources compiled on the console by SceShaccCg through vitashark
	# (the stubs are linked with the Vita packaging below, next to vitaGL's).
	message(STATUS "Rendering backend: GXM (PS Vita, native sceGxm)")
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RHI_GXM")
elseif(NCINE_PREFERRED_RHI STREQUAL "RSX")
	# Selects the PlayStation 3's native RSX backend in RhiFwd.h/Rhi.h. Like GXM above - and unlike the
	# fixed-function PVR/GX/GU/GS tier - the RSX is a programmable part (an NV47), so this is a full shader
	# backend that keeps the whole post-processing chain. Its shaders are the same Cg the emitter produces
	# for the Vita, but compiled to NV40 microcode OFFLINE by cgcomp (there is no runtime shader compiler on
	# the console) and embedded per program-variant, which is what the RSX shader generation below does.
	# PSL1GHT's librsx/libgcm_sys come in with the platform packaging further down.
	message(STATUS "Rendering backend: RSX (PlayStation 3, native libgcm)")
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RHI_RSX")
elseif(NCINE_PREFERRED_RHI STREQUAL "LegacyGL")
	# Selects the fixed-function OpenGL 1.x backend in RhiFwd.h/Rhi.h. Its client library is a plain
	# desktop GL where one exists, MiniGL on AmigaOS 4 and TinyGL on MorphOS - on both of those it has
	# to follow SDL2 on the link line, which is what the two arms below arrange.
	message(STATUS "Rendering backend: Legacy OpenGL (fixed-function 1.x)")
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RHI_LEGACYGL")
	if(PLATFORM_AMIGAOS4)
		# MiniGL, the SDK's fixed-function OpenGL over Warp3D. Its stub library has to follow SDL2 on the
		# link line (SDL2's OS4 video driver calls into it to create the context), which is what listing
		# it here rather than as an interface dependency of the imported target does - the SDL2 target is
		# already on the line by the time this is appended.
		target_link_libraries(${NCINE_APP} PRIVATE GL)
	elseif(PLATFORM_MORPHOS)
		# Asks for the SDK's TinyGL initializer by name. It is what opens tinygl.library into the
		# `TinyGLBase` the call macros read, but nothing references it: SDL2's static glue declares that
		# same global as a common symbol, which satisfies every reference without pulling the initializer's
		# object out of libGL.a - and the program would then call through a null library base. An
		# undefined symbol on the link line is what makes the linker take that object (see
		# Sources/nCine/Backends/MorphOS/MorphOSTinyGl.cpp).
		target_link_options(${NCINE_APP} PRIVATE "-u" "_CSTP_init_TinyGLBase")
	else()
		if(TARGET OpenGL::OpenGL)
			target_link_libraries(${NCINE_APP} PRIVATE OpenGL::OpenGL)
		elseif(TARGET OpenGL::GL)
			target_link_libraries(${NCINE_APP} PRIVATE OpenGL::GL)
		else()
			message(FATAL_ERROR "NCINE_PREFERRED_RHI=LegacyGL needs a desktop OpenGL library, which was not found")
		endif()
	endif()
elseif(NCINE_PREFERRED_RHI STREQUAL "D3D11")
	# Selects the Direct3D 11 backend in RhiFwd.h/Rhi.h instead of the default OpenGL family backend
	message(STATUS "Rendering backend: Direct3D 11")
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RHI_D3D11")
	# Direct3D 11 device/swap chain, DXGI and the HLSL compiler
	target_link_libraries(${NCINE_APP} PRIVATE d3d11 dxgi d3dcompiler)
elseif(NCINE_PREFERRED_RHI STREQUAL "Vulkan")
	# Selects the Vulkan backend in RhiFwd.h/Rhi.h instead of the default OpenGL family backend
	message(STATUS "Rendering backend: Vulkan")
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RHI_VULKAN")
	# Header-only Khronos Vulkan-Headers (fetched in ncine_imported_targets.cmake). No vulkan-1.lib is linked:
	# the loader binds the runtime vulkan-1.dll (shipped with GPU drivers) dynamically through SDL at startup.
	target_include_directories(${NCINE_APP} PRIVATE "${VULKAN_HEADERS_INCLUDE_DIR}")
else()
	# OpenGL/WebGL is the default rendering backend
	if(NCINE_RHI_GL_PROFILE STREQUAL "ES2")
		set(_NCINE_RHI_SUMMARY "OpenGL|ES 2.0 profile (ESSL 100, no UBOs)")
	elseif(NCINE_RHI_GL_PROFILE STREQUAL "ES3")
		set(_NCINE_RHI_SUMMARY "OpenGL|ES 3.0 profile")
	else()
		set(_NCINE_RHI_SUMMARY "OpenGL 3.3 core profile")
	endif()
	if(ANGLE_FOUND)
		string(APPEND _NCINE_RHI_SUMMARY ", ANGLE")
	elseif(EMSCRIPTEN)
		string(APPEND _NCINE_RHI_SUMMARY ", WebGL")
	elseif(VITA)
		string(APPEND _NCINE_RHI_SUMMARY ", vitaGL")
	endif()
	if(GLEW_FOUND)
		string(APPEND _NCINE_RHI_SUMMARY ", GLEW loader")
	elseif(TARGET OpenGL::OpenGL)
		string(APPEND _NCINE_RHI_SUMMARY ", GLVND")
	endif()
	message(STATUS "Rendering backend: ${_NCINE_RHI_SUMMARY}")

	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RHI_GL")
endif()

# Optional 16-bit (RGB565) screen framebuffer, honored by the software and OpenGL backends (see the option's
# comment in ncine_options.cmake). Defined after the backend selection so both read the same single macro.
if(NCINE_RHI_USE_FB16)
	message(STATUS "Screen framebuffer: RGB565 (16-bit)")
	target_compile_definitions(${NCINE_APP} PRIVATE "RHI_USE_FB16")
endif()

if(NOT DEDICATED_SERVER AND NOT NCINE_BUILD_LIBRETRO)
	if(PLATFORM_MORPHOS)
		# MorphOS reaches the display and the input devices through SDL2, so it has no backend of its own -
		# this is only the handful of library functions its C runtime declares but does not implement
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Backends/MorphOS/MorphOSLibcCompat.cpp)
		if(NCINE_PREFERRED_RHI STREQUAL "LegacyGL")
			# ... plus the two TinyGL globals an application has to fill in itself (see the file)
			list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Backends/MorphOS/MorphOSTinyGl.cpp)
		endif()
	endif()

	if(PLATFORM_AMIGA)
		# Intuition/CyberGraphX window/input backend (no SDL/GLFW: the 68k SDL2 ports are nascent, and the
		# whole job here is opening one RTG screen and copying the Software RHI's framebuffer into it - see
		# AmigaGfxDevice). The OS libraries (intuition, graphics, cybergraphics, lowlevel, ahi.device) are
		# opened at run time, so nothing is linked beyond what -mcrt already brings in.
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_AMIGA")
		list(APPEND HEADERS
			${NCINE_SOURCE_DIR}/nCine/Backends/Amiga/AmigaPlatform.h
			${NCINE_SOURCE_DIR}/nCine/Backends/Amiga/AmigaInputManager.h
			${NCINE_SOURCE_DIR}/nCine/Backends/Amiga/AmigaGfxDevice.h
		)
		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Backends/Amiga/AmigaPlatform.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/Amiga/AmigaInputManager.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/Amiga/AmigaGfxDevice.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/Amiga/AmigaLibcCompat.c
		)

		# AMMX scanline kernels for the Apollo 68080 (Vampire), runtime-gated so the binary stays
		# 68060-safe. binutils' assembler does not know AMMX, so the module is assembled by vasm,
		# which ships with the amiga-gcc toolchain ("make vasm").
		find_program(VASM_EXECUTABLE vasmm68k_mot HINTS "$ENV{AMIGA_INST}/bin")
		if(VASM_EXECUTABLE)
			# A subdirectory of its own, so the object cannot collide with anything else assembled into the
			# build root, and vasm itself is a dependency - a toolchain swap re-assembles the module
			set(_ammxObject "${CMAKE_CURRENT_BINARY_DIR}/Amiga/AmigaAmmxOps.o")
			add_custom_command(OUTPUT "${_ammxObject}"
				COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Amiga"
				COMMAND "${VASM_EXECUTABLE}" -quiet -m68080 -Fhunk
					-o "${_ammxObject}" "${NCINE_SOURCE_DIR}/nCine/Backends/Amiga/AmigaAmmxOps.s"
				DEPENDS "${NCINE_SOURCE_DIR}/nCine/Backends/Amiga/AmigaAmmxOps.s" "${VASM_EXECUTABLE}"
				COMMENT "Assembling AMMX scanline kernels (vasm)"
				VERBATIM)
			set_source_files_properties("${_ammxObject}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
			target_sources(${NCINE_APP} PRIVATE "${_ammxObject}")
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_AMMX")
			list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/RHI/Software/SwAmmxOps.h)
			message(STATUS "AMMX scanline kernels enabled (vasm: ${VASM_EXECUTABLE})")
		else()
			message(STATUS "vasm not found, AMMX scanline kernels will not be built")
		endif()
	elseif(PLATFORM_N64)
		# libdragon window/input backend (no SDL/GLFW: libdragon ships neither, and the console's video
		# output is a VI framebuffer configured through display_init rather than anything a windowing
		# library would wrap). The rdpq libraries the rendering backend calls into come in with libdragon
		# itself, linked with the platform packaging below.
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_N64")
		list(APPEND HEADERS
			${NCINE_SOURCE_DIR}/nCine/Backends/N64/N64InputManager.h
			${NCINE_SOURCE_DIR}/nCine/Backends/N64/N64GfxDevice.h
		)
		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Backends/N64/N64InputManager.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/N64/N64GfxDevice.cpp
		)
	elseif(NINTENDO_WII OR NINTENDO_GAMECUBE)
		# libogc window/input backend (no SDL/GLFW on these consoles); GX is linked by the
		# devkitPro toolchain's standard libraries (ogc), listed with the platform packaging below
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_OGC")
		list(APPEND HEADERS
			${NCINE_SOURCE_DIR}/nCine/Backends/Ogc/OgcInputManager.h
			${NCINE_SOURCE_DIR}/nCine/Backends/Ogc/OgcGfxDevice.h
		)
		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Backends/Ogc/OgcInputManager.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/Ogc/OgcGfxDevice.cpp
		)
		if(NINTENDO_WII)
			target_link_libraries(${NCINE_APP} PRIVATE wiiuse bte fat ogc m)
		else()
			target_link_libraries(${NCINE_APP} PRIVATE fat ogc m)
		endif()
	elseif(PLATFORM_DREAMCAST)
		# KallistiOS window/input backend (no SDL/GLFW); the KOS toolchain links its own libraries
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_DC")
		list(APPEND HEADERS
			${NCINE_SOURCE_DIR}/nCine/Backends/Dc/DcInputManager.h
			${NCINE_SOURCE_DIR}/nCine/Backends/Dc/DcGfxDevice.h
		)
		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Backends/Dc/DcInputManager.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/Dc/DcGfxDevice.cpp
		)
	elseif(PLATFORM_PSP)
		# PSPSDK window/input backend (no SDL/GLFW); the PSP libraries are linked with the packaging below
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_PSP")
		list(APPEND HEADERS
			${NCINE_SOURCE_DIR}/nCine/Backends/Psp/PspInputManager.h
			${NCINE_SOURCE_DIR}/nCine/Backends/Psp/PspGfxDevice.h
		)
		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Backends/Psp/PspInputManager.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/Psp/PspGfxDevice.cpp
		)
	elseif(PLATFORM_PS2)
		# PS2SDK window/input backend. PS2SDK does package an SDL2, and the configure would otherwise pick it,
		# but the shared SdlGfxDevice code passes `std::int32_t*` into SDL's `int*` parameters and
		# `std::int32_t` is `long` on the Emotion Engine - so it cannot compile there without changing
		# signatures every other platform depends on. A bespoke backend is the conventional answer on this
		# tier anyway (see the Dc/Ogc/Psp arms above). The PS2 libraries are linked with the packaging below.
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_PS2")

		list(APPEND HEADERS
			${NCINE_SOURCE_DIR}/nCine/Backends/Ps2/Ps2InputManager.h
			${NCINE_SOURCE_DIR}/nCine/Backends/Ps2/Ps2GfxDevice.h
		)
		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Backends/Ps2/Ps2InputManager.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/Ps2/Ps2GfxDevice.cpp
		)
		# The R5900 has no load-linked/store-conditional for sub-word sizes, so GCC lowers the engine's
		# `std::atomic<bool>`/`<std::uint8_t>` operations to libatomic calls rather than inline instructions
		# `mc` is libmc: the memory card is read and written through ordinary POSIX paths (MCMAN registers
		# with the same ioman the newlib port uses), but a slot has to be probed through libmc before MCMAN
		# will answer for it at all - see the mcGetInfo() call in MainApplication
		target_link_libraries(${NCINE_APP} PRIVATE draw graph dma packet pad mc cdvd kernel atomic)
	elseif(PLATFORM_PS3)
		# PSL1GHT window/input backend (no SDL/GLFW: PSL1GHT ships neither, and the console's video output is
		# configured through sysutil rather than through anything a windowing library would wrap). The RSX
		# libraries the rendering backend calls into are linked with the platform packaging below.
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_PS3")
		list(APPEND HEADERS
			${NCINE_SOURCE_DIR}/nCine/Backends/Ps3/Ps3InputManager.h
			${NCINE_SOURCE_DIR}/nCine/Backends/Ps3/Ps3GfxDevice.h
		)
		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Backends/Ps3/Ps3InputManager.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/Ps3/Ps3GfxDevice.cpp
		)
	elseif(GLFW_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "GLFW")
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_GLFW")
		target_link_libraries(${NCINE_APP} PRIVATE GLFW::GLFW)

		list(APPEND HEADERS
			${NCINE_SOURCE_DIR}/nCine/Backends/GlfwInputManager.h
			${NCINE_SOURCE_DIR}/nCine/Backends/GlfwGfxDevice.h
		)
		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Backends/GlfwInputManager.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/GlfwKeys.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/GlfwGfxDevice.cpp
		)
	elseif((SDL2_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "SDL2") OR (SDL3_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "SDL3"))
		# Both SDL versions share the same backend sources (SDL2/SDL3 API differences are handled inline
		# with WITH_SDL2 / WITH_SDL3 preprocessor forks), so only the compile definition and linked library differ
		if(NCINE_PREFERRED_BACKEND STREQUAL "SDL3")
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_SDL3")
			target_link_libraries(${NCINE_APP} PRIVATE SDL3::SDL3)
		else()
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_SDL2")
			target_link_libraries(${NCINE_APP} PRIVATE SDL2::SDL2)
		endif()

		list(APPEND HEADERS
			${NCINE_SOURCE_DIR}/nCine/Backends/SdlInputManager.h
			${NCINE_SOURCE_DIR}/nCine/Backends/SdlGfxDevice.h
		)
		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Backends/SdlInputManager.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/SdlKeys.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/SdlGfxDevice.cpp
		)
	elseif(Qt5_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "QT5")
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_QT5")
		target_link_libraries(${NCINE_APP} PUBLIC Qt5::Widgets)
		if(Qt5Gamepad_FOUND)
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_QT5GAMEPAD")
			target_link_libraries(${NCINE_APP} PRIVATE Qt5::Gamepad)
		endif()

		qt5_wrap_cpp(MOC_SOURCES ${NCINE_SOURCE_DIR}/nCine/Backends/Qt5Widget.h)

		list(APPEND HEADERS
			${NCINE_SOURCE_DIR}/nCine/Backends/Qt5Widget.h
			${NCINE_SOURCE_DIR}/nCine/Backends/Qt5InputManager.h
			${NCINE_SOURCE_DIR}/nCine/Backends/Qt5GfxDevice.h
		)
		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Backends/Qt5Widget.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/Qt5InputManager.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/Qt5Keys.cpp
			${NCINE_SOURCE_DIR}/nCine/Backends/Qt5GfxDevice.cpp
			${MOC_SOURCES}
		)
	endif()
endif()

if(NOT DEDICATED_SERVER)
	if(OPENAL_FOUND OR ASND_FOUND OR AICA_FOUND OR N64AUDIO_FOUND OR PS3AUDIO_FOUND OR AHIAUDIO_FOUND OR SDLAUDIO_FOUND)
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_AUDIO")

		list(APPEND HEADERS
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioDeviceBase.h
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioMixerCommon.h
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioBufferPlayer.h
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioStreamPlayer.h
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderWav.h
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderWav.h
		)

		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioDeviceBase.cpp
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioBufferPlayer.cpp
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioStreamPlayer.cpp
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderWav.cpp
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderWav.cpp
		)

		# Exactly one audio backend is compiled into a binary, mirroring the rendering backends. Each
		# one lives in "nCine/Audio/Backends/<backend>/" and implements IAudioDevice.
		if(OPENAL_FOUND)
			set(_NCINE_AUDIO_BACKEND "OpenAL")
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_OPENAL")
			target_link_libraries(${NCINE_APP} PRIVATE OpenAL::OpenAL)

			list(APPEND HEADERS
				${NCINE_SOURCE_DIR}/nCine/Audio/Backends/AL/ALAudioDevice.h
				${NCINE_SOURCE_DIR}/nCine/Audio/Backends/AL/ALDebug.h)
			list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/AL/ALAudioDevice.cpp)
		elseif(ASND_FOUND)
			set(_NCINE_AUDIO_BACKEND "ASND (libogc DSP mixer)")
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_ASND")
			target_link_libraries(${NCINE_APP} PRIVATE asnd)

			list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/ASND/AsndAudioDevice.h)
			list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/ASND/AsndAudioDevice.cpp)
		elseif(AICA_FOUND)
			set(_NCINE_AUDIO_BACKEND "AICA (KallistiOS sound driver)")
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_AICA")

			list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/AICA/AicaAudioDevice.h)
			list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/AICA/AicaAudioDevice.cpp)
		elseif(N64AUDIO_FOUND)
			set(_NCINE_AUDIO_BACKEND "N64 (libdragon AI DMA ring mixer)")
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_N64AUDIO")
			# The AI is part of libdragon itself, which is linked with the platform packaging below

			list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/N64/N64AudioDevice.h)
			list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/N64/N64AudioDevice.cpp)
		elseif(AHIAUDIO_FOUND)
			set(_NCINE_AUDIO_BACKEND "AHI (ahi.device software mixer)")
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_AHIAUDIO")
			# ahi.device is opened at run time through exec, nothing to link

			list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/Amiga/AmigaAudioDevice.h)
			list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/Amiga/AmigaAudioDevice.cpp)
		elseif(SDLAUDIO_FOUND)
			set(_NCINE_AUDIO_BACKEND "SDL (software mixer into SDL's audio queue)")
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_SDLAUDIO")
			# SDL itself is already linked as the window backend

			list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/SDL/SdlAudioDevice.h)
			list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/SDL/SdlAudioDevice.cpp)
		elseif(PS3AUDIO_FOUND)
			set(_NCINE_AUDIO_BACKEND "PS3 (PSL1GHT libaudio mixer)")
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_PS3AUDIO")
			# libaudio is the lv2 audio port itself; libsysmodule loads the SYSMODULE_AUDIO PRX it needs
			target_link_libraries(${NCINE_APP} PRIVATE audio sysmodule)

			list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/PS3/Ps3AudioDevice.h)
			list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Audio/Backends/PS3/Ps3AudioDevice.cpp)
		endif()
		message(STATUS "Audio backend: ${_NCINE_AUDIO_BACKEND}")

		if(VORBIS_FOUND)
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_VORBIS")
			if(VORBIS_DYNAMIC_LINK)
				target_compile_definitions(${NCINE_APP} PRIVATE "WITH_VORBIS_DYNAMIC")
				target_include_directories(${NCINE_APP} PRIVATE "${EXTERNAL_INCLUDES_DIR}")
			else()
				target_link_libraries(${NCINE_APP} PRIVATE Vorbis::Vorbisfile)
			endif()
			
			list(APPEND HEADERS
				${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderOgg.h
				${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderOgg.h)

			list(APPEND SOURCES
				${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderOgg.cpp
				${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderOgg.cpp)
		endif()
		
		if(OPENMPT_FOUND)
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_OPENMPT")
			if(OPENMPT_DYNAMIC_LINK)
				target_compile_definitions(${NCINE_APP} PRIVATE "WITH_OPENMPT_DYNAMIC")
				target_include_directories(${NCINE_APP} PRIVATE "${EXTERNAL_INCLUDES_DIR}/libopenmpt/")
				target_link_libraries(${NCINE_APP} PRIVATE ${CMAKE_DL_LIBS})
			else()
				target_link_libraries(${NCINE_APP} PRIVATE libopenmpt::libopenmpt)
			endif()
			
			list(APPEND HEADERS
				${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderMpt.h
				${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderMpt.h)

			list(APPEND SOURCES
				${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderMpt.cpp
				${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderMpt.cpp)
		elseif(NCINE_WITH_XMP)
			# libxmp, the lightweight tracker player for platforms where libopenmpt is not real-time-viable
			# (today: the classic Amiga). Downloaded and built by cmake/Findlibxmp.cmake - the full library
			# rather than libxmp-lite, because the lite build drops the Galaxy Music System loaders that
			# read the game's original ".j2b" modules.
			target_compile_definitions(${NCINE_APP} PRIVATE "WITH_XMP")
			target_link_libraries(${NCINE_APP} PRIVATE Libxmp)

			list(APPEND HEADERS
				${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderXmp.h
				${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderXmp.h)

			list(APPEND SOURCES
				${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderXmp.cpp
				${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderXmp.cpp)
		endif()
	elseif(NOT NCINE_BUILD_ANDROID)
		message(STATUS "Cannot find any audio backend library")
	endif()
endif()

#if(PNG_FOUND)
#	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_PNG")
#	target_link_libraries(${NCINE_APP} PRIVATE PNG::PNG)

#	list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureSaverPng.h)
#	list(APPEND PRIVATE_HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPng.h)
#	list(APPEND SOURCES
#		${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPng.cpp
#		${NCINE_SOURCE_DIR}/nCine/Graphics/TextureSaverPng.cpp)
#endif()
#if(WEBP_FOUND)
#	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_WEBP")
#	target_link_libraries(${NCINE_APP} PRIVATE WebP::WebP)

#	list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureSaverWebP.h)
#	list(APPEND PRIVATE_HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderWebP.h)
#	list(APPEND SOURCES
#		${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderWebP.cpp
#		${NCINE_SOURCE_DIR}/nCine/Graphics/TextureSaverWebP.cpp)
#endif()

if(Threads_FOUND AND NCINE_WITH_THREADS)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_THREADS")
	target_link_libraries(${NCINE_APP} PRIVATE Threads::Threads)

	if(WIN32)
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Threading/WindowsThreadSync.cpp)
	else()
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Threading/PosixThreadSync.cpp)
	endif()

	list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Threading/ThreadPool.h)
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Threading/ThreadPool.cpp)
endif()

#if(LUA_FOUND)
#	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_LUA")
#	target_link_libraries(${NCINE_APP} PRIVATE Lua::Lua)

#	list(APPEND HEADERS
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaTypes.h
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaStateManager.h
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaUtils.h
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaDebug.h
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaRectUtils.h
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaVector2Utils.h
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaVector3Utils.h
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaColorUtils.h
#	)

#	list(APPEND PRIVATE_HEADERS
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaNames.h
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaStatistics.h
#	)

#	list(APPEND SOURCES
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaStateManager.cpp
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaUtils.cpp
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaDebug.cpp
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaStatistics.cpp
#		${NCINE_SOURCE_DIR}/nCine/Scripting/LuaColorUtils.cpp
#	)

#	if(NCINE_WITH_SCRIPTING_API)
#		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_SCRIPTING_API")

#		list(APPEND HEADERS
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaUntrackedUserData.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaIAppEventHandler.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaIInputEventHandler.h
#		)

#		list(APPEND PRIVATE_HEADERS
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaClassTracker.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaILogger.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaRect.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaVector2.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaVector3.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaColor.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaIInputManager.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaMouseEvents.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaKeys.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaKeyboardEvents.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaJoystickEvents.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaTouchEvents.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaTimeStamp.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaFileSystem.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaApplication.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaAppConfiguration.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaSceneNode.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaDrawableNode.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaTexture.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaBaseSprite.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaSprite.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaMeshSprite.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaRectAnimation.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaAnimatedSprite.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaFont.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaTextNode.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaParticleSystem.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaViewport.h
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaCamera.h
#		)

#		list(APPEND SOURCES
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaIAppEventHandler.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaILogger.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaColor.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaIInputManager.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaIInputEventHandler.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaMouseEvents.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaKeys.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaKeyboardEvents.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaJoystickEvents.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaTouchEvents.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaTimeStamp.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaFileSystem.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaApplication.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaAppConfiguration.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaSceneNode.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaDrawableNode.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaTexture.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaBaseSprite.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaSprite.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaMeshSprite.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaRectAnimation.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaAnimatedSprite.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaFont.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaTextNode.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaParticleSystem.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaViewport.cpp
#			${NCINE_SOURCE_DIR}/nCine/Scripting/LuaCamera.cpp
#		)

#		if(OPENAL_FOUND)
#			list(APPEND PRIVATE_HEADERS
#				${NCINE_SOURCE_DIR}/nCine/Scripting/LuaIAudioDevice.h
#				${NCINE_SOURCE_DIR}/nCine/Scripting/LuaIAudioPlayer.h
#				${NCINE_SOURCE_DIR}/nCine/Scripting/LuaAudioStreamPlayer.h
#				${NCINE_SOURCE_DIR}/nCine/Scripting/LuaAudioBuffer.h
#				${NCINE_SOURCE_DIR}/nCine/Scripting/LuaAudioBufferPlayer.h
#			)

#			list(APPEND SOURCES
#				${NCINE_SOURCE_DIR}/nCine/Scripting/LuaIAudioDevice.cpp
#				${NCINE_SOURCE_DIR}/nCine/Scripting/LuaIAudioPlayer.cpp
#				${NCINE_SOURCE_DIR}/nCine/Scripting/LuaAudioStreamPlayer.cpp
#				${NCINE_SOURCE_DIR}/nCine/Scripting/LuaAudioBuffer.cpp
#				${NCINE_SOURCE_DIR}/nCine/Scripting/LuaAudioBufferPlayer.cpp
#			)
#		endif()

#		if(NOT ANDROID)
#			list(APPEND PRIVATE_HEADERS ${NCINE_SOURCE_DIR}/nCine/Scripting/LuaEventHandler.h)
#			list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Scripting/LuaEventHandler.cpp)
#		endif()
#	endif()
#endif()

#if(NCINE_WITH_ALLOCATORS)
#	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_ALLOCATORS")
#
#	list(APPEND HEADERS
#		${NCINE_ROOT}/include/nctl/AllocManager.h
#		${NCINE_ROOT}/include/nctl/IAllocator.h
#		${NCINE_ROOT}/include/nctl/MallocAllocator.h
#		${NCINE_ROOT}/include/nctl/LinearAllocator.h
#		${NCINE_ROOT}/include/nctl/StackAllocator.h
#		${NCINE_ROOT}/include/nctl/PoolAllocator.h
#		${NCINE_ROOT}/include/nctl/FreeListAllocator.h
#		${NCINE_ROOT}/include/nctl/ProxyAllocator.h
#	)
#
#	list(APPEND SOURCES
#		${NCINE_ROOT}/src/base/AllocManager.cpp
#		${NCINE_ROOT}/src/base/IAllocator.cpp
#		${NCINE_ROOT}/src/base/MallocAllocator.cpp
#		${NCINE_ROOT}/src/base/LinearAllocator.cpp
#		${NCINE_ROOT}/src/base/StackAllocator.cpp
#		${NCINE_ROOT}/src/base/PoolAllocator.cpp
#		${NCINE_ROOT}/src/base/FreeListAllocator.cpp
#		${NCINE_ROOT}/src/base/ProxyAllocator.cpp
#	)
#endif()

if(ANGELSCRIPT_FOUND)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_ANGELSCRIPT")
	target_link_libraries(${NCINE_APP} PRIVATE Angelscript)
endif()

if(NCINE_WITH_IMGUI AND NOT DEDICATED_SERVER)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_IMGUI")

	# For external projects compiling using an nCine build directory
	set(IMGUI_INCLUDE_ONLY_DIR ${IMGUI_SOURCE_DIR}/include_only)
	file(COPY ${IMGUI_SOURCE_DIR}/imgui.h DESTINATION ${IMGUI_INCLUDE_ONLY_DIR})
	file(COPY ${IMGUI_SOURCE_DIR}/imconfig.h DESTINATION ${IMGUI_INCLUDE_ONLY_DIR})

	list(APPEND HEADERS
		${IMGUI_INCLUDE_ONLY_DIR}/imgui.h
		${IMGUI_INCLUDE_ONLY_DIR}/imconfig.h
		${IMGUI_SOURCE_DIR}/imgui_internal.h
		${IMGUI_SOURCE_DIR}/imstb_rectpack.h
		${IMGUI_SOURCE_DIR}/imstb_textedit.h
		${IMGUI_SOURCE_DIR}/imstb_truetype.h
		${NCINE_SOURCE_DIR}/nCine/Graphics/ImGuiDrawing.h
		${NCINE_SOURCE_DIR}/nCine/Input/ImGuiJoyMappedInput.h
	)

	list(APPEND SOURCES
		${IMGUI_SOURCE_DIR}/imgui.cpp
		${IMGUI_SOURCE_DIR}/imgui_demo.cpp
		${IMGUI_SOURCE_DIR}/imgui_draw.cpp
		${IMGUI_SOURCE_DIR}/imgui_tables.cpp
		${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
		${NCINE_SOURCE_DIR}/nCine/Graphics/ImGuiDrawing.cpp
		${NCINE_SOURCE_DIR}/nCine/Input/ImGuiJoyMappedInput.cpp
	)

	if(GLFW_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "GLFW")
		list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Backends/ImGuiGlfwInput.h)
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Backends/ImGuiGlfwInput.cpp)
	elseif((SDL2_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "SDL2") OR (SDL3_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "SDL3"))
		list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Backends/ImGuiSdlInput.h)
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Backends/ImGuiSdlInput.cpp)
	elseif(Qt5_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "QT5")
		list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Backends/ImGuiQt5Input.h)
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Backends/ImGuiQt5Input.cpp)
	elseif(ANDROID)
		list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Backends/Android/ImGuiAndroidInput.h)
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Backends/Android/ImGuiAndroidInput.cpp)
	endif()

	list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/ImGuiDebugOverlay.h)
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Graphics/ImGuiDebugOverlay.cpp)

	target_include_directories(${NCINE_APP}
		INTERFACE $<BUILD_INTERFACE:${IMGUI_INCLUDE_ONLY_DIR}>
		PRIVATE $<BUILD_INTERFACE:${IMGUI_INCLUDE_ONLY_DIR}>)

	if(MINGW)
		target_link_libraries(${NCINE_APP} PRIVATE imm32 dwmapi)
	endif()
endif()

if(NCINE_WITH_TRACY)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_TRACY")
	if(NOT ANDROID AND NOT APPLE AND NOT EMSCRIPTEN)
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_TRACY_OPENGL")
	endif()
	target_compile_definitions(${NCINE_APP} PUBLIC "TRACY_ENABLE")
	target_compile_definitions(${NCINE_APP} PRIVATE "TRACY_DELAYED_INIT")

	# For external projects compiling using an nCine build directory
	set(TRACY_INCLUDE_ONLY_DIR ${TRACY_SOURCE_DIR}/include_only)
	file(GLOB TRACY_ROOT_HPP "${TRACY_SOURCE_DIR}/public/tracy/*.hpp" "${TRACY_SOURCE_DIR}/public/tracy/*.h")
	file(COPY ${TRACY_ROOT_HPP} DESTINATION ${TRACY_INCLUDE_ONLY_DIR}/tracy/tracy)
	file(GLOB TRACY_COMMON_HPP "${TRACY_SOURCE_DIR}/public/common/*.hpp" "${TRACY_SOURCE_DIR}/public/common/*.h")
	file(COPY ${TRACY_COMMON_HPP} DESTINATION ${TRACY_INCLUDE_ONLY_DIR}/tracy/common)
	file(COPY "${TRACY_SOURCE_DIR}/public/common/TracySystem.cpp" DESTINATION ${TRACY_INCLUDE_ONLY_DIR}/tracy/common)
	file(GLOB TRACY_CLIENT_HPP "${TRACY_SOURCE_DIR}/public/client/*.hpp" "${TRACY_SOURCE_DIR}/public/client/*.h")
	file(COPY ${TRACY_CLIENT_HPP} DESTINATION ${TRACY_INCLUDE_ONLY_DIR}/tracy/client)
	#file(COPY "${TRACY_SOURCE_DIR}/LICENSE" DESTINATION ${TRACY_INCLUDE_ONLY_DIR}/tracy)

	list(APPEND HEADERS
		${NCINE_SOURCE_DIR}/nCine/tracy.h
		${NCINE_SOURCE_DIR}/nCine/tracy_opengl.h
	)

	list(APPEND SOURCES
		${NCINE_SOURCE_DIR}/nCine/tracy_memory.cpp
		${TRACY_SOURCE_DIR}/public/TracyClient.cpp
	)

	target_include_directories(${NCINE_APP}
		PUBLIC $<BUILD_INTERFACE:${TRACY_INCLUDE_ONLY_DIR}/tracy>
		PUBLIC $<INSTALL_INTERFACE:include/tracy>)
endif()

#if(NCINE_WITH_RENDERDOC AND NOT APPLE)
#	find_file(RENDERDOC_API_H
#		NAMES renderdoc.h renderdoc_app.h
#		PATHS "$ENV{ProgramW6432}/RenderDoc"
#			"$ENV{ProgramFiles}/RenderDoc"
#			"$ENV{ProgramFiles\(x86\)}/RenderDoc"
#			${RENDERDOC_DIR}
#		PATH_SUFFIXES "include"
#		DOC "Path to the RenderDoc header file")
#
#	if(NOT EXISTS ${RENDERDOC_API_H})
#		message(FATAL_ERROR "RenderDoc header file not found")
#	endif()
#
#	get_filename_component(RENDERDOC_INCLUDE_DIR ${RENDERDOC_API_H} DIRECTORY)
#	target_include_directories(${NCINE_APP} PRIVATE ${RENDERDOC_INCLUDE_DIR})
#
#	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_RENDERDOC")
#	if(UNIX)
#		target_link_libraries(${NCINE_APP} PRIVATE dl)
#	endif()
#
#	list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/RenderDocCapture.h ${RENDERDOC_API_H})
#	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Graphics/RenderDocCapture.cpp)
#endif()

if(NOT CURL_FOUND AND NOT WIN32)
	# `WebRequest` needs an HTTP backend: WinHTTP on Windows, libcurl everywhere else. Without one
	# it cannot be compiled at all, so it's excluded together with everything that depends on it
	# (the update check and the online server list, see `WITH_ONLINE_MULTIPLAYER`).
	list(REMOVE_ITEM SOURCES "${NCINE_SOURCE_DIR}/Shared/IO/WebRequest.cpp")
endif()

if(CURL_FOUND)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_CURL")
	target_link_libraries(${NCINE_APP} PRIVATE CURL::libcurl)
endif()

if(ZLIB_FOUND)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_ZLIB")
	target_link_libraries(${NCINE_APP} PRIVATE ZLIB::ZLIB)
endif()

if(LZ4_FOUND AND TARGET Lz4)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_LZ4")
	target_link_libraries(${NCINE_APP} PRIVATE Lz4)
endif()

if(ZSTD_FOUND AND TARGET Zstd)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_ZSTD")
	target_link_libraries(${NCINE_APP} PRIVATE Zstd)
endif()

if(NCINE_BUILD_ANDROID)
	list(APPEND HEADERS
		${NCINE_SOURCE_DIR}/nCine/Backends/Android/AndroidApplication.h
	)
endif()

if(ANDROID)
	list(APPEND HEADERS
		${NCINE_SOURCE_DIR}/nCine/Backends/Android/AndroidInputManager.h
		${NCINE_SOURCE_DIR}/nCine/Backends/Android/AndroidJniHelper.h
		${NCINE_SOURCE_DIR}/nCine/Backends/Android/EglGfxDevice.h
		${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPkm.h
	)
	list(APPEND SOURCES
		${NCINE_SOURCE_DIR}/nCine/Backends/Android/AndroidApplication.cpp
		${NCINE_SOURCE_DIR}/nCine/Backends/Android/AndroidInputManager.cpp
		${NCINE_SOURCE_DIR}/nCine/Backends/Android/AndroidJniHelper.cpp
		${NCINE_SOURCE_DIR}/nCine/Backends/Android/AndroidKeys.cpp
		${NCINE_SOURCE_DIR}/nCine/Backends/Android/EglGfxDevice.cpp
		${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPkm.cpp
	)
elseif(WINDOWS_PHONE OR WINDOWS_STORE)
	list(APPEND HEADERS
		${NCINE_SOURCE_DIR}/nCine/Backends/Uwp/UwpApplication.h
		${NCINE_SOURCE_DIR}/nCine/Backends/Uwp/UwpGfxDevice.h
		${NCINE_SOURCE_DIR}/nCine/Backends/Uwp/UwpInputManager.h
	)
	list(APPEND SOURCES
		${NCINE_SOURCE_DIR}/nCine/Backends/Uwp/UwpApplication.cpp
		${NCINE_SOURCE_DIR}/nCine/Backends/Uwp/UwpGfxDevice.cpp
		${NCINE_SOURCE_DIR}/nCine/Backends/Uwp/UwpInputManager.cpp
	)
	
	set(UWP_ASSETS
		${NCINE_SOURCE_DIR}/Icons/Logo.png
		${NCINE_SOURCE_DIR}/Icons/SmallLogo.png
		${NCINE_SOURCE_DIR}/Icons/SplashScreen.png
		${NCINE_SOURCE_DIR}/Icons/StoreLogo.png
	)
		
	target_sources(${NCINE_APP} PRIVATE ${UWP_ASSETS})
	set_property(SOURCE ${UWP_ASSETS} PROPERTY VS_DEPLOYMENT_CONTENT 1)
	set_property(SOURCE ${UWP_ASSETS} PROPERTY VS_DEPLOYMENT_LOCATION "Assets")
	source_group("Assets" FILES ${UWP_ASSETS})

	set(PACKAGE_VERSION_MAJOR ${NCINE_VERSION_MAJOR})
	set(PACKAGE_VERSION_MINOR ${NCINE_VERSION_MINOR})
	set(PACKAGE_VERSION_PATCH ${NCINE_VERSION_PATCH})
	if(NCINE_VERSION_FROM_GIT AND GIT_NO_TAG)
		if(DEFINED NCINE_VERSION_PATCH_LAST)
			set(PACKAGE_VERSION_PATCH ${NCINE_VERSION_PATCH_LAST})
		else()
			set(PACKAGE_VERSION_PATCH "0")
		endif()
	endif()
	set(PACKAGE_VERSION_REV "0")
	if(DEFINED GIT_REV_COUNT)
		set(PACKAGE_VERSION_REV ${GIT_REV_COUNT})
	endif()
	
	set(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_PATCH}.${PACKAGE_VERSION_REV}")
	set(PACKAGE_GUID "a7153bb5-7dc8-4985-9f9c-3853f96034c9")
	configure_file(${NCINE_SOURCE_DIR}/Package.appxmanifest.in ${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest @ONLY)
	list(APPEND GENERATED_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest)
	
	# Include dependencies in UWP package
	set(UWP_DEPENDENCIES
		"${MSVC_WINRT_BINDIR}/msvcp140.dll"
		"${MSVC_WINRT_BINDIR}/vcruntime140.dll"
		"${MSVC_WINRT_BINDIR}/vcruntime140_1.dll"
	)
	
	if(NCINE_PREFERRED_RHI STREQUAL "D3D11")
		# Direct3D 11 renders through the DXGI CoreWindow swap chain; d3d11.dll / dxgi.dll / d3dcompiler_47.dll
		# are inbox OS components on UWP (Windows Store / Xbox), so no EGL / OpenGL|ES runtime DLLs are packaged.
	elseif(NCINE_WITH_ANGLE)
		list(APPEND UWP_DEPENDENCIES
			"${MSVC_WINRT_BINDIR}/libEGL.dll"
			"${MSVC_WINRT_BINDIR}/libGLESv2.dll")
	else()
		message(STATUS "Using Mesa as OpenGL|ES backend (Experimental)")

		list(APPEND UWP_DEPENDENCIES
			"${MSVC_WINRT_BINDIR}/Mesa/dxil.dll"
			"${MSVC_WINRT_BINDIR}/Mesa/libEGL.dll"
			"${MSVC_WINRT_BINDIR}/Mesa/libgallium_wgl.dll"
			"${MSVC_WINRT_BINDIR}/Mesa/libglapi.dll"
			"${MSVC_WINRT_BINDIR}/Mesa/libGLESv2.dll"
			"${MSVC_WINRT_BINDIR}/Mesa/z-1.dll")
	endif()
		
	if(ZLIB_FOUND)
		list(APPEND UWP_DEPENDENCIES "${MSVC_BINDIR}/zlib.dll")
	endif()

	if(NCINE_WITH_WEBP AND WEBP_FOUND)
		list(APPEND UWP_DEPENDENCIES "${MSVC_BINDIR}/libwebp.dll")
	endif()

	if(NCINE_WITH_AUDIO AND OPENAL_FOUND)
		list(APPEND UWP_DEPENDENCIES "${MSVC_BINDIR}/OpenAL32.dll")

		if(NCINE_WITH_VORBIS AND VORBIS_FOUND AND NOT VORBIS_DYNAMIC_LINK)
			list(APPEND UWP_DEPENDENCIES "${MSVC_BINDIR}/libogg.dll" "${MSVC_BINDIR}/libvorbis.dll" "${MSVC_BINDIR}/libvorbisfile.dll")
		endif()
		
		if(NCINE_WITH_OPENMPT AND OPENMPT_FOUND AND NOT OPENMPT_DYNAMIC_LINK)
			list(APPEND UWP_DEPENDENCIES "${MSVC_BINDIR}/libopenmpt.dll" "${MSVC_BINDIR}/openmpt-mpg123.dll" "${MSVC_BINDIR}/openmpt-ogg.dll" "${MSVC_BINDIR}/openmpt-vorbis.dll" "${MSVC_BINDIR}/openmpt-zlib.dll")
		endif()
	endif()

	ncine_deploy_runtime_dependencies(${UWP_DEPENDENCIES})

	# Include `Content` directory
	file(GLOB_RECURSE PACKAGE_CONTENT_FILES "${NCINE_CONTENT_DIR}/*")
	foreach(CONTENT_FILE ${PACKAGE_CONTENT_FILES})
		# Preserving directory structure
		file(RELATIVE_PATH CONTENT_FILE_RELPATH ${NCINE_CONTENT_DIR} ${CONTENT_FILE})
		get_filename_component(CONTENT_FILE_RELPATH ${CONTENT_FILE_RELPATH} DIRECTORY)
		
		target_sources(${NCINE_APP} PRIVATE ${CONTENT_FILE})
		set_property(SOURCE ${CONTENT_FILE} PROPERTY VS_DEPLOYMENT_CONTENT 1)
		set_property(SOURCE ${CONTENT_FILE} PROPERTY VS_DEPLOYMENT_LOCATION "Content/${CONTENT_FILE_RELPATH}")
		source_group("Content/${CONTENT_FILE_RELPATH}" FILES ${CONTENT_FILE})
	endforeach()
else()
	if(NCINE_BUILD_LIBRETRO)
		# The libretro entry point replaces MainApplication (the frontend drives the game loop)
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_LIBRETRO")
		list(APPEND HEADERS ${NCINE_SOURCE_DIR}/Dependencies/libretro/libretro.h)
		list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Backends/Libretro/LibretroApplication.h)
		list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Backends/Libretro/LibretroGfxDevice.h)
		list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Backends/Libretro/LibretroInputManager.h)
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/libretro.cpp)
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Backends/Libretro/LibretroApplication.cpp)
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Backends/Libretro/LibretroGfxDevice.cpp)
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Backends/Libretro/LibretroInputManager.cpp)
	else()
		list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/MainApplication.h)
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/MainApplication.cpp)
	endif()

	if(NINTENDO_SWITCH)
		nx_generate_nacp(${NCINE_APP}.nacp
			NAME "${NCINE_APP_NAME}"
			AUTHOR "${NCINE_APP_VENDOR}"
			VERSION "${NCINE_VERSION}"
		)

		nx_create_nro(${NCINE_APP}
			NACP "${NCINE_APP}.nacp"
			ICON "${NCINE_SOURCE_DIR}/Icons/256px.png"
			ROMFS "${NCINE_CONTENT_DIR}"
		)
	elseif(NINTENDO_WII OR NINTENDO_GAMECUBE)
		# Converts the ELF to the .dol format loadable by Swiss/Homebrew Channel/Dolphin
		ogc_create_dol(${NCINE_APP})

		# Stage the SD card layout expected by ContentResolver ("sd:/apps/Jazz2/" on Wii, which is
		# the standard Homebrew Channel layout, and "carda:/Jazz2/" on GameCube) into "sd/" in the
		# build directory, so its contents can be copied directly to a (virtual) SD card
		if(NINTENDO_WII)
			set(OGC_SD_APP_DIR "${CMAKE_BINARY_DIR}/sd/apps/Jazz2")
			set(OGC_SD_DOL_NAME "boot.dol")
		else()
			set(OGC_SD_APP_DIR "${CMAKE_BINARY_DIR}/sd/Jazz2")
			set(OGC_SD_DOL_NAME "Jazz2.dol")
		endif()
		add_custom_command(TARGET ${NCINE_APP} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "${OGC_SD_APP_DIR}"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/${NCINE_APP}.dol" "${OGC_SD_APP_DIR}/${OGC_SD_DOL_NAME}"
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${NCINE_CONTENT_DIR}" "${OGC_SD_APP_DIR}/Content"
			COMMENT "Staging SD card layout with game content"
			VERBATIM)
	elseif(PLATFORM_DREAMCAST)
		# Package a bootable CDI image with the game content included ("/cd/Content/"), so it can be
		# started directly in an emulator (Flycast) or burned to a disc; requires mkdcdisc
		find_program(MKDCDISC_EXECUTABLE mkdcdisc)
		if(MKDCDISC_EXECUTABLE)
			# The content is staged as "cd/Content" first, so the directory on the disc is always named
			# "Content" regardless of the source directory name (e.g. "ContentEmscripten" for the demo)
			add_custom_command(TARGET ${NCINE_APP} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_directory "${NCINE_CONTENT_DIR}" "${CMAKE_BINARY_DIR}/cd/Content"
				COMMAND "${MKDCDISC_EXECUTABLE}" -e "$<TARGET_FILE:${NCINE_APP}>" -d "${CMAKE_BINARY_DIR}/cd/Content" -n "Jazz2 Resurrection" -o "${CMAKE_BINARY_DIR}/${NCINE_APP}.cdi"
				COMMENT "Creating bootable CDI image with game content"
				VERBATIM)
		else()
			message(STATUS "mkdcdisc not found, bootable CDI image will not be created")
		endif()
	elseif(PLATFORM_AMIGA)
		# libnix ships the math functions in a separate libm; the driver does not add it on its own.
		# pthread satisfies the thread-id the trace header stamps on every line (the game itself is
		# single-threaded here); atomic covers the exchange/fetch forms the 68060 has no inline
		# sequence for, the same reason the PS2 links it.
		target_link_libraries(${NCINE_APP} PRIVATE m pthread atomic)

		# Stage a ready-to-run game directory ("dist/") with the executable next to "Content/", the
		# layout ContentResolver's relative paths expect. The directory can be copied straight onto an
		# Amiga hard disk or mounted as an emulator directory-hard-drive.
		add_custom_command(TARGET ${NCINE_APP} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${NCINE_APP}>" "${CMAKE_BINARY_DIR}/dist/Jazz2"
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${NCINE_CONTENT_DIR}" "${CMAKE_BINARY_DIR}/dist/Content"
			COMMENT "Staging Amiga game directory with game content"
			VERBATIM)
	elseif(PLATFORM_AMIGAOS4 OR PLATFORM_MORPHOS)
		# Same staged layout as the classic Amiga above - the executable next to "Content/" - copied to
		# a PowerPC Amiga machine as it is. The binary is stripped on the way: these toolchains leave the
		# DWARF in even in Release, which is two thirds of its size and of no use on the target.
		add_custom_command(TARGET ${NCINE_APP} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
			COMMAND "${CMAKE_STRIP}" -o "${CMAKE_BINARY_DIR}/dist/Jazz2" "$<TARGET_FILE:${NCINE_APP}>"
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${NCINE_CONTENT_DIR}" "${CMAKE_BINARY_DIR}/dist/Content"
			COMMENT "Staging game directory with game content"
			VERBATIM)
	elseif(PLATFORM_N64)
		# The n64.cmake toolchain file leaves the executable suffix empty; name it like the other console
		# targets do, so the intermediate ELF is recognizable next to the ROM that is packed from it
		set_target_properties(${NCINE_APP} PROPERTIES SUFFIX ".elf")

		# The libdragon libraries the engine calls into, in one link group with libc and libstdc++: the
		# toolchain's GCC ships an EMPTY `*lib:` spec - the driver never adds -lc on its own (n64.mk
		# passes it explicitly for the same reason) - and the dependencies are circular anyway (libc's
		# reentrant syscalls resolve from libdragonsys, libdragon calls back into libc).
		target_link_libraries(${NCINE_APP} PRIVATE "-Wl,--start-group" dragon m dragonsys c stdc++ "-Wl,--end-group")

		# Package a bootable .z64 ROM, reproducing libdragon's n64.mk "%.z64" rule: the symbol table for
		# on-console backtraces, the stripped+compressed ELF, and a DragonFS image with the game content
		# ("rom:/Content/", where the N64 branch of ContentResolver::GetContentPath() looks for it) are
		# concatenated by n64tool behind a table of contents. ed64romconfig then marks the save type in
		# the ROM header (the advanced homebrew header), which is how flashcarts and emulators know to
		# provide the EEPROM the preferences are stored on.
		#
		# The content is staged as "dfs/Content" first, so the directory in the image is always named
		# "Content" regardless of the source directory name (mirrors the Dreamcast arm above); a staged
		# "Source.pak" is renamed to "Prebaked.pak" the way the PS2 arm does, because the console has no
		# writable cache to convert into.
		set(_n64MarkPrebakedCommand "")
		if(EXISTS "${NCINE_CONTENT_DIR}/Source.pak")
			set(_n64MarkPrebakedCommand COMMAND ${CMAKE_COMMAND} -E rename
				"${CMAKE_BINARY_DIR}/dfs/Content/Source.pak" "${CMAKE_BINARY_DIR}/dfs/Content/Prebaked.pak")
		else()
			message(STATUS "No \"Source.pak\" in \"${NCINE_CONTENT_DIR}\", the ROM image will not be marked as prebaked")
		endif()

		# A cartridge ROM is capped at the 64 MB the flashcarts provide and a full content tree does not
		# fit, so the packaging drops what the console cannot use or cannot afford: "Music" is undecodable
		# dead weight here (NCINE_WITH_OPENMPT is off) and the 7 MB "ending" cinematic does not fit next to
		# the tilesets, while the intro does and is what the game opens with.
		# TODO: re-encode the cinematics at a lower bitrate in AssetPacker and put the ending back.
		add_custom_command(TARGET ${NCINE_APP} POST_BUILD
			# The staging directory is rebuilt from scratch: copy_directory keeps destination files that are
			# no longer in the source, so a stale file from a previous build (e.g., a "Source.pak" already
			# renamed to "Prebaked.pak" below) would otherwise be packed into every later ROM
			COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/dfs/Content"
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${NCINE_CONTENT_DIR}" "${CMAKE_BINARY_DIR}/dfs/Content"
			# Two commands rather than one "-E rm": that form needs CMake 3.17, and the project's minimum
			# is 3.15 (remove_directory/remove both ignore paths that do not exist)
			COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/dfs/Content/Music"
			COMMAND ${CMAKE_COMMAND} -E remove -f "${CMAKE_BINARY_DIR}/dfs/Content/Cinematics/ending.j2v"
			${_n64MarkPrebakedCommand}
			COMMAND "${N64_MKDFS}" "${CMAKE_BINARY_DIR}/${NCINE_APP}.dfs" "${CMAKE_BINARY_DIR}/dfs"
			COMMAND "${N64_SYM}" --all "$<TARGET_FILE:${NCINE_APP}>" "${CMAKE_BINARY_DIR}/${NCINE_APP}.elf.sym"
			COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${NCINE_APP}>" "${CMAKE_BINARY_DIR}/${NCINE_APP}.elf.stripped"
			COMMAND "${CMAKE_STRIP}" -s "${CMAKE_BINARY_DIR}/${NCINE_APP}.elf.stripped"
			COMMAND "${N64_ELFCOMPRESS}" -o "${CMAKE_BINARY_DIR}" -c 1 "${CMAKE_BINARY_DIR}/${NCINE_APP}.elf.stripped"
			COMMAND "${N64_TOOL}" --toc --title "Jazz2 Resurrection" --output "${CMAKE_BINARY_DIR}/${NCINE_APP}.z64"
				--align 256 "${CMAKE_BINARY_DIR}/${NCINE_APP}.elf.stripped"
				"${CMAKE_BINARY_DIR}/${NCINE_APP}.elf.sym"
				--align 16 "${CMAKE_BINARY_DIR}/${NCINE_APP}.dfs"
			COMMAND "${N64_ED64ROMCONFIG}" --savetype eeprom16k --regionfree "${CMAKE_BINARY_DIR}/${NCINE_APP}.z64"
			COMMAND ${CMAKE_COMMAND} "-DN64_ROM=${CMAKE_BINARY_DIR}/${NCINE_APP}.z64" -P "${CMAKE_SOURCE_DIR}/cmake/n64_check_rom_size.cmake"
			COMMENT "Creating bootable Z64 ROM image with game content"
			VERBATIM)
	elseif(PLATFORM_PSP)
		# The pspdev toolchain leaves the executable suffix empty; name it like the other console targets do,
		# so the intermediate ELF is recognizable next to the EBOOT that is packed from it
		set_target_properties(${NCINE_APP} PROPERTIES SUFFIX ".elf")

		# The PSPSDK libraries the engine calls into. The pspdev toolchain file already adds the default set
		# from build.mak (pspdebug/pspdisplay/pspge/pspctrl and the net stubs), but linking them explicitly
		# keeps the dependency visible here the way the other consoles list theirs - and pspgu/pspgum/psppower
		# are NOT in that default set.
		target_link_libraries(${NCINE_APP} PRIVATE
			pspgum
			pspgu
			pspge
			pspdisplay
			pspctrl
			psppower
			psprtc
			pspdebug
			m
		)

		if(CURL_FOUND)
			# The console has no socket stack at boot, so MainApplication brings one up for `WebRequest`
			# (see PspNetworkInitialize) - that is the only thing in the engine calling into these stubs,
			# because libcurl on top of them only needs the BSD sockets the newlib port already wraps.
			# They are in the toolchain's default set as well, but listing them here keeps the dependency
			# visible like the ones above.
			#
			# `atomic` is libatomic, for the same shape of reason as on the PlayStation 2 (see that arm): the
			# Allegrex is a 32-bit MIPS with no 64-bit atomic instruction, so the `std::atomic<std::int64_t>`
			# byte counters `WebRequest` keeps per request (WebRequestImpl::_bytesReceived and
			# WebRequestCURL::_bytesSent) are lowered to __atomic_load_8 / __atomic_store_8 /
			# __atomic_fetch_add_8 calls instead of inline instructions. It is scoped to this arm because
			# `WebRequest` is what brings those counters in - nothing else on this console needs 64-bit atomics.
			target_link_libraries(${NCINE_APP} PRIVATE
				pspnet
				pspnet_inet
				pspnet_apctl
				pspnet_resolver
				psputility
				atomic
			)
		endif()

		if(OPENAL_FOUND)
			# pspdev's OpenAL is an OpenAL Soft whose only output backend ("src/Alc/psp.c") drives
			# sceAudioOutputBlocking() from a thread of its own, so the audio stubs it imports have to be
			# linked here - the toolchain's default set does not include them. psphprm comes with it because
			# the same backend also implements capture and probes for the headset microphone.
			target_link_libraries(${NCINE_APP} PRIVATE
				pspaudio
				psphprm
			)
		endif()

		# Package an EBOOT.PBP into the standard homebrew layout ("ms0:/PSP/GAME/Jazz2/"), staged under
		# "ms0/" in the build directory together with the game content, so its contents can be copied
		# straight onto a memory stick (or handed to PPSSPP, which mounts a directory as the memory stick).
		# create_pbp_file() runs psp-strip / psp-fixup-imports / mksfoex / pack-pbp on the built ELF - see
		# "$PSPDEV/psp/share/CreatePBP.cmake", included by the toolchain file.
		set(PSP_EBOOT_DIR "${CMAKE_BINARY_DIR}/ms0/PSP/GAME/Jazz2")
		file(MAKE_DIRECTORY "${PSP_EBOOT_DIR}")
		create_pbp_file(
			TARGET ${NCINE_APP}
			#TITLE "${NCINE_APP_NAME}"
			TITLE "${NCINE_APP}"
			#VERSION "${NCINE_VERSION}"
			VERSION "01.00"
			# ICON0 is nominally 144x82; the firmware (and PPSSPP) scale whatever they are given, so the
			# existing square icon is reused instead of adding a PSP-shaped copy of it to the repository
			ICON_PATH "${NCINE_SOURCE_DIR}/Icons/128px.png"
			BACKGROUND_PATH NULL
			PREVIEW_PATH NULL
			OUTPUT_DIR "${PSP_EBOOT_DIR}"
			# MEMSIZE=1 asks the firmware for the extra memory of the 2000/3000 models (and is what PPSSPP
			# emulates by default); without it a user-mode application is capped at the 24 MB of a PSP-1000
			MEMSIZE 1
		)
		# The content is staged as "Content" next to the EBOOT, which is where the PSP branch of
		# ContentResolver::GetContentPath() looks for it
		add_custom_command(TARGET ${NCINE_APP} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${NCINE_CONTENT_DIR}" "${PSP_EBOOT_DIR}/Content"
			COMMENT "Staging memory stick layout with game content"
			VERBATIM)
	elseif(PLATFORM_PS2)
		# The ps2dev toolchain leaves the executable suffix empty; name it like the other console targets do
		set_target_properties(${NCINE_APP} PROPERTIES SUFFIX ".elf")

		# ISO9660 names are uppercase, and the boot stanza, the staged file and the volume label all have to
		# agree - so they are all derived from the target name rather than written out three times.
		string(TOUPPER "${NCINE_APP}" _ps2DiscName)
		# A PlayStation 2 disc is plain ISO9660 whose root carries a SYSTEM.CNF naming the executable to
		# boot, so no format-specific tool is needed - any ISO author will do. `VMODE = NTSC` matches the
		# 640x448 mode Ps2GfxDevice sets up; a PAL console runs an NTSC disc through its 60 Hz path, which is
		# what the fixed display geometry in GsVram's layout assumes. `VER` is nominally the disc revision
		# rather than the application version, but there is no separate source for it and the BIOS does not
		# interpret it, so the project version is the honest thing to publish.
		# Written outside the staging directory and copied in below: "cd/" is a build artifact, so anything
		# generated straight into it at configure time silently goes missing if it is cleaned and only the
		# build is re-run - which yields an ISO with no boot stanza that the BIOS refuses without a word.
		file(WRITE "${CMAKE_BINARY_DIR}/ps2/SYSTEM.CNF"
			"BOOT2 = cdrom0:\\${_ps2DiscName}.ELF;1\nVER = ${NCINE_VERSION_MAJOR}.${NCINE_VERSION_MINOR}\nVMODE = NTSC\n")

		# The I/O stack the newlib port needs to see the disc as a filesystem. cdfs registers a "cdfs:" device
		# with the original `ioman`, which is the I/O manager newlib's POSIX calls reach; it does not live in
		# ROM, so it rides on the disc and is loaded by SifLoadModule() at startup - which reaches it through
		# the IOP's own loadfile service, a different path from the POSIX open() that cannot resolve it.
		add_custom_command(TARGET ${NCINE_APP} PRE_LINK
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${CMAKE_BINARY_DIR}/ps2/SYSTEM.CNF" "${CMAKE_BINARY_DIR}/cd/SYSTEM.CNF"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"$ENV{PS2SDK}/iop/irx/cdfs.irx" "${CMAKE_BINARY_DIR}/cd/CDFS.IRX"
			COMMENT "Staging SYSTEM.CNF and cdfs.irx onto the disc image"
			VERBATIM)

		# Prefer xorrisofs, fall back to the older mkisofs/genisoimage; all three take the same options here
		find_program(PS2_MKISOFS_EXECUTABLE NAMES xorrisofs mkisofs genisoimage)
		if(PS2_MKISOFS_EXECUTABLE)
			# The content is staged as "cd/Content" first, so the directory on the disc is always named
			# "Content" regardless of the source directory name (mirrors the Dreamcast arm above)
			set(_ps2MarkPrebakedCommand "")
			if(EXISTS "${NCINE_CONTENT_DIR}/Source.pak")
				set(_ps2MarkPrebakedCommand COMMAND ${CMAKE_COMMAND} -E rename
					"${CMAKE_BINARY_DIR}/cd/Content/Source.pak" "${CMAKE_BINARY_DIR}/cd/Content/Prebaked.pak")
			else()
				message(STATUS "No \"Source.pak\" in \"${NCINE_CONTENT_DIR}\", the disc image will not be marked as prebaked")
			endif()

			add_custom_command(TARGET ${NCINE_APP} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_directory "${NCINE_CONTENT_DIR}" "${CMAKE_BINARY_DIR}/cd/Content"
				${_ps2MarkPrebakedCommand}
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${NCINE_APP}>" "${CMAKE_BINARY_DIR}/cd/${_ps2DiscName}.ELF"
				COMMAND "${PS2_MKISOFS_EXECUTABLE}" -quiet -iso-level 2 -l -V "${_ps2DiscName}" -o "${CMAKE_BINARY_DIR}/${NCINE_APP}.iso" "${CMAKE_BINARY_DIR}/cd"
				COMMENT "Creating bootable ISO image with game content"
				VERBATIM)
		else()
			# Not fatal: PCSX2 boots a bare ELF with `-elf`, which is enough for development
			message(STATUS "xorrisofs/mkisofs not found, bootable ISO image will not be created")
		endif()
	elseif(PLATFORM_PS3)
		# The ps3toolchain leaves the executable suffix empty; name it like the other console targets do, so
		# the intermediate ELF is recognizable next to the SELF and the package built from it
		set_target_properties(${NCINE_APP} PROPERTIES SUFFIX ".elf")

		# The PSL1GHT libraries the engine calls into. librsx and libgcm_sys are the RSX backend's whole
		# graphics stack (command FIFO plus the lv2 side of the GPU); libio is the pad/keyboard/mouse layer
		# the input backend reads; libsysutil brings the video-mode configuration and the XMB event queue the
		# window backend drives; libsysmodule loads the PRXs those need. librt is PSL1GHT's newlib syscall
		# layer (open/read/stat and the heap), so it has to come after everything that calls into libc.
		target_link_libraries(${NCINE_APP} PRIVATE
			rsx
			gcm_sys
			io
			sysutil
			sysmodule
			simdmath
			rt
			lv2
			m
		)

		# Package layout. A PS3 title is a directory tree whose USRDIR holds the boot executable, so unlike
		# the disc-image consoles above there is nothing to author - the tree is the deliverable. It is staged
		# under "pkg/" in the build directory and additionally wrapped into a .pkg, which is what a real
		# console installs; RPCS3 boots either the staged EBOOT.BIN directly or the installed package.
		#
		# APPID has to be exactly 9 characters (4 letters + 5 digits) or the firmware rejects PARAM.SFO, so
		# it is derived from the application name the same way the PS Vita title ID is: padded with zeroes
		# to the required length, and additionally uppercased because the PS3 accepts no lowercase. Appending
		# the padding before taking the first 9 characters also truncates a name that is longer. Everything
		# that has to agree on the ID - CONTENTID, PARAM.SFO and the writable path the game uses at runtime -
		# is derived from this one value; set it explicitly to override the derivation.
		if(NOT PS3_APPID)
			string(TOUPPER "${NCINE_APP}" PS3_APPID)
			string(SUBSTRING "${PS3_APPID}000000000" 0 9 PS3_APPID)
		endif()
		# Spelled out rather than using bounded repetition, which CMake's regex flavour does not support
		if(NOT PS3_APPID MATCHES "^[A-Z][A-Z][A-Z][A-Z][0-9][0-9][0-9][0-9][0-9]$")
			message(FATAL_ERROR "PS3_APPID \"${PS3_APPID}\" is not 4 letters followed by 5 digits, "
				"which the firmware requires - set it explicitly to a conforming ID")
		endif()
		set(PS3_CONTENTID "UP0001-${PS3_APPID}_00-0000000000000000")
		# The cache is the one thing that cannot go through the "/app_home" alias the content uses, because
		# that alias is read-only; it has to name the title's own game-data directory, so the ID reaches the
		# runtime rather than being written out a second time in ContentResolver
		target_compile_definitions(${NCINE_APP} PRIVATE "PS3_APPID=\"${PS3_APPID}\"")
		set(PS3_PKG_DIR "${CMAKE_BINARY_DIR}/pkg")
		file(MAKE_DIRECTORY "${PS3_PKG_DIR}/USRDIR")

		if(EXISTS "${NCINE_SOURCE_DIR}/Icons/128px.png")
			# The XMB scales whatever it is given (nominally 320x176), so the existing square icon is reused
			# instead of adding a PS3-shaped copy of it to the repository - the same trade the PSP arm makes
			set(_ps3Icon "${NCINE_SOURCE_DIR}/Icons/128px.png")
		else()
			set(_ps3Icon "${PS3_ICON0}")
		endif()

		# The content is staged as "Content" next to the EBOOT, which is where the PS3 branch of
		# ContentResolver::GetContentPath() looks for it
		set(_ps3MarkPrebakedCommand "")
		if(EXISTS "${NCINE_CONTENT_DIR}/Source.pak")
			set(_ps3MarkPrebakedCommand COMMAND ${CMAKE_COMMAND} -E rename
				"${PS3_PKG_DIR}/USRDIR/Content/Source.pak" "${PS3_PKG_DIR}/USRDIR/Content/Prebaked.pak")
		else()
			message(STATUS "No \"Source.pak\" in \"${NCINE_CONTENT_DIR}\", the package will not be marked as prebaked")
		endif()

		# sprxlinker rewrites the PRX import stubs the SDK's libraries left in the ELF into the form the lv2
		# loader resolves; make_self_npdrm then wraps the result as the EBOOT.BIN a package boots from, and
		# fself produces the unsigned SELF that RPCS3 and a CFW console will also run straight from disk.
		add_custom_command(TARGET ${NCINE_APP} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${NCINE_APP}>" "${CMAKE_BINARY_DIR}/${NCINE_APP}.stripped.elf"
			COMMAND "${CMAKE_STRIP}" "${CMAKE_BINARY_DIR}/${NCINE_APP}.stripped.elf"
			COMMAND "${PS3_SPRXLINKER}" "${CMAKE_BINARY_DIR}/${NCINE_APP}.stripped.elf"
			COMMAND "${PS3_SELF_NPDRM}" "${CMAKE_BINARY_DIR}/${NCINE_APP}.stripped.elf" "${PS3_PKG_DIR}/USRDIR/EBOOT.BIN" "${PS3_CONTENTID}"
			COMMAND "${PS3_FSELF}" "${CMAKE_BINARY_DIR}/${NCINE_APP}.stripped.elf" "${CMAKE_BINARY_DIR}/${NCINE_APP}.self"
			COMMAND "${PS3_SFO}" --title "${NCINE_APP_NAME}" --appid "${PS3_APPID}" -f "${PS3_SFO_XML}" "${PS3_PKG_DIR}/PARAM.SFO"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_ps3Icon}" "${PS3_PKG_DIR}/ICON0.PNG"
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${NCINE_CONTENT_DIR}" "${PS3_PKG_DIR}/USRDIR/Content"
			${_ps3MarkPrebakedCommand}
			COMMAND "${PS3_PKG}" --contentid "${PS3_CONTENTID}" "${PS3_PKG_DIR}/" "${CMAKE_BINARY_DIR}/${NCINE_APP}.pkg"
			COMMENT "Packaging EBOOT.BIN, SELF and NPDRM package with game content"
			VERBATIM)
	elseif(VITA)
		include("${VITASDK}/share/vita.cmake" REQUIRED)

		# Link to all required libraries and stubs
		target_link_libraries(${NCINE_APP} PRIVATE
			vitaGL
			vitashark
			SceShaccCgExt

			mathneon

			-Wl,--whole-archive # --whole-archive is required, otherwise all stubs are not linked properly

			SceLibKernel_stub
			SceAppMgr_stub
			SceAppUtil_stub
			SceAudio_stub
			SceAudioIn_stub
			SceCtrl_stub
			SceCommonDialog_stub
			SceDisplay_stub
			SceGxm_stub
			SceHid_stub
			SceHttp_stub
			SceKernelDmacMgr_stub
			SceMotion_stub
			SceNet_stub
			SceNetCtl_stub
			ScePower_stub
			SceShaccCg_stub
			SceSsl_stub
			SceSysmodule_stub
			SceTouch_stub
			taihen_stub

			-Wl,--no-whole-archive
		)
		string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" _ ${NCINE_VERSION})
		string(LENGTH ${CMAKE_MATCH_1} VITA_VERSION_MAJOR_LEN)
		if(VITA_VERSION_MAJOR_LEN EQUAL 1)
			set(VITA_VERSION "0${CMAKE_MATCH_1}")
		else()
			set(VITA_VERSION "${CMAKE_MATCH_1}")
		endif()
		string(LENGTH ${CMAKE_MATCH_2} VITA_VERSION_MINOR_LEN)
		if(VITA_VERSION_MINOR_LEN EQUAL 1)
			set(VITA_VERSION "${VITA_VERSION}.0${CMAKE_MATCH_2}")
		else()
			set(VITA_VERSION "${VITA_VERSION}.${CMAKE_MATCH_2}")
		endif()
		# The title id has to be exactly 9 characters, which is the width of the PARAM.SFO field and of the
		# "ux0:/app/<titleid>/" directory the firmware installs into. Unlike the PlayStation 3 the Vita does
		# not additionally require 4 letters followed by 5 digits - homebrew of the likes of VitaShell ships
		# as nine letters - so only the length is enforced here. Appending the padding before taking the
		# first 9 characters pads a shorter name and truncates a longer one, which the SDK does not check:
		# `vita_create_vpk()` validates the version but passes the id straight to `vita-mksfoex`.
		# Set it explicitly to override the derivation.
		if(NOT VITA_TITLEID)
			string(SUBSTRING "${NCINE_APP}000000000" 0 9 VITA_TITLEID)
		endif()
		if(NOT VITA_TITLEID MATCHES "^[A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9]$")
			message(FATAL_ERROR "VITA_TITLEID \"${VITA_TITLEID}\" is not 9 alphanumeric characters, "
				"which the firmware requires - set it explicitly to a conforming id")
		endif()
		vita_create_self(${NCINE_APP}.self ${NCINE_APP})
		# The game content travels inside the VPK, next to the executable, so it ends up in the
		# application's own read-only directory ("ux0:/app/<titleid>/", mounted as "app0:") and needs
		# no separate copy on the device.
		vita_create_vpk(${NCINE_APP}.vpk ${VITA_TITLEID} ${NCINE_APP}.self
			VERSION ${VITA_VERSION} NAME ${NCINE_APP_NAME}
			FILE "${NCINE_SOURCE_DIR}/Icons/128px.png" "sce_sys/icon0.png"
			FILE "${NCINE_CONTENT_DIR}" "Content")
	elseif(WIN32 AND NCINE_COPY_DEPENDENCIES)
		set(WIN32_DEPENDENCIES "")
		
		if(ZLIB_FOUND)
			list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/zlib.dll")
		endif()
		
		if(NCINE_WITH_ANGLE AND ANGLE_FOUND)
			list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/libEGL.dll" "${MSVC_BINDIR}/libGLESv2.dll")
		elseif(GLEW_FOUND)
			list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/glew32.dll")
		endif()
		
		if(NOT DEDICATED_SERVER)
			if(NCINE_PREFERRED_BACKEND STREQUAL "GLFW" AND GLFW_FOUND)
				list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/glfw3.dll")
			endif()
			if(NCINE_PREFERRED_BACKEND STREQUAL "SDL2" AND SDL2_FOUND)
				list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/SDL2.dll")
			endif()
			if(NCINE_PREFERRED_BACKEND STREQUAL "SDL3" AND SDL3_FOUND AND EXISTS "${MSVC_BINDIR}/SDL3.dll")
				list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/SDL3.dll")
			endif()

			if(NCINE_WITH_AUDIO AND OPENAL_FOUND)
				list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/OpenAL32.dll")

				if(NCINE_WITH_VORBIS AND VORBIS_FOUND AND NOT VORBIS_DYNAMIC_LINK)
					list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/libogg.dll" "${MSVC_BINDIR}/libvorbis.dll" "${MSVC_BINDIR}/libvorbisfile.dll")
				endif()
			
				if(NCINE_WITH_OPENMPT AND OPENMPT_FOUND AND NOT OPENMPT_DYNAMIC_LINK)
					list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/libopenmpt.dll" "${MSVC_BINDIR}/openmpt-mpg123.dll" "${MSVC_BINDIR}/openmpt-ogg.dll" "${MSVC_BINDIR}/openmpt-vorbis.dll" "${MSVC_BINDIR}/openmpt-zlib.dll")
				endif()
			endif()
		endif()
		
		if(NCINE_WITH_WEBP AND WEBP_FOUND)
			list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/libwebp.dll")
		endif()
		
		ncine_deploy_runtime_dependencies(${WIN32_DEPENDENCIES})
	endif()
	
	if(NCINE_CREATE_CONTENT_SYMLINK)
		add_custom_command(TARGET ${NCINE_APP} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E create_symlink "${NCINE_CONTENT_DIR}" "$<TARGET_FILE_DIR:${NCINE_APP}>/Content"
			COMMENT "Creating symbolic link to \"${NCINE_CONTENT_DIR}\"")
	endif()
endif()

# Jazz² Resurrection options
if(SHAREWARE_DEMO_ONLY)
	message(STATUS "Building the game only with Shareware Demo episode")
	target_compile_definitions(${NCINE_APP} PUBLIC "SHAREWARE_DEMO_ONLY")

	if(WITH_MULTIPLAYER AND SHAREWARE_DEMO_ALLOW_MULTIPLAYER)
		target_compile_definitions(${NCINE_APP} PUBLIC "SHAREWARE_DEMO_ALLOW_MULTIPLAYER")
	endif()
endif()

if(DISABLE_RESCALE_SHADERS)
	message(STATUS "Building the game with disabled rescaling options")
	target_compile_definitions(${NCINE_APP} PUBLIC "DISABLE_RESCALE_SHADERS")
endif()

if(TILEMAP_USE_SINGLE_DRAW)
	message(STATUS "Building the game with tilemap layer draw call aggregation")
	target_compile_definitions(${NCINE_APP} PUBLIC "TILEMAP_USE_SINGLE_DRAW")
endif()

if(WITH_MULTIPLAYER)
	target_compile_definitions(${NCINE_APP} PUBLIC "WITH_MULTIPLAYER")
	
	list(APPEND HEADERS
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/CtfBase.h
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/Flag.h
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/LocalPlayerOnServer.h
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/MpPlayer.h
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/PlayerOnServer.h
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/RemotablePlayer.h
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/RemoteActor.h
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/RemoteElectroShot.h
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/RemoteThunderbolt.h
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/RemotePlayerOnServer.h
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/StateInterpolationBuffer.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/ConnectionResult.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/INetworkHandler.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/MpGameMode.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/MpLevelHandler.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/NetworkManager.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/NetworkManagerBase.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/PacketTypes.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/Peer.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/PeerDescriptor.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/RaceRouteGenerator.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/Reason.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/ServerDiscovery.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/ServerInitialization.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/Teams.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/WebhookClient.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/MpPlayerState.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/IGameMode.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/GameModeFactory.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/CooperationMode.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/BattleMode.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/TeamBattleMode.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/RaceMode.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/TeamRaceMode.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/TreasureHuntMode.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/TeamTreasureHuntMode.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/CaptureTheFlagMode.h
		${NCINE_SOURCE_DIR}/Jazz2/UI/Menu/CreateLocalGameOptionsSection.h
		${NCINE_SOURCE_DIR}/Jazz2/UI/Menu/CreateServerOptionsSection.h
		${NCINE_SOURCE_DIR}/Jazz2/UI/Menu/MultiplayerGameModeSelectSection.h
		${NCINE_SOURCE_DIR}/Jazz2/UI/Menu/PlayMultiplayerSection.h
		${NCINE_SOURCE_DIR}/Jazz2/UI/Menu/ScoreboardSection.h
		${NCINE_SOURCE_DIR}/Jazz2/UI/Menu/ServerSelectSection.h
		${NCINE_SOURCE_DIR}/Jazz2/UI/Multiplayer/MpHUD.h
		${NCINE_SOURCE_DIR}/Jazz2/UI/Multiplayer/MpInGameCanvasLayer.h
		${NCINE_SOURCE_DIR}/Jazz2/UI/Multiplayer/MpInGameLobby.h
	)

	list(APPEND SOURCES
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/CtfBase.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/Flag.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/LocalPlayerOnServer.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/MpPlayer.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/PlayerOnServer.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/RemotablePlayer.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/RemoteActor.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/RemoteElectroShot.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/RemoteThunderbolt.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Actors/Multiplayer/RemotePlayerOnServer.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/ConnectionResult.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/MpLevelHandler.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/GameModeFactory.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/CooperationMode.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/BattleMode.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/TeamBattleMode.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/RaceMode.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/TreasureHuntMode.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/GameModes/CaptureTheFlagMode.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/NetworkManager.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/NetworkManagerBase.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/Peer.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/RaceRouteGenerator.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/ServerDiscovery.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/WebhookClient.cpp
		${NCINE_SOURCE_DIR}/Jazz2/UI/Menu/CreateLocalGameOptionsSection.cpp
		${NCINE_SOURCE_DIR}/Jazz2/UI/Menu/CreateServerOptionsSection.cpp
		${NCINE_SOURCE_DIR}/Jazz2/UI/Menu/MultiplayerGameModeSelectSection.cpp
		${NCINE_SOURCE_DIR}/Jazz2/UI/Menu/PlayMultiplayerSection.cpp
		${NCINE_SOURCE_DIR}/Jazz2/UI/Menu/ScoreboardSection.cpp
		${NCINE_SOURCE_DIR}/Jazz2/UI/Menu/ServerSelectSection.cpp
		${NCINE_SOURCE_DIR}/Jazz2/UI/Multiplayer/MpHUD.cpp
		${NCINE_SOURCE_DIR}/Jazz2/UI/Multiplayer/MpInGameCanvasLayer.cpp
		${NCINE_SOURCE_DIR}/Jazz2/UI/Multiplayer/MpInGameLobby.cpp
	)
	
	if(WITH_ONLINE_MULTIPLAYER)
		target_compile_definitions(${NCINE_APP} PUBLIC "WITH_ONLINE_MULTIPLAYER")
		if(DEDICATED_SERVER)
			message(STATUS "Building the game with online multiplayer support as dedicated server")
			target_compile_definitions(${NCINE_APP} PUBLIC "DEDICATED_SERVER")
		else()
			message(STATUS "Building the game with online multiplayer support")
		endif()
		
		if(NINTENDO_SWITCH)
			# Switch doesn't support IPv6 protocol, fallback to IPv4
			target_compile_definitions(${NCINE_APP} PUBLIC "ENET_IPV6=0")
		elseif(WIN32)
			# Link to IP Helper API library and Windows Sockets 2 library (only the enet transport needs them)
			target_link_libraries(${NCINE_APP} PRIVATE iphlpapi ws2_32)
		endif()
		
		if(NOT EMSCRIPTEN)
			list(APPEND HEADERS
				${NCINE_SOURCE_DIR}/Dependencies/enet/enet.h
				${NCINE_SOURCE_DIR}/Dependencies/enet/ifaddrs-android.h
			)
		endif()
		
		if(WITH_WEBSOCKET OR EMSCRIPTEN) # Emscripten always uses the browser's native WebSocket
			message(STATUS "Building the game with WebSocket transport support")
			target_compile_definitions(${NCINE_APP} PUBLIC "WITH_WEBSOCKET")

			if(EMSCRIPTEN)
				# Emscripten uses the browser's native WebSocket via <emscripten/websocket.h>
				message(STATUS "Using Emscripten native WebSocket API")
				target_link_options(${NCINE_APP} PUBLIC -sFETCH=1)
			else()
				# IXWebSocket library for all other platforms
				if(WITH_WEBSOCKET_TLS_BACKEND STREQUAL "SecureTransport" AND APPLE)
					message(STATUS "WebSocket TLS: Using SecureTransport")
					set(USE_TLS ON CACHE BOOL "" FORCE)
					set(USE_SECURE_TRANSPORT ON CACHE BOOL "" FORCE)
					set(USE_SCHANNEL OFF CACHE BOOL "" FORCE)
					set(USE_OPEN_SSL OFF CACHE BOOL "" FORCE)
					set(USE_MBED_TLS OFF CACHE BOOL "" FORCE)
					target_compile_definitions(${NCINE_APP} PUBLIC "WITH_WEBSOCKET_TLS")
				elseif(WITH_WEBSOCKET_TLS_BACKEND STREQUAL "Schannel" AND WIN32)
					message(STATUS "WebSocket TLS: Using Schannel")
					set(USE_TLS ON CACHE BOOL "" FORCE)
					set(USE_SCHANNEL ON CACHE BOOL "" FORCE)
					set(USE_SECURE_TRANSPORT OFF CACHE BOOL "" FORCE)
					set(USE_OPEN_SSL OFF CACHE BOOL "" FORCE)
					set(USE_MBED_TLS OFF CACHE BOOL "" FORCE)
					target_compile_definitions(${NCINE_APP} PUBLIC "WITH_WEBSOCKET_TLS")
				elseif(WITH_WEBSOCKET_TLS_BACKEND STREQUAL "OpenSSL")
					find_package(OpenSSL QUIET)
					if(OPENSSL_FOUND)
						message(STATUS "WebSocket TLS: Using OpenSSL")
						set(USE_TLS ON CACHE BOOL "" FORCE)
						set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
						set(USE_SCHANNEL OFF CACHE BOOL "" FORCE)
						set(USE_MBED_TLS OFF CACHE BOOL "" FORCE)
						target_compile_definitions(${NCINE_APP} PUBLIC "WITH_WEBSOCKET_TLS")

						# `NCINE_COPY_DEPENDENCIES` doesn't exist on UWP, which always deploys its dependencies
						if(WIN32 AND NOT OPENSSL_USE_STATIC_LIBS AND (NCINE_COPY_DEPENDENCIES OR WINDOWS_PHONE OR WINDOWS_STORE))
							# A dynamically linked OpenSSL needs its runtime libraries next to the executable, but
							# `FindOpenSSL` reports only the import libraries, so the DLLs have to be looked up
							set(OPENSSL_DLL_HINTS "")

							# The include directory is the most reliable anchor, because it's always directly in the
							# installation root, while the import libraries can be nested arbitrarily deep in it
							# (the official Windows builds put them in `lib/VC/x64/MD`, vcpkg in just `lib`)
							if(OPENSSL_INCLUDE_DIR)
								get_filename_component(OPENSSL_INSTALL_DIR "${OPENSSL_INCLUDE_DIR}" DIRECTORY)
								list(APPEND OPENSSL_DLL_HINTS "${OPENSSL_INSTALL_DIR}/bin" "${OPENSSL_INSTALL_DIR}")
							endif()
							if(OPENSSL_ROOT_DIR)
								list(APPEND OPENSSL_DLL_HINTS "${OPENSSL_ROOT_DIR}/bin" "${OPENSSL_ROOT_DIR}")
							endif()

							# Otherwise walk up from the import libraries, looking for a `bin` directory on the way
							foreach(OPENSSL_LIBRARY ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_LIBRARIES})
								# Skip the `optimized` and `debug` keywords of a per-configuration library list
								if(OPENSSL_LIBRARY MATCHES "\\.(lib|a|dll)$")
									get_filename_component(OPENSSL_LIBDIR "${OPENSSL_LIBRARY}" DIRECTORY)
									foreach(OPENSSL_ANCESTOR "" "/.." "/../.." "/../../.." "/../../../..")
										list(APPEND OPENSSL_DLL_HINTS
											"${OPENSSL_LIBDIR}${OPENSSL_ANCESTOR}" "${OPENSSL_LIBDIR}${OPENSSL_ANCESTOR}/bin")
									endforeach()
								endif()
							endforeach()

							# Finally, the libraries can also be dropped in with the other bundled dependencies
							if(MSVC_BINDIR)
								list(APPEND OPENSSL_DLL_HINTS "${MSVC_BINDIR}")
							endif()
							list(REMOVE_DUPLICATES OPENSSL_DLL_HINTS)

							# The official Windows builds append the architecture to the file name
							if(CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]|[Aa][Aa][Rr][Cc][Hh]")
								if(CMAKE_SIZEOF_VOID_P EQUAL 8)
									set(OPENSSL_DLL_SUFFIX "-arm64")
								else()
									set(OPENSSL_DLL_SUFFIX "-arm")
								endif()
							elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
								set(OPENSSL_DLL_SUFFIX "-x64")
							else()
								set(OPENSSL_DLL_SUFFIX "")
							endif()

							# Only the directories of the found OpenSSL are searched, a DLL from an unrelated
							# installation in `PATH` (e.g. the one bundled with Git) wouldn't match the import libraries
							find_file(OPENSSL_SSL_DLL
								NAMES "libssl-3${OPENSSL_DLL_SUFFIX}.dll" "libssl-3.dll"
									"libssl-1_1${OPENSSL_DLL_SUFFIX}.dll" "libssl-1_1.dll" "libssl.dll" "ssleay32.dll"
								PATHS ${OPENSSL_DLL_HINTS} NO_DEFAULT_PATH)
							find_file(OPENSSL_CRYPTO_DLL
								NAMES "libcrypto-3${OPENSSL_DLL_SUFFIX}.dll" "libcrypto-3.dll"
									"libcrypto-1_1${OPENSSL_DLL_SUFFIX}.dll" "libcrypto-1_1.dll" "libcrypto.dll" "libeay32.dll"
								PATHS ${OPENSSL_DLL_HINTS} NO_DEFAULT_PATH)
							mark_as_advanced(OPENSSL_SSL_DLL OPENSSL_CRYPTO_DLL)

							if(OPENSSL_SSL_DLL AND OPENSSL_CRYPTO_DLL)
								message(STATUS "OpenSSL runtime libraries: ${OPENSSL_SSL_DLL}, ${OPENSSL_CRYPTO_DLL}")
								# On UWP the libraries have to come from a UWP build of OpenSSL to be deployable
								ncine_deploy_runtime_dependencies(${OPENSSL_SSL_DLL} ${OPENSSL_CRYPTO_DLL})
							else()
								string(REPLACE ";" "\n  " OPENSSL_SEARCHED_DIRS "${OPENSSL_DLL_HINTS}")
								message(WARNING "OpenSSL runtime libraries not found, set OPENSSL_SSL_DLL and "
									"OPENSSL_CRYPTO_DLL manually or copy them next to the executable. Searched in:\n  "
									"${OPENSSL_SEARCHED_DIRS}")
							endif()
						endif()
					else()
						message(WARNING "OpenSSL not found, building WebSocket without TLS support")
						set(USE_TLS OFF CACHE BOOL "" FORCE)
					endif()
				elseif(WITH_WEBSOCKET_TLS_BACKEND STREQUAL "mbedTLS")
					find_package(MbedTLS QUIET)
					if(MbedTLS_FOUND OR MBEDTLS_FOUND)
						message(STATUS "WebSocket TLS: Using mbedTLS")
						set(USE_TLS ON CACHE BOOL "" FORCE)
						set(USE_OPEN_SSL OFF CACHE BOOL "" FORCE)
						set(USE_SCHANNEL OFF CACHE BOOL "" FORCE)
						set(USE_MBED_TLS ON CACHE BOOL "" FORCE)
						target_compile_definitions(${NCINE_APP} PUBLIC "WITH_WEBSOCKET_TLS")
					else()
						message(WARNING "mbedTLS not found, building WebSocket without TLS support")
						set(USE_TLS OFF CACHE BOOL "" FORCE)
					endif()
				else()
					message(STATUS "WebSocket TLS: Disabled")
					set(USE_TLS OFF CACHE BOOL "" FORCE)
				endif()

				find_package(IXWebSocket REQUIRED)
				target_link_libraries(${NCINE_APP} PRIVATE IXWebSocket)
			endif()
		endif()
	else()
		message(STATUS "Building the game with local multiplayer support")
	endif()
else()
	message(STATUS "Building the game without multiplayer support")
endif()
