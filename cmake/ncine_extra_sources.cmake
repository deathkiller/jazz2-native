if(WIN32)
	if(NOT MINGW)
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Threading/WindowsAtomic.cpp)
	else()
		list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Threading/GccAtomic.cpp)
	endif()
elseif(APPLE)
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Threading/StdAtomic.cpp)
else()
	if(ATOMIC_FOUND)
		target_link_libraries(${NCINE_APP} PRIVATE Atomic::Atomic)
	endif()
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Threading/GccAtomic.cpp)
endif()

if(EMSCRIPTEN)
	list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/IO/EmscriptenLocalFile.h)
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/IO/EmscriptenLocalFile.cpp)
endif()

if(ANGLE_FOUND OR OPENGLES2_FOUND)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_OPENGLES")
	target_link_libraries(${NCINE_APP} PRIVATE EGL::EGL OpenGLES2::GLES2)

	if(ANGLE_FOUND)
		message(STATUS "ANGLE has been found")
		target_compile_definitions(${NCINE_APP} PRIVATE "WITH_ANGLE")
	endif()

	list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPkm.h)
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPkm.cpp)
endif()

if(GLEW_FOUND)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_GLEW")
	target_link_libraries(${NCINE_APP} PRIVATE GLEW::GLEW)
endif()

if(GLFW_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "GLFW")
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
elseif(SDL2_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "SDL2")
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_SDL")
	target_link_libraries(${NCINE_APP} PRIVATE SDL2::SDL2)

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

	qt5_wrap_cpp(MOC_SOURCES ${NCINE_SOURCE_DIR}/nCine/Qt5Widget.h)

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

	list(REMOVE_ITEM SOURCES ${NCINE_SOURCE_DIR}/nCine/Input/JoyMapping.cpp)
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Backends/Qt5JoyMapping.cpp)
endif()

if(OPENAL_FOUND)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_AUDIO")
	target_link_libraries(${NCINE_APP} PRIVATE OpenAL::OpenAL)

	list(APPEND HEADERS
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioBuffer.h
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioStream.h
		${NCINE_SOURCE_DIR}/nCine/Audio/IAudioPlayer.h
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioBufferPlayer.h
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioStreamPlayer.h
		${NCINE_SOURCE_DIR}/nCine/Audio/ALAudioDevice.h
		${NCINE_SOURCE_DIR}/nCine/Audio/IAudioLoader.h
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderWav.h
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderWav.h
		${NCINE_SOURCE_DIR}/nCine/Audio/IAudioReader.h
	)

	list(APPEND SOURCES
		${NCINE_SOURCE_DIR}/nCine/Audio/ALAudioDevice.cpp
		${NCINE_SOURCE_DIR}/nCine/Audio/IAudioLoader.cpp
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderWav.cpp
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderWav.cpp
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioBuffer.cpp
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioStream.cpp
		${NCINE_SOURCE_DIR}/nCine/Audio/IAudioPlayer.cpp
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioBufferPlayer.cpp
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioStreamPlayer.cpp
	)

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
	endif()
elseif(NOT NCINE_BUILD_ANDROID)
	message(STATUS "Cannot find OpenAL library")
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

if(Threads_FOUND)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_THREADS")
	target_link_libraries(${NCINE_APP} PRIVATE Threads::Threads)

	list(APPEND HEADERS
		${NCINE_SOURCE_DIR}/nCine/Threading/Thread.h
		${NCINE_SOURCE_DIR}/nCine/Threading/ThreadSync.h
	)

	if(WIN32)
		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Threading/WindowsThread.cpp
			${NCINE_SOURCE_DIR}/nCine/Threading/WindowsThreadSync.cpp
		)
	else()
		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Threading/PosixThread.cpp
			${NCINE_SOURCE_DIR}/nCine/Threading/PosixThreadSync.cpp
		)
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
	target_link_libraries(${NCINE_APP} PRIVATE Angelscript::Angelscript)
endif()

if(NCINE_WITH_IMGUI)
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
	elseif(SDL2_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "SDL2")
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

if(LIBDEFLATE_FOUND)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_LIBDEFLATE")
	target_link_libraries(${NCINE_APP} PRIVATE libdeflate::libdeflate)
