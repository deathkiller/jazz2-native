if(NCINE_DOWNLOAD_DEPENDENCIES AND NOT EMSCRIPTEN AND NOT NINTENDO_SWITCH)
	if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.14.0")
		if(APPLE)
			if(NCINE_ARM_PROCESSOR)
				set(NCINE_LIBS_URL "https://github.com/deathkiller/jazz2-libraries/raw/2.0.1-macos/jazz2-libraries-macos-arm64.tar.gz")
			else()
				set(NCINE_LIBS_URL "https://github.com/deathkiller/jazz2-libraries/raw/2.0.1-macos/jazz2-libraries-macos.tar.gz")
			endif()
		else()
			set(NCINE_LIBS_URL "https://github.com/deathkiller/jazz2-libraries/archive/2.8.0.tar.gz")
		endif()
		message(STATUS "Downloading dependencies from \"${NCINE_LIBS_URL}\"...")

		include(FetchContent)
		FetchContent_Declare(
			ncine_libraries
			DOWNLOAD_EXTRACT_TIMESTAMP TRUE
			URL ${NCINE_LIBS_URL}
		)
		FetchContent_MakeAvailable(ncine_libraries)

		set(NCINE_LIBS ${ncine_libraries_SOURCE_DIR})
	else()
		message(STATUS "Cannot download dependencies, CMake 3.14.0 or newer is required, please download dependencies manually to `Libs` directory")
		set(NCINE_LIBS "${NCINE_ROOT}/Libs/")
	endif()
elseif(NOT NCINE_LIBS)
	set(NCINE_LIBS "${NCINE_ROOT}/Libs/")
endif()

set(EXTERNAL_INCLUDES_DIR "${NCINE_LIBS}/Includes/" CACHE PATH "Set the path to external header files")

if(NCINE_WITH_BACKWARD)
	add_subdirectory("${NCINE_SOURCE_DIR}/backward/")
endif()

if(EMSCRIPTEN)
	set(EXTERNAL_EMSCRIPTEN_DIR "${NCINE_LIBS}/Emscripten/" CACHE PATH "Set the path to the Emscripten libraries directory")
	if(IS_DIRECTORY ${EXTERNAL_EMSCRIPTEN_DIR})
		message(STATUS "Emscripten libraries directory: ${EXTERNAL_EMSCRIPTEN_DIR}")
	endif()

	if(NCINE_WITH_THREADS)
		add_library(Threads::Threads INTERFACE IMPORTED)
		set_target_properties(Threads::Threads PROPERTIES
			INTERFACE_COMPILE_OPTIONS "SHELL:-pthread"
			INTERFACE_LINK_OPTIONS "SHELL:-pthread -s PTHREAD_POOL_SIZE=4 -s WASM_MEM_MAX=128MB")
		set(Threads_FOUND 1)
	endif()

	add_library(ZLIB::ZLIB INTERFACE IMPORTED)
	set_target_properties(ZLIB::ZLIB PROPERTIES
		INTERFACE_COMPILE_OPTIONS "SHELL:-sUSE_ZLIB=1"
		INTERFACE_LINK_OPTIONS "SHELL:-sUSE_ZLIB=1")
	set(ZLIB_FOUND 1)

	add_library(OpenGL::GL INTERFACE IMPORTED)
	set_target_properties(OpenGL::GL PROPERTIES
		INTERFACE_LINK_OPTIONS "SHELL:-s USE_WEBGL2=1 -s FULL_ES3=1 -s FULL_ES2=1")
	set(OPENGL_FOUND 1)

	if(NCINE_PREFERRED_BACKEND STREQUAL "GLFW")
		add_library(GLFW::GLFW INTERFACE IMPORTED)
		#set_target_properties(GLFW::GLFW PROPERTIES
		#	INTERFACE_LINK_OPTIONS "SHELL:-s USE_GLFW=3")
		
		# Newer GLFW implementation supported since Emscripten 3.1.55
		set_target_properties(GLFW::GLFW PROPERTIES
			INTERFACE_LINK_OPTIONS "SHELL:--use-port=contrib.glfw3")
		set(GLFW_FOUND 1)
	elseif(NCINE_PREFERRED_BACKEND STREQUAL "SDL2")
		add_library(SDL2::SDL2 INTERFACE IMPORTED)
		set_target_properties(SDL2::SDL2 PROPERTIES
			INTERFACE_COMPILE_OPTIONS "SHELL:-s USE_SDL=2"
			INTERFACE_LINK_OPTIONS "SHELL:-s USE_SDL=2")
		set(SDL2_FOUND 1)
	endif()

	#if(NCINE_WITH_PNG)
	#	add_library(PNG::PNG INTERFACE IMPORTED)
	#	set_target_properties(PNG::PNG PROPERTIES
	#		INTERFACE_COMPILE_OPTIONS "SHELL:-s USE_LIBPNG=1"
	#		INTERFACE_LINK_OPTIONS "SHELL:-s USE_LIBPNG=1")
	#	set(PNG_FOUND 1)
	#endif()

	if(NCINE_WITH_WEBP)
		add_library(WebP::WebP STATIC IMPORTED)
		set_target_properties(WebP::WebP PROPERTIES
			IMPORTED_LOCATION "${EXTERNAL_EMSCRIPTEN_DIR}/libwebp.a"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}")
		if(EXISTS ${EXTERNAL_EMSCRIPTEN_DIR}/libsharpyuv.a)
			# Since libwebp 1.3.0, libwebp.a needs some symbols from libsharpyuv.a
			add_library(WebP::SharpYUV STATIC IMPORTED)
			set_target_properties(WebP::SharpYUV PROPERTIES
				IMPORTED_LOCATION ${EXTERNAL_EMSCRIPTEN_DIR}/libsharpyuv.a)
			set_target_properties(WebP::WebP PROPERTIES
				INTERFACE_LINK_LIBRARIES WebP::SharpYUV)
		endif()
		set(WEBP_FOUND 1)
	endif()

	if(NCINE_WITH_AUDIO)
		add_library(OpenAL::OpenAL INTERFACE IMPORTED)
		set_target_properties(OpenAL::OpenAL PROPERTIES
			INTERFACE_LINK_OPTIONS "SHELL:-lopenal")
		set(OPENAL_FOUND 1)

		if(NCINE_WITH_VORBIS)
			add_library(Vorbis::Vorbisfile INTERFACE IMPORTED)
			set_target_properties(Vorbis::Vorbisfile PROPERTIES
				INTERFACE_COMPILE_OPTIONS "SHELL:-s USE_VORBIS=1"
				INTERFACE_LINK_OPTIONS "SHELL:-s USE_VORBIS=1")
			set(VORBIS_FOUND 1)
		endif()
		
		if(NCINE_WITH_OPENMPT)
			find_package(libopenmpt)
			if(TARGET libopenmpt::libopenmpt)
				set(OPENMPT_FOUND 1)
			endif()
		endif()
	endif()

	if(NCINE_WITH_LUA AND EXISTS "${EXTERNAL_EMSCRIPTEN_DIR}/liblua.a")
		add_library(Lua::Lua STATIC IMPORTED)
		set_target_properties(Lua::Lua PROPERTIES
			IMPORTED_LOCATION "${EXTERNAL_EMSCRIPTEN_DIR}/liblua.a"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}")
		set(LUA_FOUND 1)
	endif()
	
	if(NCINE_WITH_ANGELSCRIPT)
		find_package(Angelscript)
	endif()

	return()
