include_guard(DIRECTORY)

function(ncine_internal_get_enabled_languages out_var)
	# Limit flag modification to c-like code, we don't want to accidentally add incompatible flags to MSVC's RC or Swift
	set(languages_to_process ASM C CXX OBJC OBJCXX)
	get_property(globally_enabled_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
	set(enabled_languages "")
	foreach(lang ${languages_to_process})
		if(lang IN_LIST globally_enabled_languages)
			list(APPEND enabled_languages "${lang}")
		endif()
	endforeach()
	set(${out_var} "${enabled_languages}" PARENT_SCOPE)
endfunction()

function(ncine_internal_get_configs out_var)
	set(configs RELEASE RELWITHDEBINFO MINSIZEREL DEBUG)
	set(${out_var} "${configs}" PARENT_SCOPE)
endfunction()

function(ncine_internal_get_target_link_types out_var)
	set(target_link_types EXE SHARED MODULE STATIC)
	set(${out_var} "${target_link_types}" PARENT_SCOPE)
endfunction()

function(ncine_internal_add_flags_inner flag_var_name flags IN_CACHE)
	set(${flag_var_name} "${${flag_var_name}} ${flags}")
	string(STRIP "${${flag_var_name}}" ${flag_var_name})
	set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)

	if(IN_CACHE)
		set(mod_flags "$CACHE{${flag_var_name}} ${flags}")
		string(STRIP "${mod_flags}" mod_flags)
		get_property(help_text CACHE ${flag_var_name} PROPERTY HELPSTRING)
		set(${flag_var_name} "${mod_flags}" CACHE STRING "${help_text}" FORCE)
	endif()
endfunction()

function(ncine_internal_add_compiler_flags)
	cmake_parse_arguments(PARSE_ARGV 0 ARGS "IN_CACHE" "FLAGS" "CONFIGS;LANGUAGES")

	if(NOT ARGS_CONFIGS)
		message(FATAL_ERROR "You must specify at least one configuration for which to add the flags")
	endif()
	if(NOT ARGS_FLAGS)
		message(FATAL_ERROR "You must specify at least one flag to add")
	endif()

	if(ARGS_LANGUAGES)
		set(enabled_languages "${ARGS_LANGUAGES}")
	else()
		ncine_internal_get_enabled_languages(enabled_languages)
	endif()

	foreach(lang ${enabled_languages})
		foreach(config ${ARGS_CONFIGS})
			set(flag_var_name "CMAKE_${lang}_FLAGS_${config}")
			ncine_internal_add_flags_inner(${flag_var_name} "${ARGS_FLAGS}" "${ARGS_IN_CACHE}")
			set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)
		endforeach()
	endforeach()
endfunction()

function(ncine_internal_add_linker_flags)
	cmake_parse_arguments(PARSE_ARGV 0 ARGS "IN_CACHE" "FLAGS" "CONFIGS;TYPES")

	if(NOT ARGS_TYPES)
		message(FATAL_ERROR "You must specify at least one linker target type for which to add the flags")
	endif()
	if(NOT ARGS_CONFIGS)
		message(FATAL_ERROR "You must specify at least one configuration for which to add the flags")
	endif()
	if(NOT ARGS_FLAGS)
		message(FATAL_ERROR "You must specify at least one flag to add.")
	endif()

	foreach(config ${ARGS_CONFIGS})
		foreach(t ${ARGS_TYPES})
			set(flag_var_name "CMAKE_${t}_LINKER_FLAGS_${config}")
			ncine_internal_add_flags_inner(${flag_var_name} "${ARGS_FLAGS}" "${ARGS_IN_CACHE}")
			set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)
		endforeach()
	endforeach()
endfunction()

function(ncine_internal_remove_flags_inner flag_var_name flag_values IN_CACHE)
	cmake_parse_arguments(ARGS "REGEX" "" "" ${ARGN})
	set(replace_type REPLACE)
	if(ARGS_REGEX)
		list(PREPEND replace_type REGEX)
	endif()

	foreach(flag_value IN LISTS flag_values)
		string(${replace_type} "${flag_value}" " " ${flag_var_name} "${${flag_var_name}}")
	endforeach()
	string(STRIP "${${flag_var_name}}" ${flag_var_name})
	set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)

	if(IN_CACHE)
		set(mod_flags $CACHE{${flag_var_name}})
		foreach(flag_value IN LISTS flag_values)
			string(${replace_type} "${flag_value}" " " mod_flags "${mod_flags}")
		endforeach()
		string(STRIP "${mod_flags}" mod_flags)
		get_property(help_text CACHE ${flag_var_name} PROPERTY HELPSTRING)
		set(${flag_var_name} "${mod_flags}" CACHE STRING "${help_text}" FORCE)
	endif()