elseif(ZLIB_FOUND)
	target_compile_definitions(${NCINE_APP} PRIVATE "WITH_ZLIB")
	target_link_libraries(${NCINE_APP} PRIVATE ZLIB::ZLIB)
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
	if (NCINE_VERSION_FROM_GIT AND GIT_NO_TAG)
		set(PACKAGE_VERSION_PATCH "0")
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
		"${MSVC_WINRT_BINDIR}/libEGL.dll"
		"${MSVC_WINRT_BINDIR}/libGLESv2.dll"
	)
		
	if(LIBDEFLATE_FOUND)
		list(APPEND UWP_DEPENDENCIES "${MSVC_BINDIR}/libdeflate.dll")
	elseif(ZLIB_FOUND)
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

	target_sources(${NCINE_APP} PRIVATE ${UWP_DEPENDENCIES})
	set_property(SOURCE ${UWP_DEPENDENCIES} PROPERTY VS_DEPLOYMENT_CONTENT 1)
	set_property(SOURCE ${UWP_DEPENDENCIES} PROPERTY VS_DEPLOYMENT_LOCATION ".")
	source_group("Dependencies" FILES ${UWP_DEPENDENCIES})
	
	# Include `Content` directory
	file(GLOB_RECURSE PACKAGE_CONTENT_FILES "${NCINE_DATA_DIR}/*")
	foreach(CONTENT_FILE ${PACKAGE_CONTENT_FILES})
		# Preserving directory structure
		file(RELATIVE_PATH CONTENT_FILE_RELPATH ${NCINE_DATA_DIR} ${CONTENT_FILE})
		get_filename_component(CONTENT_FILE_RELPATH ${CONTENT_FILE_RELPATH} DIRECTORY)
		
		target_sources(${NCINE_APP} PRIVATE ${CONTENT_FILE})
		set_property(SOURCE ${CONTENT_FILE} PROPERTY VS_DEPLOYMENT_CONTENT 1)
		set_property(SOURCE ${CONTENT_FILE} PROPERTY VS_DEPLOYMENT_LOCATION "Content/${CONTENT_FILE_RELPATH}")
		source_group("Content/${CONTENT_FILE_RELPATH}" FILES ${CONTENT_FILE})
	endforeach()
else()
	list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/MainApplication.h)
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/MainApplication.cpp)

	if(NINTENDO_SWITCH)
		nx_generate_nacp(${NCINE_APP}.nacp
			NAME "${NCINE_APP_NAME}"
			AUTHOR "${NCINE_APP_VENDOR}"
			VERSION "${NCINE_VERSION}"
		)

		nx_create_nro(${NCINE_APP}
			NACP "${NCINE_APP}.nacp"
			ICON "${NCINE_SOURCE_DIR}/Icons/256px.png"
			ROMFS "${NCINE_DATA_DIR}"
		)
	elseif(WIN32 AND NCINE_COPY_DEPENDENCIES)
		set(WIN32_DEPENDENCIES "")

		if(NCINE_WITH_ANGLE AND ANGLE_FOUND)
			list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/libEGL.dll" "${MSVC_BINDIR}/libGLESv2.dll")
		elseif(GLEW_FOUND)
			list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/glew32.dll")
		endif()
		
		if(NCINE_PREFERRED_BACKEND STREQUAL "GLFW" AND GLFW_FOUND)
			list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/glfw3.dll")
		endif()
		if(NCINE_PREFERRED_BACKEND STREQUAL "SDL2" AND SDL2_FOUND)
			list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/SDL2.dll")
		endif()
		
		if(LIBDEFLATE_FOUND)
			list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/libdeflate.dll")
		elseif(ZLIB_FOUND)
			list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/zlib.dll")
		endif()

		if(NCINE_WITH_WEBP AND WEBP_FOUND)
			list(APPEND WIN32_DEPENDENCIES "${MSVC_BINDIR}/libwebp.dll")
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
		
		foreach(DEPENDENCY ${WIN32_DEPENDENCIES})
			add_custom_command(TARGET ${NCINE_APP} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DEPENDENCY} $<TARGET_FILE_DIR:${NCINE_APP}>
				VERBATIM)
		endforeach()
	endif()
endif()

# Jazz² Resurrection options
if(SHAREWARE_DEMO_ONLY)
	message(STATUS "Building the game only with Shareware Demo episode")
	target_compile_definitions(${NCINE_APP} PUBLIC "SHAREWARE_DEMO_ONLY")
endif()

if(DISABLE_RESCALE_SHADERS)
	message(STATUS "Building the game with disabled rescaling options")
	target_compile_definitions(${NCINE_APP} PUBLIC "DISABLE_RESCALE_SHADERS")
endif()

if(WITH_MULTIPLAYER)
	message(STATUS "Building the game with multiplayer support")
	target_compile_definitions(${NCINE_APP} PUBLIC "WITH_MULTIPLAYER")
	
	list(APPEND HEADERS
		${NCINE_SOURCE_DIR}/Jazz2/Actors/RemoteActor.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/ConnectionResult.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/INetworkHandler.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/MultiLevelHandler.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/NetworkManager.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/PacketTypes.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/Peer.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/Reason.h
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/Backends/enet.h
	)

	list(APPEND SOURCES
		${NCINE_SOURCE_DIR}/Jazz2/Actors/RemoteActor.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/ConnectionResult.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/MultiLevelHandler.cpp
		${NCINE_SOURCE_DIR}/Jazz2/Multiplayer/NetworkManager.cpp
	)
endif()
