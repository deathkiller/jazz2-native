target_compile_features(${NCINE_APP} PUBLIC cxx_std_17)
set_target_properties(${NCINE_APP} PROPERTIES CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include(CheckStructHasMember)

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

target_compile_definitions(${NCINE_APP} PUBLIC "CMAKE_BUILD")
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

if(DEATH_DEBUG)
	target_compile_definitions(${NCINE_APP} PUBLIC "DEATH_DEBUG")
else()
	target_compile_definitions(${NCINE_APP} PUBLIC "$<$<CONFIG:Debug>:DEATH_DEBUG>")
endif()
if(DEATH_TRACE)
	target_compile_definitions(${NCINE_APP} PUBLIC "DEATH_TRACE")
	message(STATUS "Runtime event tracing is enabled")
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
	# Enable Win32 executable and force Unicode mode on Windows
	set_target_properties(${NCINE_APP} PROPERTIES WIN32_EXECUTABLE TRUE)
	target_compile_definitions(${NCINE_APP} PRIVATE "_UNICODE" "UNICODE")
	
	if(WINDOWS_PHONE OR WINDOWS_STORE)
		target_compile_definitions(${NCINE_APP} PUBLIC "DEATH_TARGET_WINDOWS_RT")
		
		# Workaround for "error C1189: The <experimental/coroutine> and <experimental/resumable> headers are only supported with /await"
		target_compile_options(${NCINE_APP} PRIVATE /await)
		# Workaround for "error C2039: 'wait_for': is not a member of 'winrt::impl'"
		target_compile_options(${NCINE_APP} PRIVATE /Zc:twoPhase-)
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
		
		# Link to Windows Sockets 2 library for HTTP requests
		target_link_libraries(${NCINE_APP} PRIVATE ws2_32)
		
		# Try to use VC-LTL library (if not disabled)
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
				target_compile_definitions(${NCINE_APP} PRIVATE "_DISABLE_DEPRECATE_LTL_MESSAGE")
				set_target_properties(${NCINE_APP} PROPERTIES VS_GLOBAL_VC_LTL_Root ${VC_LTL_Root})
				if(EXISTS "${NCINE_ROOT}/VC-LTL helper for Visual Studio.props")
					set_target_properties(${NCINE_APP} PROPERTIES VS_PROJECT_IMPORT "${NCINE_ROOT}/VC-LTL helper for Visual Studio.props")
				else()
					set_target_properties(${NCINE_APP} PROPERTIES VS_PROJECT_IMPORT "${VC_LTL_Root}/VC-LTL helper for Visual Studio.props")
				endif()
				set(VC_LTL_FOUND 1)
			endif()
		endif()
	endif()
endif()

if(EMSCRIPTEN)
	set(EMSCRIPTEN_LINKER_OPTIONS
		"SHELL:-s WASM=1"
		"SHELL:-s ASYNCIFY=1"
		"SHELL:-s DISABLE_EXCEPTION_CATCHING=1"
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

	#if(ZLIB_FOUND)
	#	target_link_libraries(${NCINE_APP} PUBLIC ZLIB::ZLIB)
	#endif()

	if(VORBIS_FOUND)
		target_link_libraries(${NCINE_APP} PUBLIC Vorbis::Vorbisfile)
	endif()

	#if(OPENMPT_FOUND)
	#	target_link_libraries(${NCINE_APP} PUBLIC libopenmpt::libopenmpt)
	#endif()
	
	target_link_libraries(${NCINE_APP} PUBLIC idbfs.js)
	target_link_libraries(${NCINE_APP} PUBLIC websocket.js)
endif()

if(NINTENDO_SWITCH)
	target_compile_definitions(${NCINE_APP} PUBLIC "DEATH_TARGET_SWITCH")
endif()

if(MSVC)
	# Build with Multiple Processes and force UTF-8
	target_compile_options(${NCINE_APP} PRIVATE /MP /utf-8)
	# Always use the non-debug version of the runtime library
	target_compile_options(${NCINE_APP} PUBLIC $<IF:$<BOOL:${VC_LTL_FOUND}>,/MT,/MD>)
	# Disable exceptions
	target_compile_definitions(${NCINE_APP} PRIVATE "_HAS_EXCEPTIONS=0")
	target_compile_options(${NCINE_APP} PRIVATE /EHsc)
	# Extra optimizations in Release
	target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:/fp:fast /O2 /Oi /Qpar /Gy>)
	# Include PDB debug information in Release and enable hot reloading in Debug
	target_compile_options(${NCINE_APP} PRIVATE $<IF:$<CONFIG:Debug>,/ZI,/Zi>)
	target_link_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL>)
	# Turn off SAFESEH because of OpenAL on x86 and also UAC
	target_link_options(${NCINE_APP} PRIVATE /SAFESEH:NO /MANIFESTUAC:NO)
	target_link_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:NOICF>)
	# Specifies the architecture for code generation (IA32, SSE, SSE2, AVX, AVX2, AVX512)
	if(NCINE_ARCH_EXTENSIONS)
		message(STATUS "Specified architecture extensions for code generation: ${NCINE_ARCH_EXTENSIONS}")
		target_compile_options(${NCINE_APP} PRIVATE /arch:${NCINE_ARCH_EXTENSIONS})
	endif()

	# Enable Whole Program Optimization
	if(NCINE_LINKTIME_OPTIMIZATION)
		target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:/GL>)
		target_link_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:/LTCG>)
	endif()

	if(NCINE_AUTOVECTORIZATION_REPORT)
		target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:/Qvec-report:2 /Qpar-report:2>)
	endif()

	# Suppress linker warning about templates
	target_compile_options(${NCINE_APP} PUBLIC "/wd4251")
	# Suppress warnings about "conversion from '<bigger int type>' to '<smaller int type>'",
	# "conversion from 'size_t' to '<smaller int type>', possible loss of data"
	target_compile_options(${NCINE_APP} PUBLIC "/wd4244" "/wd4267")

	# Adjust incremental linking
	target_link_options(${NCINE_APP} PRIVATE $<IF:$<CONFIG:Debug>,/INCREMENTAL,/INCREMENTAL:NO>)

	if(NCINE_WITH_TRACY)
		target_link_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:/DEBUG>)
	endif()

	if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		target_compile_options(${NCINE_APP} PRIVATE -Wno-switch -Wno-unknown-pragmas -Wno-reorder-ctor -Wno-braced-scalar-init -Wno-deprecated-builtins)
	endif()