endif()

if(NCINE_WITH_THREADS)
	find_package(Threads)
endif()

if(MSVC OR MINGW OR MSYS)
	set(EXTERNAL_MSVC_DIR "${NCINE_LIBS}/Windows/" CACHE PATH "Set the path to the MSVC libraries directory")
	if(NOT IS_DIRECTORY ${EXTERNAL_MSVC_DIR})
		message(STATUS "MSVC libraries directory not found at: ${EXTERNAL_MSVC_DIR}")
	else()
		message(STATUS "MSVC libraries directory: ${EXTERNAL_MSVC_DIR}")
	endif()

	# TODO: Detect ARM64EC libraries
	set(MSVC_ARCH_SUFFIX "x86")
	if(MINGW OR MSYS)
		if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "AMD64")
			set(MSVC_ARCH_SUFFIX "x64")
		endif()
	elseif(MSVC_C_ARCHITECTURE_ID MATCHES 64 OR MSVC_CXX_ARCHITECTURE_ID MATCHES 64)
		set(MSVC_ARCH_SUFFIX "x64")
	endif()

	set(MSVC_LIBDIR "${EXTERNAL_MSVC_DIR}/${MSVC_ARCH_SUFFIX}/")
	set(MSVC_BINDIR "${EXTERNAL_MSVC_DIR}/${MSVC_ARCH_SUFFIX}/Bin/")
	
	if(WINDOWS_PHONE OR WINDOWS_STORE)
		set(EXTERNAL_MSVC_WINRT_DIR "${NCINE_LIBS}/Universal Windows Platform/" CACHE PATH "Set the path to the MSVC (WinRT) libraries directory")
		if(NOT IS_DIRECTORY ${EXTERNAL_MSVC_WINRT_DIR})
			message(STATUS "MSVC (WinRT) libraries directory not found at: ${EXTERNAL_MSVC_WINRT_DIR}")
		else()
			message(STATUS "MSVC (WinRT) libraries directory: ${EXTERNAL_MSVC_WINRT_DIR}")
		endif()
	
		set(MSVC_WINRT_LIBDIR "${EXTERNAL_MSVC_WINRT_DIR}/${MSVC_ARCH_SUFFIX}/")
		set(MSVC_WINRT_BINDIR "${EXTERNAL_MSVC_WINRT_DIR}/${MSVC_ARCH_SUFFIX}/Bin/")
	elseif(WIN32)
		# Try to find VC-LTL library (if not disabled)
		if(DEATH_WITH_VC_LTL AND MSVC)
			if(NOT VC_LTL_Root)
				if(EXISTS "${NCINE_ROOT}/Libs/VC-LTL/_msvcrt.h")
					set(VC_LTL_Root "${NCINE_ROOT}/Libs/VC-LTL")
				endif()
			endif()
			if(NOT VC_LTL_Root)
				GET_FILENAME_COMPONENT(FOUND_FILE "[HKEY_CURRENT_USER\\Code\\VC-LTL;Root]" ABSOLUTE)
				if (NOT ${FOUND_FILE} STREQUAL "registry")
					set(VC_LTL_Root ${FOUND_FILE})
				endif()
			endif()
			if(IS_DIRECTORY ${VC_LTL_Root})
				message(STATUS "Found VC-LTL: ${VC_LTL_Root}")
				set(VC_LTL_FOUND 1)
			endif()
		endif()
		
		find_package(OpenGL REQUIRED)
	endif()
