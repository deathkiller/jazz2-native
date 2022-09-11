if(WIN32)
#	list(APPEND HEADERS ${NCINE_ROOT}/include/ncine/common_windefines.h)
#	list(APPEND SOURCES ${NCINE_ROOT}/src/FileLoggerWindows.cpp)
#	if(NOT MINGW)
#		list(APPEND SOURCES ${NCINE_ROOT}/src/base/WindowsAtomic.cpp)
#	else()
#		list(APPEND SOURCES ${NCINE_ROOT}/src/base/GccAtomic.cpp)
#	endif()
elseif(APPLE)
#	list(APPEND SOURCES ${NCINE_ROOT}/src/base/StdAtomic.cpp)
else()
#	if(ATOMIC_FOUND)
#		target_link_libraries(ncine PRIVATE Atomic::Atomic)
#	endif()
#	list(APPEND SOURCES ${NCINE_ROOT}/src/base/GccAtomic.cpp)
endif()

if(EMSCRIPTEN)
	list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/IO/EmscriptenLocalFile.h)
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/IO/EmscriptenLocalFile.cpp)
endif()

if(ANGLE_FOUND OR OPENGLES2_FOUND)
	target_compile_definitions(ncine PRIVATE "WITH_OPENGLES")
	target_link_libraries(ncine PRIVATE EGL::EGL OpenGLES2::GLES2)

	if(ANGLE_FOUND)
		message(STATUS "ANGLE has been found")
		target_compile_definitions(ncine PRIVATE "WITH_ANGLE")
	endif()

	list(APPEND PRIVATE_HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPkm.h)
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPkm.cpp)
endif()

if(GLEW_FOUND)
	if (NOT WIN32)
		message(STATUS "GLEW has been found")
	endif()
	target_compile_definitions(ncine PRIVATE "WITH_GLEW")
	target_link_libraries(ncine PRIVATE GLEW::GLEW)
endif()

if(GLFW_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "GLFW")
	target_compile_definitions(ncine PRIVATE "WITH_GLFW")
	target_link_libraries(ncine PRIVATE GLFW::GLFW)

	list(APPEND PRIVATE_HEADERS
		${NCINE_SOURCE_DIR}/nCine/Input/GlfwInputManager.h
		${NCINE_SOURCE_DIR}/nCine/Graphics/GL/GlfwGfxDevice.h
	)
	list(APPEND SOURCES
		${NCINE_SOURCE_DIR}/nCine/Input/GlfwInputManager.cpp
		${NCINE_SOURCE_DIR}/nCine/Input/GlfwKeys.cpp
		${NCINE_SOURCE_DIR}/nCine/Graphics/GL/GlfwGfxDevice.cpp
	)
elseif(SDL2_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "SDL2")
	target_compile_definitions(ncine PRIVATE "WITH_SDL")
	target_link_libraries(ncine PRIVATE SDL2::SDL2)

	list(APPEND PRIVATE_HEADERS
		${NCINE_SOURCE_DIR}/nCine/Input/SdlInputManager.h
		${NCINE_SOURCE_DIR}/nCine/Graphics/GL/SdlGfxDevice.h
	)
	list(APPEND SOURCES
		${NCINE_SOURCE_DIR}/nCine/Input/SdlInputManager.cpp
		${NCINE_SOURCE_DIR}/nCine/Input/SdlKeys.cpp
		${NCINE_SOURCE_DIR}/nCine/Graphics/GL/SdlGfxDevice.cpp
	)
elseif(Qt5_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "QT5")
	target_compile_definitions(ncine PRIVATE "WITH_QT5")
	target_link_libraries(ncine PUBLIC Qt5::Widgets)
	if(Qt5Gamepad_FOUND)
		target_compile_definitions(ncine PRIVATE "WITH_QT5GAMEPAD")
		target_link_libraries(ncine PRIVATE Qt5::Gamepad)
	endif()

	list(APPEND HEADERS	${NCINE_SOURCE_DIR}/nCine/Qt5Widget.h)
	qt5_wrap_cpp(MOC_SOURCES ${NCINE_SOURCE_DIR}/nCine/Qt5Widget.h)

	list(APPEND PRIVATE_HEADERS
		${NCINE_SOURCE_DIR}/nCine/Input/Qt5InputManager.h
		${NCINE_SOURCE_DIR}/nCine/Graphics/GL/Qt5GfxDevice.h
	)
	list(APPEND SOURCES
		${NCINE_SOURCE_DIR}/nCine/Qt5Widget.cpp
		${NCINE_SOURCE_DIR}/nCine/Input/Qt5InputManager.cpp
		${NCINE_SOURCE_DIR}/nCine/Input/Qt5Keys.cpp
		${NCINE_SOURCE_DIR}/nCine/Graphics/GL/Qt5GfxDevice.cpp
		${MOC_SOURCES}
	)

	list(REMOVE_ITEM SOURCES ${NCINE_SOURCE_DIR}/nCine/Input/JoyMapping.cpp)
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Input/Qt5JoyMapping.cpp)
endif()

