include(ncine_helpers)
include(CheckStructHasMember)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

ncine_normalize_optimizations()

target_compile_definitions(${NCINE_APP} PUBLIC "NCINE_VERSION=\"${NCINE_VERSION}\"")

string(TIMESTAMP NCINE_BUILD_YEAR "%Y") 
target_compile_definitions(${NCINE_APP} PUBLIC "NCINE_BUILD_YEAR=\"${NCINE_BUILD_YEAR}\"")

if(NCINE_OVERRIDE_CONTENT_PATH)
	message(STATUS "Using overriden `Content` path: ${NCINE_OVERRIDE_CONTENT_PATH}")
	target_compile_definitions(${NCINE_APP} PUBLIC "NCINE_OVERRIDE_CONTENT_PATH=\"${NCINE_OVERRIDE_CONTENT_PATH}\"")
elseif(NCINE_BUILD_FLATPAK)
	set(FLATPAK_CONTENT_PATH "/app/share/${NCINE_APP_NAME}/Content/") # Must be the same as in `ncine_installation.cmake`
	message(STATUS "Using custom `Content` path for Flatpak: ${FLATPAK_CONTENT_PATH}")
	target_compile_definitions(${NCINE_APP} PUBLIC "NCINE_OVERRIDE_CONTENT_PATH=\"${FLATPAK_CONTENT_PATH}\"")
elseif(NCINE_LINUX_PACKAGE)
	message(STATUS "Using custom Linux package name: ${NCINE_LINUX_PACKAGE}")
	target_compile_definitions(${NCINE_APP} PUBLIC "NCINE_LINUX_PACKAGE=\"${NCINE_LINUX_PACKAGE}\"")
	if(NCINE_PACKAGED_CONTENT_PATH)
		target_compile_definitions(${NCINE_APP} PUBLIC "NCINE_PACKAGED_CONTENT_PATH")
	endif()
endif()

if(DEATH_CPU_USE_RUNTIME_DISPATCH)
	target_compile_definitions(${NCINE_APP} PUBLIC "DEATH_CPU_USE_RUNTIME_DISPATCH")
	if(DEATH_CPU_USE_IFUNC)
		target_compile_definitions(${NCINE_APP} PUBLIC "DEATH_CPU_USE_IFUNC")
		message(STATUS "Using GNU IFUNC for CPU-dependent functionality")
	else()
		message(STATUS "Using runtime dispatch for CPU-dependent functionality")
	endif()
endif()

set(CMAKE_REQUIRED_QUIET ON)
check_struct_has_member("struct tm" tm_gmtoff time.h DEATH_USE_GMTOFF_IN_TM)
set(CMAKE_REQUIRED_QUIET OFF)
if(DEATH_USE_GMTOFF_IN_TM)
	target_compile_definitions(${NCINE_APP} PUBLIC "DEATH_USE_GMTOFF_IN_TM")
endif()

if(NOT DEATH_RUNTIME_CAST)
	target_compile_definitions(${NCINE_APP} PUBLIC "DEATH_NO_RUNTIME_CAST")
	message(STATUS "runtime_cast<T>() optimization is disabled")
endif()

if(NCINE_PROFILING)
	target_compile_definitions(${NCINE_APP} PUBLIC "NCINE_PROFILING")
	message(STATUS "Runtime profiling is enabled")
endif()

if(NCINE_WITH_FIXED_BATCH_SIZE)
	target_compile_definitions(${NCINE_APP} PUBLIC "WITH_FIXED_BATCH_SIZE=${NCINE_WITH_FIXED_BATCH_SIZE}")
	message(STATUS "Specified custom fixed batch size: ${NCINE_WITH_FIXED_BATCH_SIZE}")
endif()
if(NCINE_INPUT_DEBUGGING)
	target_compile_definitions(${NCINE_APP} PUBLIC "NCINE_INPUT_DEBUGGING")
	message(STATUS "Input debugging is enabled")
endif()