elseif(NOT ANDROID AND NOT NCINE_BUILD_ANDROID) # GCC and LLVM
	if(APPLE)
		set(CMAKE_FRAMEWORK_PATH ${NCINE_LIBS})
		set(CMAKE_MACOSX_RPATH ON)

		if(NOT IS_DIRECTORY ${CMAKE_FRAMEWORK_PATH})
			message(FATAL_ERROR "MacOS Frameworks directory not found at: ${CMAKE_FRAMEWORK_PATH}")
		else()
			message(STATUS "MacOS Frameworks directory: ${CMAKE_FRAMEWORK_PATH}")
		endif()
	endif()

	find_package(ZLIB)
	if(NCINE_WITH_GLEW)
		find_package(GLEW)
	endif()
	if(NOT NINTENDO_SWITCH)
		set(OPENGL_USE_OPENGL ON)
		find_package(OpenGL)
	endif()
	if(NCINE_ARM_PROCESSOR)
		include(check_atomic)
	endif()
	if(NCINE_WITH_OPENGLES OR NINTENDO_SWITCH)
		find_package(OpenGLES2)
	endif()
	# Look for both GLFW and SDL2 to make the fallback logic work
	find_package(GLFW)
	find_package(SDL2)
	#if(NCINE_WITH_PNG)
	#	find_package(PNG)
	#endif()
	if(NCINE_WITH_WEBP)
		find_package(WebP)
	endif()
	if(NCINE_WITH_AUDIO)
		find_package(OpenAL)

		if(NINTENDO_SWITCH)
			# This flag is not set correctly by the toolchain
			set(OPENAL_FOUND 1)
		endif()

		if(NCINE_WITH_VORBIS)
			find_package(Vorbis)
		endif()
		if(NCINE_WITH_OPENMPT)
			find_package(libopenmpt)
		endif()
	endif()
	if(NCINE_WITH_LUA)
		# Older CMake versions do not support Lua 5.4 if not required explicitly
		find_package(Lua 5.4)
		if(NOT LUA_FOUND)
			find_package(Lua)
		endif()
	endif()
endif()

if(NOT ANDROID AND NOT EMSCRIPTEN AND NCINE_PREFERRED_BACKEND STREQUAL "QT5")
	find_package(Qt5 COMPONENTS Widgets REQUIRED)
	find_package(Qt5Gamepad)
endif()

if(ANDROID)
	find_library(ANDROID_LIBRARY android)
	find_library(EGL_LIBRARY EGL)
	find_library(GLES3_LIBRARY GLESv3)
	find_library(LOG_LIBRARY log)
	find_library(NATIVEWINDOW_LIBRARY nativewindow)
	find_library(OPENSLES_LIBRARY OpenSLES)
	find_library(ZLIB_LIBRARY z)
	
	add_library(ZLIB::ZLIB STATIC IMPORTED)
	set_target_properties(ZLIB::ZLIB PROPERTIES
		IMPORTED_LOCATION "${ZLIB_LIBRARY}")
	set(ZLIB_FOUND 1)
	message(STATUS "Found ZLIB: ${ZLIB_LIBRARY}")
	
	#if(NCINE_WITH_PNG AND EXISTS "${EXTERNAL_ANDROID_DIR}/png/${ANDROID_ABI}/libpng16.a")
	#	add_library(PNG::PNG STATIC IMPORTED)
	#	set_target_properties(PNG::PNG PROPERTIES
	#		IMPORTED_LINK_INTERFACE_LANGUAGES "C"
	#		IMPORTED_LOCATION "${EXTERNAL_ANDROID_DIR}/png/${ANDROID_ABI}/libpng16.a"
	#		INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_ANDROID_DIR}/png/include"
	#		INTERFACE_LINK_LIBRARIES "${ZLIB_LIBRARY}")
	#	set(PNG_FOUND 1)
	#endif()

	if(NCINE_WITH_WEBP AND EXISTS "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libwebp.a")
		add_library(WebP::WebP STATIC IMPORTED)
		set_target_properties(WebP::WebP PROPERTIES
			IMPORTED_LOCATION "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libwebp.a"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}/webp/")
		if(EXISTS "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libsharpyuv.a")
			# Since libwebp 1.3.0, libwebp.a needs some symbols from libsharpyuv.a
			add_library(WebP::SharpYUV STATIC IMPORTED)
			set_target_properties(WebP::SharpYUV PROPERTIES
				IMPORTED_LOCATION "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libsharpyuv.a")
			set_target_properties(WebP::WebP PROPERTIES
				INTERFACE_LINK_LIBRARIES WebP::SharpYUV)
		endif()
		set(WEBP_FOUND 1)
	endif()

	if(NCINE_WITH_AUDIO AND EXISTS "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libopenal.so")
		add_library(OpenAL::OpenAL SHARED IMPORTED)
		set_target_properties(OpenAL::OpenAL PROPERTIES
			IMPORTED_LOCATION "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libopenal.so"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}/AL/")
		set(OPENAL_FOUND 1)

		if(NCINE_WITH_VORBIS AND
		   EXISTS "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libvorbisfile.a" AND
		   EXISTS "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libvorbis.a" AND
		   EXISTS "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libogg.a")
			add_library(Ogg::Ogg STATIC IMPORTED)
			set_target_properties(Ogg::Ogg PROPERTIES
				IMPORTED_LOCATION "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libogg.a"
				INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}")

			add_library(Vorbis::Vorbis STATIC IMPORTED)
			set_target_properties(Vorbis::Vorbis PROPERTIES
				IMPORTED_LOCATION "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libvorbis.a"
				INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}"
				INTERFACE_LINK_LIBRARIES Ogg::Ogg)

			add_library(Vorbis::Vorbisfile STATIC IMPORTED)
			set_target_properties(Vorbis::Vorbisfile PROPERTIES
				IMPORTED_LOCATION "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libvorbisfile.a"
				INTERFACE_LINK_LIBRARIES Vorbis::Vorbis)
			set(VORBIS_FOUND 1)
		endif()
		
		if(NCINE_WITH_OPENMPT AND EXISTS "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libopenmpt.a")
			add_library(libopenmpt::libopenmpt STATIC IMPORTED)
			set_target_properties(libopenmpt::libopenmpt PROPERTIES
				IMPORTED_LOCATION "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/libopenmpt.a"
				INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}/libopenmpt/")
			set(OPENMPT_FOUND 1)
		endif()
	endif()

	if(NCINE_WITH_LUA AND EXISTS "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/liblua.a")
		add_library(Lua::Lua STATIC IMPORTED)
		set_target_properties(Lua::Lua PROPERTIES
			IMPORTED_LOCATION "${EXTERNAL_ANDROID_DIR}/${ANDROID_ABI}/liblua.a"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}/lua/")
		set(LUA_FOUND 1)
	endif()
	
	if(NCINE_WITH_ANGELSCRIPT)
		find_package(Angelscript)
		if(TARGET Angelscript::Angelscript)
			set(ANGELSCRIPT_FOUND 1)
		endif()
	endif()