if(OPENAL_FOUND)
	target_compile_definitions(ncine PRIVATE "WITH_AUDIO")
	target_link_libraries(ncine PRIVATE OpenAL::AL)

	list(APPEND HEADERS
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioBuffer.h
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioStream.h
		${NCINE_SOURCE_DIR}/nCine/Audio/IAudioPlayer.h
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioBufferPlayer.h
		${NCINE_SOURCE_DIR}/nCine/Audio/AudioStreamPlayer.h
	)

	list(APPEND PRIVATE_HEADERS
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
		target_compile_definitions(ncine PRIVATE "WITH_VORBIS")
		target_link_libraries(ncine PRIVATE Vorbis::Vorbisfile)

		list(APPEND PRIVATE_HEADERS
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderOgg.h
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderOgg.h)

		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderOgg.cpp
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderOgg.cpp)
	endif()
	
	if(OPENMPT_FOUND)
		target_compile_definitions(ncine PRIVATE "WITH_OPENMPT")
		target_link_libraries(ncine PRIVATE libopenmpt::libopenmpt)
		
		list(APPEND PRIVATE_HEADERS
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderMpt.h
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderMpt.h)

		list(APPEND SOURCES
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioLoaderMpt.cpp
			${NCINE_SOURCE_DIR}/nCine/Audio/AudioReaderMpt.cpp)
	endif()
endif()

#if(PNG_FOUND)
#	target_compile_definitions(ncine PRIVATE "WITH_PNG")
#	target_link_libraries(ncine PRIVATE PNG::PNG)

#	list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureSaverPng.h)
#	list(APPEND PRIVATE_HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPng.h)
#	list(APPEND SOURCES
#		${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPng.cpp
#		${NCINE_SOURCE_DIR}/nCine/Graphics/TextureSaverPng.cpp)
#endif()
#if(WEBP_FOUND)
#	target_compile_definitions(ncine PRIVATE "WITH_WEBP")
#	target_link_libraries(ncine PRIVATE WebP::WebP)

#	list(APPEND HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureSaverWebP.h)
#	list(APPEND PRIVATE_HEADERS ${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderWebP.h)
#	list(APPEND SOURCES
#		${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderWebP.cpp
#		${NCINE_SOURCE_DIR}/nCine/Graphics/TextureSaverWebP.cpp)
#endif()

if(Threads_FOUND)
	target_compile_definitions(ncine PRIVATE "WITH_THREADS")
	target_link_libraries(ncine PRIVATE Threads::Threads)

	list(APPEND PRIVATE_HEADERS
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

	list(APPEND PRIVATE_HEADERS ${NCINE_SOURCE_DIR}/nCine/Threading/ThreadPool.h)
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/Threading/ThreadPool.cpp)
	list(APPEND PRIVATE_HEADERS ${NCINE_SOURCE_DIR}/nCine/Threading/ThreadCommands.h)
endif()

#if(LUA_FOUND)
#	target_compile_definitions(ncine PRIVATE "WITH_LUA")
#	target_link_libraries(ncine PRIVATE Lua::Lua)

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
#		target_compile_definitions(ncine PRIVATE "WITH_SCRIPTING_API")

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
#	target_compile_definitions(ncine PRIVATE "WITH_ALLOCATORS")
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