else() # GCC and LLVM
	target_compile_options(${NCINE_APP} PRIVATE -fno-exceptions)
	target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-ffast-math>)

	if(NCINE_DYNAMIC_LIBRARY)
		target_compile_options(${NCINE_APP} PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
	endif()

	if(MINGW OR MSYS)
		target_link_options(${NCINE_APP} PUBLIC -municode)
	endif()

	if(NCINE_ARCH_EXTENSIONS AND UNIX AND NOT APPLE AND NOT ANDROID)
		message(STATUS "Specified architecture extensions for code generation: ${NCINE_ARCH_EXTENSIONS}")
		target_compile_options(${NCINE_APP} PRIVATE -march=${NCINE_ARCH_EXTENSIONS})
	endif()
	
	# Only in Debug - preserve debug information
	if(EMSCRIPTEN)
		target_compile_options(${NCINE_APP} PUBLIC $<$<CONFIG:Debug>:-g>)
		target_link_options(${NCINE_APP} PUBLIC $<$<CONFIG:Debug>:-g>)
	endif()

	# Only in Debug
	if(NCINE_ADDRESS_SANITIZER)
		# Add ASan options as public so that targets linking the library will also use them
		if(EMSCRIPTEN)
			target_compile_options(${NCINE_APP} PUBLIC $<$<CONFIG:Debug>:-O1 -g -fsanitize=address>) # Needs "ALLOW_MEMORY_GROWTH" which is already passed to the linker
			target_link_options(${NCINE_APP} PUBLIC $<$<CONFIG:Debug>:-fsanitize=address>)
		elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
			target_compile_options(${NCINE_APP} PUBLIC $<$<CONFIG:Debug>:-O1 -g -fsanitize=address -fsanitize-address-use-after-scope -fno-optimize-sibling-calls -fno-common -fno-omit-frame-pointer -rdynamic>)
			target_link_options(${NCINE_APP} PUBLIC $<$<CONFIG:Debug>:-fsanitize=address>)
		endif()
	endif()

	# Only in Debug
	if(NCINE_UNDEFINED_SANITIZER)
		# Add UBSan options as public so that targets linking the library will also use them
		if(EMSCRIPTEN)
			target_compile_options(${NCINE_APP} PUBLIC $<$<CONFIG:Debug>:-O1 -g -fsanitize=undefined>)
			target_link_options(${NCINE_APP} PUBLIC $<$<CONFIG:Debug>:-fsanitize=undefined>)
		elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
			target_compile_options(${NCINE_APP} PUBLIC $<$<CONFIG:Debug>:-O1 -g -fsanitize=undefined -fno-omit-frame-pointer>)
			target_link_options(${NCINE_APP} PUBLIC $<$<CONFIG:Debug>:-fsanitize=undefined>)
		endif()
	endif()

	# Only in Debug
	if(("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang") AND NCINE_CODE_COVERAGE)
		# Add code coverage options as public so that targets linking the library will also use them
		target_compile_options(${NCINE_APP} PUBLIC $<$<CONFIG:Debug>:--coverage>)
		target_link_options(${NCINE_APP} PUBLIC $<$<CONFIG:Debug>:--coverage>)
	endif()

	if(NCINE_WITH_TRACY)
		target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-g -fno-omit-frame-pointer -rdynamic>)
		if(MINGW OR MSYS)
			target_link_libraries(${NCINE_APP} PRIVATE dbghelp)
		elseif(NOT ANDROID AND NOT APPLE)
			target_link_libraries(${NCINE_APP} PRIVATE dl)
		endif()
	endif()

	if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		target_compile_options(${NCINE_APP} PRIVATE -fdiagnostics-color=auto)
		target_compile_options(${NCINE_APP} PRIVATE -Wall -Wno-old-style-cast -Wno-long-long -Wno-unused-parameter -Wno-ignored-qualifiers -Wno-variadic-macros -Wcast-align -Wno-multichar -Wno-switch -Wno-unknown-pragmas -Wno-reorder)

		target_link_options(${NCINE_APP} PRIVATE -Wno-free-nonheap-object)
		if(NCINE_DYNAMIC_LIBRARY)
			target_link_options(${NCINE_APP} PRIVATE -Wl,--no-undefined)
		endif()

		target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Debug>:-fvar-tracking-assignments>)

		# Extra optimizations in Release
		if(NINTENDO_SWITCH)
			# -Ofast is crashing on Nintendo Switch for some reason, use -O2 instead
			target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-O2 -funsafe-loop-optimizations -ftree-loop-if-convert-stores>)
		else()
			target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-Ofast -funsafe-loop-optimizations -ftree-loop-if-convert-stores>)
		endif()

		if(NCINE_LINKTIME_OPTIMIZATION AND NOT (MINGW OR MSYS OR ANDROID))
			target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-flto=auto>)
			target_link_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-flto=auto>)
		endif()

		if(NCINE_AUTOVECTORIZATION_REPORT)
			target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-fopt-info-vec-optimized>)
		endif()

		# Enabling strong stack protector of GCC 4.9
		if(NCINE_GCC_HARDENING AND NOT (MINGW OR MSYS))
			target_compile_options(${NCINE_APP} PUBLIC $<$<CONFIG:Release>:-Wformat -Wformat-security -fstack-protector-strong -fPIE -fPIC>)
			target_compile_definitions(${NCINE_APP} PUBLIC "$<$<CONFIG:Release>:_FORTIFY_SOURCE=2>")
			target_link_options(${NCINE_APP} PUBLIC $<$<CONFIG:Release>:-Wl,-z,relro -Wl,-z,now -pie>)
		endif()
	elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
		target_compile_options(${NCINE_APP} PRIVATE -fcolor-diagnostics)
		target_compile_options(${NCINE_APP} PRIVATE -Wall -Wno-old-style-cast -Wno-gnu-zero-variadic-macro-arguments -Wno-unused-parameter -Wno-variadic-macros -Wno-c++11-long-long -Wno-missing-braces -Wno-multichar -Wno-switch -Wno-unknown-pragmas -Wno-reorder-ctor -Wno-braced-scalar-init)

		if(NCINE_DYNAMIC_LIBRARY)
			target_link_options(${NCINE_APP} PRIVATE -Wl,-undefined,error)
		endif()

		if(NOT EMSCRIPTEN)
			# Extra optimizations in Release
			target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-Ofast>)
		endif()

		# Enable ThinLTO of Clang 4
		if(NCINE_LINKTIME_OPTIMIZATION)
			if(EMSCRIPTEN)
				target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-flto>)
				target_link_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-flto>)
			elseif(NOT (MINGW OR MSYS OR ANDROID))
				target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-flto=thin>)
				target_link_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-flto=thin>)
			endif()
		endif()

		if(NCINE_AUTOVECTORIZATION_REPORT)
			target_compile_options(${NCINE_APP} PRIVATE $<$<CONFIG:Release>:-Rpass=loop-vectorize -Rpass-analysis=loop-vectorize>)
		endif()
	endif()
endif()