elseif(MSVC OR MINGW OR MSYS)
	if(EXISTS "${MSVC_LIBDIR}/zlib.lib" AND EXISTS "${MSVC_BINDIR}/zlib.dll")
		add_library(ZLIB::ZLIB SHARED IMPORTED)
		set_target_properties(ZLIB::ZLIB PROPERTIES
			IMPORTED_IMPLIB "${MSVC_LIBDIR}/zlib.lib"
			IMPORTED_LOCATION "${MSVC_BINDIR}/zlib.dll"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}/zlib/")
		set(ZLIB_FOUND 1)
	endif()

	if(NCINE_WITH_ANGLE AND
	   EXISTS "${MSVC_LIBDIR}/libEGL.lib" AND EXISTS "${MSVC_BINDIR}/libEGL.dll" AND
	   EXISTS "${MSVC_LIBDIR}/libGLESv2.lib" AND EXISTS "${MSVC_BINDIR}/libGLESv2.dll")
		add_library(EGL::EGL SHARED IMPORTED)
		set_target_properties(EGL::EGL PROPERTIES
			IMPORTED_IMPLIB "${MSVC_LIBDIR}/libEGL.lib"
			IMPORTED_LOCATION "${MSVC_BINDIR}/libEGL.dll"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}")

		add_library(OpenGLES2::GLES2 SHARED IMPORTED)
		set_target_properties(OpenGLES2::GLES2 PROPERTIES
			IMPORTED_IMPLIB "${MSVC_LIBDIR}/libGLESv2.lib"
			IMPORTED_LOCATION "${MSVC_BINDIR}/libGLESv2.dll"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}")
		set(ANGLE_FOUND 1)
	elseif(WINDOWS_PHONE OR WINDOWS_STORE)
		add_library(EGL::EGL SHARED IMPORTED)
		set_target_properties(EGL::EGL PROPERTIES
			IMPORTED_IMPLIB "${MSVC_WINRT_BINDIR}/Mesa/libEGL.lib"
			IMPORTED_LOCATION "${MSVC_WINRT_BINDIR}/Mesa/libEGL.dll"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}")

		add_library(OpenGLES2::GLES2 SHARED IMPORTED)
		set_target_properties(OpenGLES2::GLES2 PROPERTIES
			IMPORTED_IMPLIB "${MSVC_WINRT_BINDIR}/Mesa/libGLESv2.lib"
			IMPORTED_LOCATION "${MSVC_WINRT_BINDIR}/Mesa/libGLESv2.dll"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}")
		set(OPENGLES2_FOUND 1)
	else()
		if(EXISTS "${MSVC_LIBDIR}/glew32.lib" AND EXISTS "${MSVC_BINDIR}/glew32.dll")
			add_library(GLEW::GLEW SHARED IMPORTED)
			set_target_properties(GLEW::GLEW PROPERTIES
				IMPORTED_IMPLIB "${MSVC_LIBDIR}/glew32.lib"
				IMPORTED_LOCATION "${MSVC_BINDIR}/glew32.dll"
				INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}")
			set(GLEW_FOUND 1)
		endif()
	endif()

	if(NOT WINDOWS_PHONE AND NOT WINDOWS_STORE)
		if(NCINE_PREFERRED_BACKEND STREQUAL "GLFW" AND
			EXISTS "${MSVC_LIBDIR}/glfw3dll.lib" AND EXISTS "${MSVC_BINDIR}/glfw3.dll")
			add_library(GLFW::GLFW SHARED IMPORTED)
			set_target_properties(GLFW::GLFW PROPERTIES
				IMPORTED_IMPLIB "${MSVC_LIBDIR}/glfw3dll.lib"
				IMPORTED_LOCATION "${MSVC_BINDIR}/glfw3.dll"
				INTERFACE_COMPILE_DEFINITIONS "GLFW_NO_GLU"
				INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}/GL/")
			set(GLFW_FOUND 1)
		endif()

		if(NCINE_PREFERRED_BACKEND STREQUAL "SDL2" AND
			EXISTS "${MSVC_LIBDIR}/SDL2.lib" AND EXISTS "${MSVC_BINDIR}/SDL2.dll")
			add_library(SDL2::SDL2 SHARED IMPORTED)
			set_target_properties(SDL2::SDL2 PROPERTIES
				IMPORTED_IMPLIB "${MSVC_LIBDIR}/SDL2.lib"
				IMPORTED_LOCATION "${MSVC_BINDIR}/SDL2.dll"
				INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}/SDL2/")
			set(SDL2_FOUND 1)
		endif()
	endif()

	#if(NCINE_WITH_PNG AND
	#	EXISTS "${MSVC_LIBDIR}/libpng16.lib" AND EXISTS "${MSVC_LIBDIR}/zlib.lib" AND
	#	EXISTS "${MSVC_BINDIR}/libpng16.dll" AND EXISTS "${MSVC_BINDIR}/zlib.dll")
	#	add_library(ZLIB::ZLIB SHARED IMPORTED)
	#	set_target_properties(ZLIB::ZLIB PROPERTIES
	#		IMPORTED_IMPLIB "${MSVC_LIBDIR}/zlib.lib"
	#		IMPORTED_LOCATION "${MSVC_BINDIR}/zlib.dll")
	#	add_library(PNG::PNG SHARED IMPORTED)
	#	set_target_properties(PNG::PNG PROPERTIES
	#		IMPORTED_LINK_INTERFACE_LANGUAGES "C"
	#		IMPORTED_IMPLIB "${MSVC_LIBDIR}/libpng16.lib"
	#		IMPORTED_LOCATION "${MSVC_BINDIR}/libpng16.dll"
	#		INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}"
	#		INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
	#	set(PNG_FOUND 1)
	#endif()

	if(NCINE_WITH_WEBP AND
		EXISTS "${MSVC_LIBDIR}/libwebp_dll.lib" AND EXISTS "${MSVC_BINDIR}/libwebp.dll")
		add_library(WebP::WebP SHARED IMPORTED)
		set_target_properties(WebP::WebP PROPERTIES
			IMPORTED_IMPLIB "${MSVC_LIBDIR}/libwebp_dll.lib"
			IMPORTED_LOCATION "${MSVC_BINDIR}/libwebp.dll"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}")
		set(WEBP_FOUND 1)
	endif()

	if(NCINE_WITH_AUDIO AND EXISTS "${MSVC_LIBDIR}/OpenAL32.lib" AND EXISTS "${MSVC_BINDIR}/OpenAL32.dll")
		add_library(OpenAL::OpenAL SHARED IMPORTED)
		set_target_properties(OpenAL::OpenAL PROPERTIES
			IMPORTED_IMPLIB "${MSVC_LIBDIR}/OpenAL32.lib"
			IMPORTED_LOCATION "${MSVC_BINDIR}/OpenAL32.dll"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}/AL/")
		set(OPENAL_FOUND 1)

		if(NCINE_WITH_VORBIS)
			if(NCINE_WITH_OPENMPT AND EXISTS "${MSVC_BINDIR}/openmpt-ogg.dll" AND EXISTS "${MSVC_BINDIR}/openmpt-vorbis.dll")
				# If NCINE_WITH_OPENMPT is also enabled on Windows, shared library can be used instead of the dedicated one
				set(VORBIS_FOUND 1)
				set(VORBIS_DYNAMIC_LINK 1)
			elseif(EXISTS "${MSVC_LIBDIR}/libogg.lib" AND EXISTS "${MSVC_LIBDIR}/libvorbis.lib" AND EXISTS "${MSVC_LIBDIR}/libvorbisfile.lib" AND
				EXISTS "${MSVC_BINDIR}/libogg.dll" AND EXISTS "${MSVC_BINDIR}/libvorbis.dll" AND EXISTS "${MSVC_BINDIR}/libvorbisfile.dll")
				add_library(Ogg::Ogg SHARED IMPORTED)
				set_target_properties(Ogg::Ogg PROPERTIES
					IMPORTED_IMPLIB "${MSVC_LIBDIR}/libogg.lib"
					IMPORTED_LOCATION "${MSVC_BINDIR}/libogg.dll")

				add_library(Vorbis::Vorbis SHARED IMPORTED)
				set_target_properties(Vorbis::Vorbis PROPERTIES
					IMPORTED_IMPLIB "${MSVC_LIBDIR}/libvorbis.lib"
					IMPORTED_LOCATION "${MSVC_BINDIR}/libvorbis.dll"
					INTERFACE_LINK_LIBRARIES Ogg::Ogg)

				add_library(Vorbis::Vorbisfile SHARED IMPORTED)
				set_target_properties(Vorbis::Vorbisfile PROPERTIES
					IMPORTED_IMPLIB "${MSVC_LIBDIR}/libvorbisfile.lib"
					IMPORTED_LOCATION "${MSVC_BINDIR}/libvorbisfile.dll"
					INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}"
					INTERFACE_LINK_LIBRARIES Vorbis::Vorbis)
				set(VORBIS_FOUND 1)
			endif()
		endif()
		
		if(NCINE_WITH_OPENMPT AND EXISTS "${MSVC_LIBDIR}/libopenmpt.lib" AND EXISTS "${MSVC_BINDIR}/libopenmpt.dll")
			add_library(libopenmpt::libopenmpt SHARED IMPORTED)
			set_target_properties(libopenmpt::libopenmpt PROPERTIES
				IMPORTED_IMPLIB "${MSVC_LIBDIR}/libopenmpt.lib"
				IMPORTED_LOCATION "${MSVC_BINDIR}/libopenmpt.dll"
				INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}/libopenmpt/")
			set(OPENMPT_FOUND 1)
		endif()
	endif()

	if(NCINE_WITH_LUA AND EXISTS "${MSVC_LIBDIR}/lua54.lib" AND EXISTS "${MSVC_BINDIR}/lua54.dll")
		add_library(Lua::Lua SHARED IMPORTED)
		set_target_properties(Lua::Lua PROPERTIES
			IMPORTED_IMPLIB "${MSVC_LIBDIR}/lua54.lib"
			IMPORTED_LOCATION "${MSVC_BINDIR}/lua54.dll"
			INTERFACE_INCLUDE_DIRECTORIES "${EXTERNAL_INCLUDES_DIR}")
		set(LUA_FOUND 1)
	endif()
	
	if(NCINE_WITH_ANGELSCRIPT)
		find_package(Angelscript)
		if(TARGET Angelscript::Angelscript)
			set(ANGELSCRIPT_FOUND 1)
		endif()
	endif()