#if(NCINE_WITH_IMGUI)
#	target_compile_definitions(ncine PRIVATE "WITH_IMGUI")
#
#	# For external projects compiling using an nCine build directory
#	set(IMGUI_INCLUDE_ONLY_DIR ${IMGUI_SOURCE_DIR}/include_only)
#	file(COPY ${IMGUI_SOURCE_DIR}/imgui.h DESTINATION ${IMGUI_INCLUDE_ONLY_DIR}/ncine)
#	file(COPY ${IMGUI_SOURCE_DIR}/imconfig.h DESTINATION ${IMGUI_INCLUDE_ONLY_DIR}/ncine)
#
#	list(APPEND HEADERS
#		${IMGUI_INCLUDE_ONLY_DIR}/ncine/imgui.h
#		${IMGUI_INCLUDE_ONLY_DIR}/ncine/imconfig.h
#	)
#
#	list(APPEND PRIVATE_HEADERS
#		${IMGUI_SOURCE_DIR}/imgui_internal.h
#		${IMGUI_SOURCE_DIR}/imstb_rectpack.h
#		${IMGUI_SOURCE_DIR}/imstb_textedit.h
#		${IMGUI_SOURCE_DIR}/imstb_truetype.h
#		${NCINE_ROOT}/src/include/ImGuiDrawing.h
#		${NCINE_ROOT}/src/include/ImGuiJoyMappedInput.h
#	)
#
#	list(APPEND SOURCES
#		${IMGUI_SOURCE_DIR}/imgui.cpp
#		${IMGUI_SOURCE_DIR}/imgui_demo.cpp
#		${IMGUI_SOURCE_DIR}/imgui_draw.cpp
#		${IMGUI_SOURCE_DIR}/imgui_tables.cpp
#		${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
#		${NCINE_ROOT}/src/graphics/ImGuiDrawing.cpp
#		${NCINE_ROOT}/src/input/ImGuiJoyMappedInput.cpp
#	)
#
#	if(GLFW_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "GLFW")
#		list(APPEND PRIVATE_HEADERS ${NCINE_ROOT}/src/include/ImGuiGlfwInput.h)
#		list(APPEND SOURCES ${NCINE_ROOT}/src/input/ImGuiGlfwInput.cpp)
#	elseif(SDL2_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "SDL2")
#		list(APPEND PRIVATE_HEADERS ${NCINE_ROOT}/src/include/ImGuiSdlInput.h)
#		list(APPEND SOURCES ${NCINE_ROOT}/src/input/ImGuiSdlInput.cpp)
#	elseif(Qt5_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "QT5")
#		list(APPEND PRIVATE_HEADERS ${NCINE_ROOT}/src/include/ImGuiQt5Input.h)
#		list(APPEND SOURCES ${NCINE_ROOT}/src/input/ImGuiQt5Input.cpp)
#	elseif(ANDROID)
#		list(APPEND PRIVATE_HEADERS ${NCINE_ROOT}/src/include/ImGuiAndroidInput.h)
#		list(APPEND SOURCES ${NCINE_ROOT}/src/android/ImGuiAndroidInput.cpp)
#	endif()
#
#	list(APPEND PRIVATE_HEADERS ${NCINE_ROOT}/src/include/ImGuiDebugOverlay.h)
#	list(APPEND SOURCES ${NCINE_ROOT}/src/graphics/ImGuiDebugOverlay.cpp)
#
#	if(MINGW)
#		target_link_libraries(ncine PRIVATE imm32 dwmapi)
#	endif()
#endif()

#if(NCINE_WITH_NUKLEAR)
#	target_compile_definitions(ncine PRIVATE "WITH_NUKLEAR")
#
#	# For external projects compiling using an nCine build directory
#	set(NUKLEAR_INCLUDE_ONLY_DIR ${NUKLEAR_SOURCE_DIR}/include_only)
#	file(COPY ${NUKLEAR_SOURCE_DIR}/nuklear.h DESTINATION ${NUKLEAR_INCLUDE_ONLY_DIR}/ncine)
#
#	list(APPEND HEADERS
#		${NUKLEAR_INCLUDE_ONLY_DIR}/ncine/nuklear.h
#		${NCINE_ROOT}/include/ncine/NuklearContext.h
#	)
#
#	list(APPEND PRIVATE_HEADERS
#		${NCINE_ROOT}/src/include/NuklearDrawing.h
#	)
#
#	list(APPEND SOURCES
#		${NCINE_ROOT}/src/NuklearContext.cpp
#		${NCINE_ROOT}/src/graphics/NuklearDrawing.cpp
#	)

#	if(GLFW_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "GLFW")
#		list(APPEND PRIVATE_HEADERS ${NCINE_ROOT}/src/include/NuklearGlfwInput.h)
#		list(APPEND SOURCES ${NCINE_ROOT}/src/input/NuklearGlfwInput.cpp)
#	elseif(SDL2_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "SDL2")
#		list(APPEND PRIVATE_HEADERS ${NCINE_ROOT}/src/include/NuklearSdlInput.h)
#		list(APPEND SOURCES ${NCINE_ROOT}/src/input/NuklearSdlInput.cpp)
#	elseif(Qt5_FOUND AND NCINE_PREFERRED_BACKEND STREQUAL "QT5")
#		list(APPEND PRIVATE_HEADERS ${NCINE_ROOT}/src/include/NuklearQt5Input.h)
#		list(APPEND SOURCES ${NCINE_ROOT}/src/input/NuklearQt5Input.cpp)
#	elseif(ANDROID)
#		list(APPEND PRIVATE_HEADERS ${NCINE_ROOT}/src/include/NuklearAndroidInput.h)
#		list(APPEND SOURCES ${NCINE_ROOT}/src/android/NuklearAndroidInput.cpp)
#	endif()
#endif()