if(WIN32)
	# Enable Win32 executable
	set_target_properties(${NCINE_APP} PROPERTIES WIN32_EXECUTABLE TRUE)
	
	if(WINDOWS_PHONE OR WINDOWS_STORE)
		target_link_libraries(${NCINE_APP} PRIVATE WindowsApp.lib rpcrt4.lib onecoreuap.lib)

		set_target_properties(${NCINE_APP} PROPERTIES VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION "10.0.18362.0")
		set_target_properties(${NCINE_APP} PROPERTIES VS_GLOBAL_MinimalCoreWin "true")
		set_target_properties(${NCINE_APP} PROPERTIES VS_GLOBAL_AppxBundle "Always")
		set_target_properties(${NCINE_APP} PROPERTIES VS_GLOBAL_AppxBundlePlatforms "x64")
		set_target_properties(${NCINE_APP} PROPERTIES VS_GLOBAL_AppxPackageSigningTimestampDigestAlgorithm "SHA256")

		if(NCINE_UWP_CERTIFICATE_THUMBPRINT)
			message(STATUS "Signing package with certificate by thumbprint: ${NCINE_UWP_CERTIFICATE_THUMBPRINT}")
			set_target_properties(${NCINE_APP} PROPERTIES VS_GLOBAL_AppxPackageSigningEnabled "true")
			set_target_properties(${NCINE_APP} PROPERTIES VS_GLOBAL_PackageCertificateThumbprint ${NCINE_UWP_CERTIFICATE_THUMBPRINT})
		else()
			if(NOT EXISTS ${NCINE_UWP_CERTIFICATE_PATH})
				set(NCINE_UWP_CERTIFICATE_PATH "${NCINE_ROOT}/UwpCertificate.pfx")
			endif()
			if(EXISTS ${NCINE_UWP_CERTIFICATE_PATH})
				message(STATUS "Signing package with certificate: ${NCINE_UWP_CERTIFICATE_PATH}")
				set_target_properties(${NCINE_APP} PROPERTIES VS_GLOBAL_AppxPackageSigningEnabled "true")
				set_target_properties(${NCINE_APP} PROPERTIES VS_GLOBAL_PackageCertificateKeyFile ${NCINE_UWP_CERTIFICATE_PATH})
				if(NCINE_UWP_CERTIFICATE_PASSWORD)
					set_target_properties(${NCINE_APP} PROPERTIES VS_GLOBAL_PackageCertificatePassword ${NCINE_UWP_CERTIFICATE_PASSWORD})
				endif()
			endif()
		endif()
	else()
		# Override output executable name
		set_target_properties(${NCINE_APP} PROPERTIES OUTPUT_NAME "Jazz2")
		
		# Link to WinMM for high-precision timers and Windows Sockets 2 library for HTTP requests
		target_link_libraries(${NCINE_APP} PRIVATE winmm ws2_32)
		
		# Try to use VC-LTL library (if not disabled)
		if(VC_LTL_FOUND AND MSVC)
			target_compile_definitions(${NCINE_APP} PRIVATE "_DISABLE_DEPRECATE_LTL_MESSAGE")
			set_target_properties(${NCINE_APP} PROPERTIES VS_GLOBAL_VC_LTL_Root ${VC_LTL_Root})
			if(EXISTS "${NCINE_ROOT}/VC-LTL helper for Visual Studio.props")
				set_target_properties(${NCINE_APP} PROPERTIES VS_PROJECT_IMPORT "${NCINE_ROOT}/VC-LTL helper for Visual Studio.props")
			else()
				set_target_properties(${NCINE_APP} PROPERTIES VS_PROJECT_IMPORT "${VC_LTL_Root}/VC-LTL helper for Visual Studio.props")
			endif()
		endif()
	endif()
endif()

ncine_apply_compiler_options(${NCINE_APP})