#elseif(MINGW OR MSYS)
#	function(set_msys_dll PREFIX DLL_NAME)
#		set(LIB_NAME "${DLL_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")
#		set(DLL_LIBRARY "$ENV{MINGW_PREFIX}/bin/${LIB_NAME}")
#		if(EXISTS ${DLL_LIBRARY} AND NOT IS_DIRECTORY ${DLL_LIBRARY})
#			set(${PREFIX}_DLL_LIBRARY ${DLL_LIBRARY} PARENT_SCOPE)
#		else()
#			message(WARNING "Could not find: ${DLL_LIBRARY}")
#		endif()
#	endfunction()
#
#	if(GLFW_FOUND)
#		set_msys_dll(GLFW glfw3)
#		add_library(GLFW::GLFW SHARED IMPORTED)
#		set_target_properties(GLFW::GLFW PROPERTIES
#			IMPORTED_IMPLIB "${GLFW_LIBRARY}"
#			IMPORTED_LOCATION "${GLFW_DLL_LIBRARY}"
#			INTERFACE_COMPILE_DEFINITIONS "GLFW_NO_GLU"
#			INTERFACE_INCLUDE_DIRECTORIES "${GLFW_INCLUDE_DIR}")
#	endif()
#
#	if(SDL2_FOUND)
#		foreach(LIBRARY ${SDL2_LIBRARY})
#			string(REGEX MATCH "\.a$" FOUND_STATIC_LIB ${LIBRARY})
#			if(NOT FOUND_STATIC_LIB STREQUAL "" AND NOT ${LIBRARY} STREQUAL ${SDL2MAIN_LIBRARY})
#				set(SDL2_IMPORT_LIBRARY ${LIBRARY})
#				break()
#			endif()
#		endforeach()
#
#		set_msys_dll(SDL2 SDL2)
#		add_library(SDL2::SDL2 SHARED IMPORTED)
#		set_target_properties(SDL2::SDL2 PROPERTIES
#			IMPORTED_IMPLIB "${SDL2_IMPORT_LIBRARY}"
#			IMPORTED_LOCATION "${SDL2_DLL_LIBRARY}"
#			INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}")
#	endif()
#
#	if(WEBP_FOUND)
#		set_msys_dll(WEBP libwebp-7)
#		add_library(WebP::WebP SHARED IMPORTED)
#		set_target_properties(WebP::WebP PROPERTIES
#			IMPORTED_IMPLIB "${WEBP_LIBRARY}"
#			IMPORTED_LOCATION "${WEBP_DLL_LIBRARY}"
#			INTERFACE_INCLUDE_DIRECTORIES "${WEBP_INCLUDE_DIR}")
#	endif()
#
#	if(OPENAL_FOUND)
#		set_msys_dll(OPENAL libopenal-1)
#		add_library(OpenAL::OpenAL SHARED IMPORTED)
#		set_target_properties(OpenAL::OpenAL PROPERTIES
#			IMPORTED_IMPLIB "${OPENAL_LIBRARY}"
#			IMPORTED_LOCATION "${OPENAL_DLL_LIBRARY}"
#			IMPORTED_LOCATION "${OPENAL_LIB_PATH}/${OPENAL_LIB_NAME}.dll"
#			INTERFACE_INCLUDE_DIRECTORIES "${OPENAL_INCLUDE_DIR}")
#
#		if(VORBIS_FOUND)
#			set_msys_dll(VORBISFILE libvorbisfile-3)
#			add_library(Vorbis::Vorbisfile SHARED IMPORTED)
#			set_target_properties(Vorbis::Vorbisfile PROPERTIES
#				IMPORTED_IMPLIB "${VORBISFILE_LIBRARY}"
#				IMPORTED_LOCATION "${VORBISFILE_DLL_LIBRARY}"
#				INTERFACE_INCLUDE_DIRECTORIES "${VORBIS_INCLUDE_DIR}"
#				INTERFACE_LINK_LIBRARIES "${VORBIS_LIBRARY};${OGG_LIBRARY}")
#		endif()
#	endif()
#
#	if(LUA_FOUND)
#		set_msys_dll(LUA lua54)
#		add_library(Lua::Lua SHARED IMPORTED)
#		set_target_properties(Lua::Lua PROPERTIES
#			IMPORTED_IMPLIB "${LUA_LIBRARY}"
#			IMPORTED_LOCATION "${LUA_DLL_LIBRARY}"
#			INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}")
#	endif()
elseif(NOT NCINE_BUILD_ANDROID) # GCC and LLVM
	function(split_extra_libraries PREFIX LIBRARIES)
		foreach(LIBRARY ${LIBRARIES})
			string(REGEX MATCH "^-l" FOUND_LINKER_ARG ${LIBRARY})
			string(REGEX MATCH "\.a$" FOUND_STATIC_LIB ${LIBRARY})
			if(NOT FOUND_LINKER_ARG STREQUAL "" OR NOT FOUND_STATIC_LIB STREQUAL "")
				list(APPEND EXTRA_LIBRARIES ${LIBRARY})
			else()
				list(APPEND LIBRARY_FILE ${LIBRARY})
			endif()
		endforeach()
		if(NOT LIBRARY_FILE AND EXTRA_LIBRARIES)
			list(POP_FRONT EXTRA_LIBRARIES LIBRARY_FILE)
		endif()
		set(${PREFIX}_EXTRA_LIBRARIES ${EXTRA_LIBRARIES} PARENT_SCOPE)
		set(${PREFIX}_LIBRARY_FILE ${LIBRARY_FILE} PARENT_SCOPE)
	endfunction()

	if(ATOMIC_FOUND)
		add_library(Atomic::Atomic INTERFACE IMPORTED)
		set_target_properties(Atomic::Atomic PROPERTIES
			INTERFACE_LINK_DIRECTORIES ${ATOMIC_DIRECTORY}
			INTERFACE_LINK_LIBRARIES atomic)
	endif()

	if(NINTENDO_SWITCH)
		# Nintendo Switch supports only static linking
		set(LIBRARY_LINKAGE STATIC)
	else()
		set(LIBRARY_LINKAGE SHARED)
	endif()

	if(OPENGLES2_FOUND)
		add_library(EGL::EGL ${LIBRARY_LINKAGE} IMPORTED)
		set_target_properties(EGL::EGL PROPERTIES
			IMPORTED_LOCATION "${EGL_LIBRARIES}"
			INTERFACE_INCLUDE_DIRECTORIES "${EGL_INCLUDE_DIR}")

		add_library(OpenGLES2::GLES2 ${LIBRARY_LINKAGE} IMPORTED)
		set_target_properties(OpenGLES2::GLES2 PROPERTIES
			IMPORTED_LOCATION "${OPENGLES2_LIBRARIES}"
			INTERFACE_INCLUDE_DIRECTORIES "${OPENGLES2_INCLUDE_DIR}")
	endif()

	if(GLFW_FOUND AND NOT TARGET GLFW::GLFW)
		add_library(GLFW::GLFW ${LIBRARY_LINKAGE} IMPORTED)
		set_target_properties(GLFW::GLFW PROPERTIES
			IMPORTED_LOCATION "${GLFW_LIBRARY}" # On macOS it's a list
			INTERFACE_COMPILE_DEFINITIONS "GLFW_NO_GLU"
			INTERFACE_INCLUDE_DIRECTORIES "${GLFW_INCLUDE_DIR}")
	endif()

	if(SDL2_FOUND AND NOT TARGET SDL2::SDL2)
		split_extra_libraries(SDL2 "${SDL2_LIBRARY}")
		add_library(SDL2::SDL2 ${LIBRARY_LINKAGE} IMPORTED)
		set_target_properties(SDL2::SDL2 PROPERTIES
			IMPORTED_LOCATION "${SDL2_LIBRARY_FILE}" # On macOS it's a list
			INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
			INTERFACE_LINK_LIBRARIES "${SDL2_EXTRA_LIBRARIES}")
	endif()

	if(WEBP_FOUND AND NOT TARGET WebP::WebP)
		add_library(WebP::WebP ${LIBRARY_LINKAGE} IMPORTED)
		set_target_properties(WebP::WebP PROPERTIES
			IMPORTED_LOCATION "${WEBP_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES "${WEBP_INCLUDE_DIR}")
	endif()

	if(OPENAL_FOUND)
		if(NOT TARGET OpenAL::OpenAL)
			add_library(OpenAL::OpenAL ${LIBRARY_LINKAGE} IMPORTED)
			set_target_properties(OpenAL::OpenAL PROPERTIES
				IMPORTED_LOCATION "${OPENAL_LIBRARY}"
				INTERFACE_INCLUDE_DIRECTORIES "${OPENAL_INCLUDE_DIR}")
		endif()

		if(VORBIS_FOUND AND NOT TARGET Vorbis::Vorbisfile)
			add_library(Vorbis::Vorbisfile ${LIBRARY_LINKAGE} IMPORTED)
			set_target_properties(Vorbis::Vorbisfile PROPERTIES
				IMPORTED_LOCATION "${VORBISFILE_LIBRARY}"
				INTERFACE_INCLUDE_DIRECTORIES "${VORBIS_INCLUDE_DIR}"
				INTERFACE_LINK_LIBRARIES "${VORBIS_LIBRARY};${OGG_LIBRARY}")
		endif()
		
		if(NCINE_WITH_OPENMPT)
			set(OPENMPT_FOUND 1)
			if(NOT TARGET libopenmpt::libopenmpt)
				set(OPENMPT_DYNAMIC_LINK 1)
				message(STATUS "Cannot find libopenmpt, using dynamic linking instead")
			endif()
		endif()
	endif()

	if(LUA_FOUND AND NOT TARGET Lua::Lua)
		add_library(Lua::Lua ${LIBRARY_LINKAGE} IMPORTED)
		set_target_properties(Lua::Lua PROPERTIES
			IMPORTED_LOCATION "${LUA_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}")
	endif()
	
	if(NCINE_WITH_ANGELSCRIPT)
		find_package(Angelscript)
		if(TARGET Angelscript::Angelscript)
			set(ANGELSCRIPT_FOUND 1)
		endif()
	endif()

	if(APPLE)
		function(split_extra_frameworks PREFIX LIBRARIES)
			foreach(LIBRARY ${LIBRARIES})
				string(REGEX MATCH "^-framework " FOUND ${LIBRARY})
				if(NOT FOUND STREQUAL "")
					list(APPEND FRAMEWORK_LINKS ${LIBRARY})
				else()
					list(APPEND FRAMEWORK_DIR ${LIBRARY})
				endif()
			endforeach()
			set(${PREFIX}_FRAMEWORK_LINKS ${FRAMEWORK_LINKS} PARENT_SCOPE)
			set(${PREFIX}_FRAMEWORK_DIR ${FRAMEWORK_DIR} PARENT_SCOPE)
		endfunction()

		set_target_properties(ZLIB::ZLIB PROPERTIES
			IMPORTED_LOCATION "${ZLIB_LIBRARY_RELEASE}/zlib"
			IMPORTED_LOCATION_RELEASE "${ZLIB_LIBRARY_RELEASE}/zlib"
			IMPORTED_LOCATION_DEBUG "${ZLIB_LIBRARY_RELEASE}/zlib")

		if(GLEW_FOUND)
			get_target_property(GLEW_LIBRARY_RELEASE GLEW::GLEW IMPORTED_LOCATION_RELEASE)
			set_target_properties(GLEW::GLEW PROPERTIES
				IMPORTED_LOCATION "${GLEW_LIBRARY_RELEASE}/glew"
				IMPORTED_LOCATION_RELEASE "${GLEW_LIBRARY_RELEASE}/glew"
				IMPORTED_LOCATION_DEBUG "${GLEW_LIBRARY_RELEASE}/glew")
		endif()

		if(GLFW_FOUND)
			split_extra_frameworks(GLFW "${GLFW_LIBRARY}")
			set_target_properties(GLFW::GLFW PROPERTIES
				IMPORTED_LOCATION "${GLFW_FRAMEWORK_DIR}/glfw"
				INTERFACE_LINK_LIBRARIES "${GLFW_FRAMEWORK_LINKS}")
		endif()

		if(SDL2_FOUND)
			split_extra_frameworks(SDL2 "${SDL2_LIBRARY}")
			set_target_properties(SDL2::SDL2 PROPERTIES
				IMPORTED_LOCATION "${SDL2_FRAMEWORK_DIR}/sdl2"
				INTERFACE_LINK_LIBRARIES "${SDL2_FRAMEWORK_LINKS}")
		endif()

		#if(PNG_FOUND)
		#	get_target_property(PNG_LIBRARY_RELEASE PNG::PNG IMPORTED_LOCATION_RELEASE)
		#	set_target_properties(PNG::PNG PROPERTIES
		#		IMPORTED_LOCATION "${PNG_LIBRARY_RELEASE}/png"
		#		IMPORTED_LOCATION_RELEASE "${PNG_LIBRARY_RELEASE}/png"
		#		IMPORTED_LOCATION_DEBUG "${PNG_LIBRARY_RELEASE}/png")
		#endif()

		if(WEBP_FOUND)
			set_target_properties(WebP::WebP PROPERTIES
				IMPORTED_LOCATION "${WEBP_LIBRARY}/webp")
		endif()

		if(OPENAL_FOUND)
			set_target_properties(OpenAL::OpenAL PROPERTIES
				IMPORTED_LOCATION "${OPENAL_LIBRARY}/openal")

			if(VORBIS_FOUND)
				set_target_properties(Vorbis::Vorbisfile PROPERTIES
					IMPORTED_LOCATION "${VORBISFILE_LIBRARY}/vorbisfile")
			endif()
		endif()

		if(LUA_FOUND)
			set_target_properties(Lua::Lua PROPERTIES
				IMPORTED_LOCATION "${LUA_LIBRARY}/lua")
		endif()
	endif()
endif()