if(NCINE_WITH_TRACY)
	target_compile_definitions(ncine PRIVATE "WITH_TRACY")
	if(NOT ANDROID AND NOT APPLE AND NOT EMSCRIPTEN)
		target_compile_definitions(ncine PRIVATE "WITH_TRACY_OPENGL")
	endif()
	target_compile_definitions(ncine PUBLIC "TRACY_ENABLE")
	target_compile_definitions(ncine PRIVATE "TRACY_DELAYED_INIT")

	# For external projects compiling using an nCine build directory
	set(TRACY_INCLUDE_ONLY_DIR ${TRACY_SOURCE_DIR}/include_only)
	file(GLOB TRACY_ROOT_HPP "${TRACY_SOURCE_DIR}/*.hpp" "${TRACY_SOURCE_DIR}/*.h")
	file(COPY ${TRACY_ROOT_HPP} DESTINATION ${TRACY_INCLUDE_ONLY_DIR}/tracy)
	file(GLOB TRACY_COMMON_HPP "${TRACY_SOURCE_DIR}/common/*.hpp" "${TRACY_SOURCE_DIR}/common/*.h")
	file(COPY ${TRACY_COMMON_HPP} DESTINATION ${TRACY_INCLUDE_ONLY_DIR}/tracy/common)
	file(COPY "${TRACY_SOURCE_DIR}/common/TracySystem.cpp" DESTINATION ${TRACY_INCLUDE_ONLY_DIR}/tracy/common)
	file(GLOB TRACY_CLIENT_HPP "${TRACY_SOURCE_DIR}/client/*.hpp" "${TRACY_SOURCE_DIR}/client/*.h")
	file(COPY ${TRACY_CLIENT_HPP} DESTINATION ${TRACY_INCLUDE_ONLY_DIR}/tracy/client)
	#file(COPY "${TRACY_SOURCE_DIR}/LICENSE" DESTINATION ${TRACY_INCLUDE_ONLY_DIR}/tracy)

	list(APPEND HEADERS
		${NCINE_SOURCE_DIR}/nCine/tracy.h
		${NCINE_SOURCE_DIR}/nCine/tracy_opengl.h
	)

	list(APPEND SOURCES
		${NCINE_SOURCE_DIR}/nCine/tracy_memory.cpp
		${TRACY_SOURCE_DIR}/TracyClient.cpp
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
#	target_include_directories(ncine PRIVATE ${RENDERDOC_INCLUDE_DIR})
#
#	target_compile_definitions(ncine PRIVATE "WITH_RENDERDOC")
#	if(UNIX)
#		target_link_libraries(ncine PRIVATE dl)
#	endif()
#
#	list(APPEND HEADERS ${NCINE_ROOT}/include/ncine/RenderDocCapture.h)
#	list(APPEND PRIVATE_HEADERS ${RENDERDOC_API_H})
#	list(APPEND SOURCES ${NCINE_ROOT}/src/graphics/RenderDocCapture.cpp)
#endif()

if(ZLIB_FOUND)
	target_compile_definitions(ncine PRIVATE "WITH_ZLIB")
	target_link_libraries(ncine PRIVATE ZLIB::ZLIB)
endif()

if(LIBDEFLATE_FOUND)
	target_link_libraries(ncine PRIVATE libdeflate::libdeflate)
endif()

if(NCINE_BUILD_ANDROID)
	list(APPEND HEADERS
		${NCINE_SOURCE_DIR}/nCine/Android/AndroidApplication.h
		${NCINE_SOURCE_DIR}/nCine/IO/AssetFile.h
	)
endif()

if(ANDROID)
	list(APPEND PRIVATE_HEADERS
		${NCINE_SOURCE_DIR}/nCine/Android/AndroidInputManager.h
		${NCINE_SOURCE_DIR}/nCine/Android/AndroidJniHelper.h
		${NCINE_SOURCE_DIR}/nCine/Android/EglGfxDevice.h
		${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPkm.h
	)

	list(APPEND SOURCES
		${NCINE_SOURCE_DIR}/nCine/Android/AndroidApplication.cpp
		${NCINE_SOURCE_DIR}/nCine/Android/AndroidInputManager.cpp
		${NCINE_SOURCE_DIR}/nCine/Android/AndroidJniHelper.cpp
		${NCINE_SOURCE_DIR}/nCine/Android/AndroidKeys.cpp
		${NCINE_SOURCE_DIR}/nCine/IO/AssetFile.cpp
		${NCINE_SOURCE_DIR}/nCine/Android/EglGfxDevice.cpp
		${NCINE_SOURCE_DIR}/nCine/Graphics/TextureLoaderPkm.cpp
	)
else()
	list(APPEND SOURCES ${NCINE_SOURCE_DIR}/nCine/PCApplication.cpp)
endif()

# Jazz² Resurrection options
if(SHAREWARE_DEMO_ONLY)
	message(STATUS "Building the game only with Shareware Demo episode")
	target_compile_definitions(ncine PUBLIC "SHAREWARE_DEMO_ONLY")
endif()
