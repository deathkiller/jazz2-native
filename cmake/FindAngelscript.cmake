include(ncine_helpers)

if(NOT TARGET Angelscript::Angelscript)
	if(NCINE_DOWNLOAD_DEPENDENCIES)
		# Try to build `libopenmpt` from source
		#set(ANGELSCRIPT_URL "https://www.angelcode.com/angelscript/sdk/files/angelscript_2.36.1.zip")
		set(ANGELSCRIPT_URL "https://github.com/codecat/angelscript-mirror/archive/refs/heads/master.tar.gz")
		message(STATUS "Downloading dependencies from \"${ANGELSCRIPT_URL}\"...")
		
		include(FetchContent)
		FetchContent_Declare(
			AngelscriptGit
			DOWNLOAD_EXTRACT_TIMESTAMP TRUE
			URL ${ANGELSCRIPT_URL}
		)
		FetchContent_MakeAvailable(AngelscriptGit)

		ncine_add_dependency(Angelscript STATIC)

		set(ANGELSCRIPT_DIR "${angelscriptgit_SOURCE_DIR}/sdk/angelscript/")
		set(ANGELSCRIPT_INCLUDE_DIR "${ANGELSCRIPT_DIR}/include/")
		set_target_properties(Angelscript PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES ${ANGELSCRIPT_INCLUDE_DIR})
		target_link_libraries(Angelscript Threads::Threads)
		target_compile_definitions(Angelscript PRIVATE "AS_NO_EXCEPTIONS")
		if(MSVC)
			target_compile_definitions(Angelscript PRIVATE "_CRT_SECURE_NO_WARNINGS")
		endif()

		set(ANGELSCRIPT_HEADERS
			${ANGELSCRIPT_INCLUDE_DIR}/angelscript.h
			${ANGELSCRIPT_DIR}/source/as_array.h
			${ANGELSCRIPT_DIR}/source/as_builder.h
			${ANGELSCRIPT_DIR}/source/as_bytecode.h
			${ANGELSCRIPT_DIR}/source/as_callfunc.h
			${ANGELSCRIPT_DIR}/source/as_compiler.h
			${ANGELSCRIPT_DIR}/source/as_config.h
			${ANGELSCRIPT_DIR}/source/as_configgroup.h
			${ANGELSCRIPT_DIR}/source/as_context.h
			${ANGELSCRIPT_DIR}/source/as_criticalsection.h
			${ANGELSCRIPT_DIR}/source/as_datatype.h
			${ANGELSCRIPT_DIR}/source/as_debug.h
			${ANGELSCRIPT_DIR}/source/as_generic.h
			${ANGELSCRIPT_DIR}/source/as_map.h
			${ANGELSCRIPT_DIR}/source/as_memory.h
			${ANGELSCRIPT_DIR}/source/as_module.h
			${ANGELSCRIPT_DIR}/source/as_objecttype.h
			${ANGELSCRIPT_DIR}/source/as_outputbuffer.h
			${ANGELSCRIPT_DIR}/source/as_parser.h
			${ANGELSCRIPT_DIR}/source/as_property.h
			${ANGELSCRIPT_DIR}/source/as_restore.h
			${ANGELSCRIPT_DIR}/source/as_scriptcode.h
			${ANGELSCRIPT_DIR}/source/as_scriptengine.h
			${ANGELSCRIPT_DIR}/source/as_scriptfunction.h
			${ANGELSCRIPT_DIR}/source/as_scriptnode.h
			${ANGELSCRIPT_DIR}/source/as_scriptobject.h
			${ANGELSCRIPT_DIR}/source/as_string.h
			${ANGELSCRIPT_DIR}/source/as_string_util.h
			${ANGELSCRIPT_DIR}/source/as_texts.h
			${ANGELSCRIPT_DIR}/source/as_thread.h
			${ANGELSCRIPT_DIR}/source/as_tokendef.h
			${ANGELSCRIPT_DIR}/source/as_tokenizer.h
			${ANGELSCRIPT_DIR}/source/as_typeinfo.h
			${ANGELSCRIPT_DIR}/source/as_variablescope.h
		)

		set(ANGELSCRIPT_SOURCES
			${ANGELSCRIPT_DIR}/source/as_atomic.cpp
			${ANGELSCRIPT_DIR}/source/as_builder.cpp
			${ANGELSCRIPT_DIR}/source/as_bytecode.cpp
			${ANGELSCRIPT_DIR}/source/as_callfunc.cpp
			${ANGELSCRIPT_DIR}/source/as_callfunc_mips.cpp
			${ANGELSCRIPT_DIR}/source/as_callfunc_x86.cpp
			${ANGELSCRIPT_DIR}/source/as_callfunc_x64_gcc.cpp
			${ANGELSCRIPT_DIR}/source/as_callfunc_x64_msvc.cpp
			${ANGELSCRIPT_DIR}/source/as_callfunc_x64_mingw.cpp
			${ANGELSCRIPT_DIR}/source/as_compiler.cpp
			${ANGELSCRIPT_DIR}/source/as_configgroup.cpp
			${ANGELSCRIPT_DIR}/source/as_context.cpp
			${ANGELSCRIPT_DIR}/source/as_datatype.cpp
			${ANGELSCRIPT_DIR}/source/as_gc.cpp
			${ANGELSCRIPT_DIR}/source/as_generic.cpp
			${ANGELSCRIPT_DIR}/source/as_globalproperty.cpp
			${ANGELSCRIPT_DIR}/source/as_memory.cpp
			${ANGELSCRIPT_DIR}/source/as_module.cpp
			${ANGELSCRIPT_DIR}/source/as_objecttype.cpp
			${ANGELSCRIPT_DIR}/source/as_outputbuffer.cpp
			${ANGELSCRIPT_DIR}/source/as_parser.cpp
			${ANGELSCRIPT_DIR}/source/as_restore.cpp
			${ANGELSCRIPT_DIR}/source/as_scriptcode.cpp
			${ANGELSCRIPT_DIR}/source/as_scriptengine.cpp
			${ANGELSCRIPT_DIR}/source/as_scriptfunction.cpp
			${ANGELSCRIPT_DIR}/source/as_scriptnode.cpp
			${ANGELSCRIPT_DIR}/source/as_scriptobject.cpp
			${ANGELSCRIPT_DIR}/source/as_string.cpp
			${ANGELSCRIPT_DIR}/source/as_string_util.cpp
			${ANGELSCRIPT_DIR}/source/as_thread.cpp
			${ANGELSCRIPT_DIR}/source/as_tokenizer.cpp
			${ANGELSCRIPT_DIR}/source/as_typeinfo.cpp
			${ANGELSCRIPT_DIR}/source/as_variablescope.cpp
		)

		if(MSVC AND CMAKE_CL_64)
			enable_language(ASM_MASM)
			if(CMAKE_ASM_MASM_COMPILER_WORKS)
				set(ANGELSCRIPT_SOURCES ${ANGELSCRIPT_SOURCES} ${ANGELSCRIPT_DIR}/source/as_callfunc_x64_msvc_asm.asm)
			else()
				message(FATAL ERROR "MSVC x86_64 target requires a working assembler")
			endif()
		endif()
		
		set(ARCHFLAGS "${CMAKE_SYSTEM_PROCESSOR}")
		if(APPLE AND NOT IOS)
			if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
				set(ARCHFLAGS "aarch64")
			endif()
		endif()

		if(${ARCHFLAGS} MATCHES "^arm")
			enable_language(ASM)
			if(CMAKE_ASM_COMPILER_WORKS)
				set(ANGELSCRIPT_SOURCES ${ANGELSCRIPT_SOURCES} ${ANGELSCRIPT_DIR}/source/as_callfunc_arm.cpp ${ANGELSCRIPT_DIR}/source/as_callfunc_arm_gcc.S)
				set_property(SOURCE ${ANGELSCRIPT_DIR}/source/as_callfunc_arm_gcc.S APPEND PROPERTY COMPILE_FLAGS " -Wa,-mimplicit-it=always")
			else()
				message(FATAL ERROR "ARM target requires a working assembler")
			endif()
		endif()

		if(${ARCHFLAGS} MATCHES "^aarch64")
			enable_language(ASM)
			if(CMAKE_ASM_COMPILER_WORKS)
				if(NOT APPLE)
					set(ANGELSCRIPT_SOURCES ${ANGELSCRIPT_SOURCES} ${ANGELSCRIPT_DIR}/source/as_callfunc_arm64.cpp ${ANGELSCRIPT_DIR}/source/as_callfunc_arm64_gcc.S)
				else()
					set(ANGELSCRIPT_SOURCES ${ANGELSCRIPT_SOURCES} ${ANGELSCRIPT_DIR}/source/as_callfunc_arm64.cpp ${ANGELSCRIPT_DIR}/source/as_callfunc_arm64_xcode.S)
				endif()
			else()
				message(FATAL ERROR "ARM target requires a working assembler")
			endif()
		endif()

		target_sources(Angelscript PRIVATE ${ANGELSCRIPT_SOURCES} ${ANGELSCRIPT_HEADERS})
		target_include_directories(Angelscript PRIVATE "${ANGELSCRIPT_INCLUDE_DIR}" " ${ANGELSCRIPT_DIR}/source")

		set(ANGELSCRIPT_FOUND TRUE)
		set(ANGELSCRIPT_STATIC TRUE)
		mark_as_advanced(ANGELSCRIPT_STATIC)
	endif()
endif()