endfunction()

function(ncine_internal_remove_compiler_flags flags)
	cmake_parse_arguments(PARSE_ARGV 1 ARGS "IN_CACHE;REGEX" "" "CONFIGS;LANGUAGES")

	if("${flags}" STREQUAL "")
		message(WARNING "You must specify at least one flag to remove")
		return()
	endif()

	if(ARGS_LANGUAGES)
		set(languages "${ARGS_LANGUAGES}")
	else()
		ncine_internal_get_enabled_languages(languages)
	endif()

	if(ARGS_CONFIGS)
		set(configs "${ARGS_CONFIGS}")
	else()
		ncine_internal_get_configs(configs)
	endif()

	if(ARGS_REGEX)
		list(APPEND extra_options "REGEX")
	endif()

	foreach(lang ${languages})
		set(flag_var_name "CMAKE_${lang}_FLAGS")
		ncine_internal_remove_flags_inner(${flag_var_name} "${flags}" "${ARGS_IN_CACHE}" ${extra_options})
		set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)
		foreach(config ${configs})
			set(flag_var_name "CMAKE_${lang}_FLAGS_${config}")
			ncine_internal_remove_flags_inner(${flag_var_name} "${flags}" "${ARGS_IN_CACHE}" ${extra_options})
			set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)
		endforeach()
	endforeach()
endfunction()

function(ncine_internal_replace_flags_inner flag_var_name match_string replace_string IN_CACHE)
	if(match_string STREQUAL "" AND "${${flag_var_name}}" STREQUAL "")
		set(${flag_var_name} "${replace_string}" PARENT_SCOPE)
	else()
		string(REPLACE "${match_string}" "${replace_string}" ${flag_var_name} "${${flag_var_name}}")
		string(STRIP "${${flag_var_name}}" ${flag_var_name})
		set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)
	endif()

	if(IN_CACHE)
		get_property(help_text CACHE "${flag_var_name}" PROPERTY HELPSTRING)

		if(match_string STREQUAL "" AND "$CACHE{${flag_var_name}}" STREQUAL "")
			set(${flag_var_name} "${replace_string}" CACHE STRING "${help_text}" FORCE)
		else()
			set(mod_flags "$CACHE{${flag_var_name}}")
			string(REPLACE "${match_string}" "${replace_string}" mod_flags "${mod_flags}")
			string(STRIP "${mod_flags}" mod_flags)
			set(${flag_var_name} "${mod_flags}" CACHE STRING "${help_text}" FORCE)
		endif()
	endif()
endfunction()

function(ncine_internal_replace_compiler_flags match_string replace_string)
	cmake_parse_arguments(PARSE_ARGV 2 ARGS "IN_CACHE" "" "CONFIGS;LANGUAGES")

	if(NOT ARGS_CONFIGS)
		message(FATAL_ERROR "You must specify at least one configuration for which to replace the flags")
	endif()

	if(ARGS_LANGUAGES)
		set(enabled_languages "${ARGS_LANGUAGES}")
	else()
		qt_internal_get_enabled_languages_for_flag_manipulation(enabled_languages)
	endif()

	foreach(lang ${enabled_languages})
		foreach(config ${ARGS_CONFIGS})
			set(flag_var_name "CMAKE_${lang}_FLAGS_${config}")
			ncine_internal_replace_flags_inner(${flag_var_name} "${match_string}" "${replace_string}" "${ARGS_IN_CACHE}")
			set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)
		endforeach()
	endforeach()
endfunction()

function(ncine_internal_replace_linker_flags match_string replace_string)
	cmake_parse_arguments(PARSE_ARGV 2 ARGS "IN_CACHE" "" "CONFIGS;TYPES")

	if(NOT ARGS_TYPES)
		message(FATAL_ERROR "You must specify at least one linker target type for which to replace the flags")
	endif()
	if(NOT ARGS_CONFIGS)
		message(FATAL_ERROR "You must specify at least one configuration for which to replace the flags")
	endif()

	foreach(config ${ARGS_CONFIGS})
		foreach(t ${ARGS_TYPES})
			set(flag_var_name "CMAKE_${t}_LINKER_FLAGS_${config}")
			ncine_internal_replace_flags_inner(${flag_var_name} "${match_string}" "${replace_string}" "${ARGS_IN_CACHE}")
			set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)
		endforeach()
	endforeach()
endfunction()