if(EMSCRIPTEN)
	set(EMSCRIPTEN_LINKER_OPTIONS
		"SHELL:-s WASM=1"
		"SHELL:-s ASYNCIFY=1"
		"SHELL:-s DISABLE_EXCEPTION_CATCHING=1"
		"SHELL:-s MIN_WEBGL_VERSION=2"
		"SHELL:-s FORCE_FILESYSTEM=1"
		"SHELL:-s ALLOW_MEMORY_GROWTH=1"
		"SHELL:-s STACK_SIZE=131072" # 128 Kb
		"SHELL:-s MALLOC='emmalloc'"
		"SHELL:-s LZ4=1"
		"SHELL:--bind")

	set(EMSCRIPTEN_LINKER_OPTIONS_DEBUG
		#"SHELL:-s SAFE_HEAP=1"
		"SHELL:-s SAFE_HEAP_LOG=1"
		"SHELL:-s STACK_OVERFLOW_CHECK=2"
		"SHELL:-s GL_ASSERTIONS=1"
		"SHELL:-s DEMANGLE_SUPPORT=1"
		"SHELL:--profiling-funcs")

	string(FIND ${CMAKE_CXX_COMPILER} "fastcomp" EMSCRIPTEN_FASTCOMP_POS)
	if(EMSCRIPTEN_FASTCOMP_POS GREATER -1)
		list(APPEND EMSCRIPTEN_LINKER_OPTIONS "SHELL:-s BINARYEN_TRAP_MODE=clamp")
	else()
		list(APPEND EMSCRIPTEN_LINKER_OPTIONS "SHELL:-mnontrapping-fptoint")
	endif()

	if(DEATH_DEBUG)
		list(APPEND EMSCRIPTEN_LINKER_OPTIONS "SHELL:-s ASSERTIONS=1")
	else()
		list(APPEND EMSCRIPTEN_LINKER_OPTIONS_DEBUG "SHELL:-s ASSERTIONS=1")
	endif()
	
	# Include all files in specified directory
	list(APPEND EMSCRIPTEN_LINKER_OPTIONS "SHELL:--preload-file ${NCINE_DATA_DIR}@Content/")

	target_link_options(${NCINE_APP} PUBLIC ${EMSCRIPTEN_LINKER_OPTIONS})
	target_link_options(${NCINE_APP} PUBLIC "$<$<CONFIG:Debug>:${EMSCRIPTEN_LINKER_OPTIONS_DEBUG}>")

	if(Threads_FOUND)
		target_link_libraries(${NCINE_APP} PUBLIC Threads::Threads)
	endif()

	if(OPENGL_FOUND)
		target_link_libraries(${NCINE_APP} PUBLIC OpenGL::GL)
	endif()

	if(GLFW_FOUND)
		target_link_libraries(${NCINE_APP} PUBLIC GLFW::GLFW)
	endif()

	if(SDL2_FOUND)
		target_link_libraries(${NCINE_APP} PUBLIC SDL2::SDL2)
	endif()

	if(PNG_FOUND)
		target_link_libraries(${NCINE_APP} PUBLIC PNG::PNG)
	endif()

	if(VORBIS_FOUND)
		target_link_libraries(${NCINE_APP} PUBLIC Vorbis::Vorbisfile)
	endif()
	
	target_link_libraries(${NCINE_APP} PUBLIC idbfs.js)
	target_link_libraries(${NCINE_APP} PUBLIC websocket.js)
endif()

if(MSVC)
	if(NCINE_WITH_TRACY)
		target_link_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:/DEBUG>)
	endif()
else() # GCC and LLVM
	if(NCINE_DYNAMIC_LIBRARY)
		target_compile_options(${NCINE_APP} PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
	endif()

	if(NCINE_WITH_TRACY)
		target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-g -fno-omit-frame-pointer>)
		# Don't add `-rdynamic` on GCC/MinGW
		if(NOT ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND (MINGW OR MSYS)))
			target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-rdynamic>)
		endif()
		
		if(MINGW OR MSYS)
			target_link_libraries(${NCINE_APP} PRIVATE dbghelp)
		elseif(NOT ANDROID AND NOT APPLE)
			target_link_libraries(${NCINE_APP} PRIVATE dl)
		endif()
	endif()
endif()
