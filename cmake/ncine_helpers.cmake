function(ncine_add_compiler_options target)
	get_target_property(target_type ${target} TYPE)
	set(target_is_executable FALSE)
	if(target_type STREQUAL "INTERFACE_LIBRARY")
		return()
	elseif(target_type STREQUAL "EXECUTABLE")
		set(target_is_executable TRUE)
	endif()

	target_compile_features(${target} PUBLIC cxx_std_17)
	set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
	target_compile_definitions(${target} PUBLIC "CMAKE_BUILD")

	if(DEATH_DEBUG)
		target_compile_definitions(${target} PUBLIC "DEATH_DEBUG")
	else()
		target_compile_definitions(${target} PUBLIC "$<$<CONFIG:Debug>:DEATH_DEBUG>")
	endif()
	if(DEATH_TRACE)
		if(target_is_executable)
			message(STATUS "Runtime event tracing is enabled")
		endif()
		target_compile_definitions(${target} PUBLIC "DEATH_TRACE")
	endif()

	if(NINTENDO_SWITCH)
		target_compile_definitions(${target} PUBLIC "DEATH_TARGET_SWITCH")
	elseif(WIN32)
		# Force Unicode mode on Windows
		target_compile_definitions(${target} PRIVATE "_UNICODE" "UNICODE")
		if(WINDOWS_PHONE OR WINDOWS_STORE)
			target_compile_definitions(${target} PUBLIC "DEATH_TARGET_WINDOWS_RT")

			# Workaround for "error C1189: The <experimental/coroutine> and <experimental/resumable> headers are only supported with /await"
			target_compile_options(${target} PRIVATE /await)
			# Workaround for "error C2039: 'wait_for': is not a member of 'winrt::impl'"
			target_compile_options(${target} PRIVATE /Zc:twoPhase-)
		endif()
	endif()

	if(MSVC)
		# Enable parallel compilation and force UTF-8
		target_compile_options(${target} PRIVATE /MP /utf-8)
		# Always use the non-debug version of the runtime library
		#target_compile_options(${target} PRIVATE $<IF:$<BOOL:${VC_LTL_FOUND}>,/MT,/MD>)
		set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "$<IF:$<BOOL:${VC_LTL_FOUND}>,MultiThreaded,MultiThreadedDLL>")
		# Disable exceptions
		target_compile_definitions(${target} PRIVATE "_HAS_EXCEPTIONS=0")
		target_compile_options(${target} PRIVATE /EHsc)
		# Extra optimizations in Release
		target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/fp:fast /O2 /Oi /Qpar /Gy>)
		# Include PDB debug information in Release and enable hot reloading in Debug
		if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25.0")
			set_property(TARGET ${target} PROPERTY CMAKE_MSVC_DEBUG_INFORMATION_FORMAT $<IF:$<CONFIG:Debug>,EditAndContinue,ProgramDatabase>)
		else()
			target_compile_options(${target} PRIVATE $<IF:$<CONFIG:Debug>,/ZI,/Zi>)
		endif()
		target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL>)
		# Turn off SAFESEH because of OpenAL on x86 and also UAC
		target_link_options(${target} PRIVATE /SAFESEH:NO /MANIFESTUAC:NO)
		target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:NOICF>)
		# Specifies the architecture for code generation (IA32, SSE, SSE2, AVX, AVX2, AVX512)
		if(NCINE_ARCH_EXTENSIONS)
			if(target_is_executable)
				message(STATUS "Specified architecture extensions for code generation: ${NCINE_ARCH_EXTENSIONS}")
			endif()
			target_compile_options(${target} PRIVATE /arch:${NCINE_ARCH_EXTENSIONS})
		endif()

		# Enable Whole Program Optimization
		if(NCINE_LINKTIME_OPTIMIZATION)
			target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/GL>)
			target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/LTCG>)
		endif()

		if(NCINE_AUTOVECTORIZATION_REPORT)
			target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/Qvec-report:2 /Qpar-report:2>)
		endif()

		# Suppress linker warning about templates
		target_compile_options(${target} PUBLIC "/wd4251")
		# Suppress warnings about "conversion from '<bigger int type>' to '<smaller int type>'",
		# "conversion from 'size_t' to '<smaller int type>', possible loss of data"
		target_compile_options(${target} PUBLIC "/wd4244" "/wd4267")

		# Adjust incremental linking
		target_link_options(${target} PRIVATE $<IF:$<CONFIG:Debug>,/INCREMENTAL,/INCREMENTAL:NO>)

		if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
			target_compile_options(${target} PRIVATE -Wno-switch -Wno-unknown-pragmas -Wno-reorder-ctor -Wno-braced-scalar-init -Wno-deprecated-builtins)
		endif()
	else() # GCC and LLVM
		target_compile_options(${target} PRIVATE -fno-exceptions)
		target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-ffast-math>)

		#if(NCINE_DYNAMIC_LIBRARY)
		#	target_compile_options(${target} PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
		#endif()

		if(MINGW OR MSYS)
			target_link_options(${target} PUBLIC -municode)
		endif()

		if(NCINE_ARCH_EXTENSIONS AND UNIX AND NOT APPLE AND NOT ANDROID AND NOT NINTENDO_SWITCH)
			if(target_is_executable)
				message(STATUS "Specified architecture extensions for code generation: ${NCINE_ARCH_EXTENSIONS}")
			endif()
			target_compile_options(${target} PRIVATE -march=${NCINE_ARCH_EXTENSIONS})
		endif()
	
		# Only in Debug - preserve debug information, it's probably added automatically on other platforms
		if(EMSCRIPTEN)
			target_compile_options(${target} PUBLIC $<$<CONFIG:Debug>:-g>)
			target_link_options(${target} PUBLIC $<$<CONFIG:Debug>:-g>)
		endif()

		# Only in Debug
		if(NCINE_ADDRESS_SANITIZER)
			# Add ASan options as public so that targets linking the library will also use them
			if(EMSCRIPTEN)
				target_compile_options(${target} PUBLIC $<$<CONFIG:Debug>:-O1 -g -fsanitize=address>) # Needs "ALLOW_MEMORY_GROWTH" which is already passed to the linker
				target_link_options(${target} PUBLIC $<$<CONFIG:Debug>:-fsanitize=address>)
			elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
				target_compile_options(${target} PUBLIC $<$<CONFIG:Debug>:-O1 -g -fsanitize=address -fsanitize-address-use-after-scope -fno-optimize-sibling-calls -fno-common -fno-omit-frame-pointer -rdynamic>)
				target_link_options(${target} PUBLIC $<$<CONFIG:Debug>:-fsanitize=address>)
			endif()
		endif()

		# Only in Debug
		if(NCINE_UNDEFINED_SANITIZER)
			# Add UBSan options as public so that targets linking the library will also use them
			if(EMSCRIPTEN)
				target_compile_options(${target} PUBLIC $<$<CONFIG:Debug>:-O1 -g -fsanitize=undefined>)
				target_link_options(${target} PUBLIC $<$<CONFIG:Debug>:-fsanitize=undefined>)
			elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
				target_compile_options(${target} PUBLIC $<$<CONFIG:Debug>:-O1 -g -fsanitize=undefined -fno-omit-frame-pointer>)
				target_link_options(${target} PUBLIC $<$<CONFIG:Debug>:-fsanitize=undefined>)
			endif()
		endif()

		# Only in Debug
		if(("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang") AND NCINE_CODE_COVERAGE)
			# Add code coverage options as public so that targets linking the library will also use them
			target_compile_options(${target} PUBLIC $<$<CONFIG:Debug>:--coverage>)
			target_link_options(${target} PUBLIC $<$<CONFIG:Debug>:--coverage>)
		endif()

		if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
			target_compile_options(${target} PRIVATE -fdiagnostics-color=auto)
			target_compile_options(${target} PRIVATE -Wall -Wno-old-style-cast -Wno-long-long -Wno-unused-parameter -Wno-ignored-qualifiers -Wno-variadic-macros -Wcast-align -Wno-multichar -Wno-switch -Wno-unknown-pragmas -Wno-reorder)

			target_link_options(${target} PRIVATE -Wno-free-nonheap-object)
			#if(NCINE_DYNAMIC_LIBRARY)
			#	target_link_options(${target} PRIVATE -Wl,--no-undefined)
			#endif()

			target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:-fvar-tracking-assignments>)

			# Extra optimizations in Release
			if(NINTENDO_SWITCH)
				# -Ofast is crashing on Nintendo Switch for some reason, use -O2 instead
				target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-O2>)
			else()
				target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-Ofast>)
			endif()
			target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-funsafe-loop-optimizations -ftree-loop-if-convert-stores>)

			if(NCINE_LINKTIME_OPTIMIZATION AND NOT (MINGW OR MSYS OR ANDROID))
				target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-flto=auto>)
				target_link_options(${target} PRIVATE $<$<CONFIG:Release>:-flto=auto>)
			endif()

			if(NCINE_AUTOVECTORIZATION_REPORT)
				target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-fopt-info-vec-optimized>)
			endif()

			# Enabling strong stack protector of GCC 4.9
			if(NCINE_GCC_HARDENING AND NOT (MINGW OR MSYS))
				target_compile_options(${target} PUBLIC $<$<CONFIG:Release>:-Wformat -Wformat-security -fstack-protector-strong -fPIE -fPIC>)
				target_compile_definitions(${target} PUBLIC "$<$<CONFIG:Release>:_FORTIFY_SOURCE=2>")
				target_link_options(${target} PUBLIC $<$<CONFIG:Release>:-Wl,-z,relro -Wl,-z,now -pie>)
			endif()
		elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
			target_compile_options(${target} PRIVATE -fcolor-diagnostics)
			target_compile_options(${target} PRIVATE -Wall -Wno-old-style-cast -Wno-gnu-zero-variadic-macro-arguments -Wno-unused-parameter -Wno-variadic-macros -Wno-c++11-long-long -Wno-missing-braces -Wno-multichar -Wno-switch -Wno-unknown-pragmas -Wno-reorder-ctor -Wno-braced-scalar-init)

			#if(NCINE_DYNAMIC_LIBRARY)
			#	target_link_options(${target} PRIVATE -Wl,-undefined,error)
			#endif()

			if(NOT EMSCRIPTEN)
				# Extra optimizations in Release
				target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-Ofast>)
			endif()

			# Enable ThinLTO of Clang 4
			if(NCINE_LINKTIME_OPTIMIZATION)
				if(EMSCRIPTEN)
					target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-flto>)
					target_link_options(${target} PRIVATE $<$<CONFIG:Release>:-flto>)
				elseif(NOT (MINGW OR MSYS OR ANDROID))
					target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-flto=thin>)
					target_link_options(${target} PRIVATE $<$<CONFIG:Release>:-flto=thin>)
				endif()
			endif()

			if(NCINE_AUTOVECTORIZATION_REPORT)
				target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-Rpass=loop-vectorize -Rpass-analysis=loop-vectorize>)
			endif()
		endif()
	endif()
endfunction()

function(ncine_add_dependency target target_type)
	set(CMAKE_FOLDER "Dependencies")
	add_library(${target} ${target_type})
	ncine_add_compiler_options(${target})
endfunction()