function(ncine_normalize_optimizations)
	ncine_internal_get_enabled_languages(enabled_languages)
	ncine_internal_get_configs(configs)
	ncine_internal_get_target_link_types(target_link_types)

	if(MSVC)
		# Handle /INCREMENTAL flag which should not be enabled for Release configurations
		set(flag_values "/INCREMENTAL:YES" "/INCREMENTAL:NO" "/INCREMENTAL")
		foreach(flag_value ${flag_values})
			ncine_internal_replace_linker_flags(
					"${flag_value}" ""
					CONFIGS ${configs}
					TYPES ${target_link_types}
					IN_CACHE)
		endforeach()

		set(flag_value "/INCREMENTAL:NO")
		ncine_internal_add_linker_flags(
				FLAGS "${flag_value}"
				CONFIGS RELEASE RELWITHDEBINFO MINSIZEREL
				TYPES EXE SHARED MODULE # When linking static libraries, link.exe can't recognize this parameter, clang-cl will error out.
				IN_CACHE)

		ncine_internal_remove_compiler_flags("(^| )/EH[scra-]*( |$)" LANGUAGES CXX CONFIGS ${configs} IN_CACHE REGEX)
	endif()

	# Legacy Android toolchain file adds the `-g` flag to CMAKE_<LANG>_FLAGS, as result, our release build ends up containing debug symbols
	if(ANDROID AND ANDROID_COMPILER_FLAGS MATCHES "(^| )-g")
		ncine_internal_remove_compiler_flags("-g")
		ncine_internal_add_compiler_flags(FLAGS "-g" CONFIGS DEBUG RELWITHDEBINFO)
	endif()

	# Update all relevant flags in the calling scope
	foreach(lang ${enabled_languages})
		set(flag_var_name "CMAKE_${lang}_FLAGS")
		set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)

		foreach(config ${configs})
			set(flag_var_name "CMAKE_${lang}_FLAGS_${config}")
			set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)
		endforeach()
	endforeach()

	foreach(t ${target_link_types})
		set(flag_var_name "CMAKE_${t}_LINKER_FLAGS")
		set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)

		foreach(config ${configs})
			set(flag_var_name "CMAKE_${t}_LINKER_FLAGS_${config}")
			set(${flag_var_name} "${${flag_var_name}}" PARENT_SCOPE)
		endforeach()
	endforeach()
endfunction()

function(ncine_apply_compiler_options target)
	cmake_parse_arguments(PARSE_ARGV 1 ARGS "ALLOW_EXCEPTIONS" "" "")

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
		target_compile_definitions(${target} PUBLIC $<$<CONFIG:Debug>:DEATH_DEBUG>)
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
			target_compile_options(${target} PRIVATE "/await")
			# Workaround for "error C2039: 'wait_for': is not a member of 'winrt::impl'"
			target_compile_options(${target} PRIVATE "/Zc:twoPhase-")
		endif()
	endif()

	if(MSVC)
		# Enable parallel compilation and force UTF-8
		target_compile_options(${target} PRIVATE "/MP" "/utf-8")
		# Enable standards-conforming compiler behavior
		if(MSVC_VERSION GREATER_EQUAL 1913)
			target_compile_options(${target} PRIVATE "/permissive-")
		endif()
		# Always use the non-debug version of the runtime library
		if(VC_LTL_FOUND)
			set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
		else()
			set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
		endif()

		# Exceptions
		if(ARGS_ALLOW_EXCEPTIONS)
			target_compile_options(${target} PRIVATE "/EHsc")
			if((MSVC_VERSION GREATER_EQUAL 1929) AND NOT ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang"))
				# Use the undocumented compiler flag to make our binary smaller on x64
				# https://devblogs.microsoft.com/cppblog/making-cpp-exception-handling-smaller-x64/
				target_compile_options(${target} PRIVATE "/d2FH4")
			endif()
		else()
			# It seems MSVC 19.38.33135.0 can't link the project for 32-bit x86 without "/EHsc"
			if("${CMAKE_GENERATOR_PLATFORM}" STREQUAL "Win32")
				target_compile_options(${target} PRIVATE "/EHsc")
			else()
				target_compile_options(${target} PRIVATE "/EHs-c-" "/wd4530" "/wd4577")
			endif()
			target_compile_definitions(${target} PRIVATE "_HAS_EXCEPTIONS=0")
		endif()

		# Extra optimizations in Release
		target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/fp:fast /O2 /Oi /Qpar /Gy>)
		# Include PDB debug information in Release and enable hot reloading in Debug
		if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
			# clang-cl doesn't support EditAndContinue
			if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25.0")
				set_property(TARGET ${target} PROPERTY MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
			else()
				target_compile_options(${target} PRIVATE /Zi)
			endif()
		else()
			if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25.0")
				set_property(TARGET ${target} PROPERTY MSVC_DEBUG_INFORMATION_FORMAT $<IF:$<CONFIG:Debug>,EditAndContinue,ProgramDatabase>)
			else()
				target_compile_options(${target} PRIVATE $<IF:$<CONFIG:Debug>,/ZI,/Zi>)
			endif()
		endif()
		target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL>)

		# Turn off SAFESEH because of OpenAL on x86 and also UAC
		target_link_options(${target} PRIVATE "/SAFESEH:NO" "/MANIFESTUAC:NO")
		target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:NOICF>)

		# Specifies the architecture for code generation (IA32, SSE, SSE2, AVX, AVX2, AVX512)
		if(NCINE_ARCH_EXTENSIONS)
			if(target_is_executable)
				message(STATUS "Specified architecture extensions for code generation: ${NCINE_ARCH_EXTENSIONS}")
			endif()
			target_compile_options(${target} PRIVATE "/arch:${NCINE_ARCH_EXTENSIONS}")
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
		#target_link_options(${target} PRIVATE $<IF:$<CONFIG:Debug>,/INCREMENTAL,/INCREMENTAL:NO>)

		if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
			target_compile_options(${target} PRIVATE "-Wno-switch" "-Wno-unknown-pragmas" "-Wno-reorder-ctor" "-Wno-braced-scalar-init" "-Wno-deprecated-builtins")
		endif()
	else() # GCC and LLVM
		if(ARGS_ALLOW_EXCEPTIONS)
			target_compile_options(${target} PRIVATE "-fexceptions")
		else()
			target_compile_options(${target} PRIVATE "-fno-exceptions")
		endif()
		target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-ffast-math>)

		#if(NCINE_DYNAMIC_LIBRARY)
		#	target_compile_options(${target} PRIVATE "-fvisibility=hidden" "-fvisibility-inlines-hidden")
		#endif()

		if(MINGW OR MSYS)
			target_link_options(${target} PUBLIC "-municode")
		endif()

		if(NCINE_ARCH_EXTENSIONS AND UNIX AND NOT APPLE AND NOT ANDROID AND NOT NINTENDO_SWITCH)
			if(target_is_executable)
				message(STATUS "Specified architecture extensions for code generation: ${NCINE_ARCH_EXTENSIONS}")
			endif()
			target_compile_options(${target} PRIVATE "-march=${NCINE_ARCH_EXTENSIONS}")
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
				target_compile_options(${target} PUBLIC $<$<CONFIG:Debug>:-O1 -g -fsanitize=address -fsanitize-address-use-after-scope -fno-optimize-sibling-calls -fno-common -fno-omit-frame-pointer>)
				target_link_options(${target} PUBLIC $<$<CONFIG:Debug>:-fsanitize=address>)
				
				# Don't add `-rdynamic` on GCC/MinGW
				if(NOT ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND (MINGW OR MSYS)))
					target_compile_options(${target} PUBLIC $<$<CONFIG:Debug>:-rdynamic>)
				endif()
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
			target_compile_options(${target} PRIVATE "-fdiagnostics-color=auto")
			target_compile_options(${target} PRIVATE "-Wall" "-Wno-old-style-cast" "-Wno-long-long" "-Wno-unused-parameter" "-Wno-ignored-qualifiers"
				"-Wno-variadic-macros" "-Wcast-align" "-Wno-multichar" "-Wno-switch" "-Wno-unknown-pragmas" "-Wno-reorder")

			target_link_options(${target} PRIVATE "-Wno-free-nonheap-object")
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
				target_compile_definitions(${target} PUBLIC $<$<CONFIG:Release>:_FORTIFY_SOURCE=2>)
				target_link_options(${target} PUBLIC $<$<CONFIG:Release>:-Wl,-z,relro -Wl,-z,now -pie>)
			endif()
		elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
			target_compile_options(${target} PRIVATE "-fcolor-diagnostics")
			target_compile_options(${target} PRIVATE "-Wall" "-Wno-old-style-cast" "-Wno-gnu-zero-variadic-macro-arguments" "-Wno-unused-parameter"
				"-Wno-variadic-macros" "-Wno-c++11-long-long" "-Wno-missing-braces" "-Wno-multichar" "-Wno-switch" "-Wno-unknown-pragmas"
				"-Wno-reorder-ctor" "-Wno-braced-scalar-init")

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
	ncine_apply_compiler_options(${ARGV})
endfunction()