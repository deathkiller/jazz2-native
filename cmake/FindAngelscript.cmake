include(ncine_helpers)

if(NOT TARGET Angelscript)
	# Built from the bundled sources (2.39.0 WIP), add-ons are not included
	set(ANGELSCRIPT_DIR "${NCINE_SOURCE_DIR}/Dependencies/angelscript")
	set(ANGELSCRIPT_INCLUDE_DIR "${ANGELSCRIPT_DIR}/include")

	ncine_add_dependency(Angelscript STATIC)
	set_target_properties(Angelscript PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES ${ANGELSCRIPT_INCLUDE_DIR})
	if(TARGET Threads::Threads)
		target_link_libraries(Angelscript Threads::Threads)
	endif()
	target_compile_definitions(Angelscript PRIVATE "AS_NO_EXCEPTIONS")
	if(MSVC)
		target_compile_definitions(Angelscript PRIVATE "_CRT_SECURE_NO_WARNINGS")
	endif()
	if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64" AND UNIX AND NOT APPLE)
		set_target_properties(Angelscript PROPERTIES POSITION_INDEPENDENT_CODE ON)
	endif()

	set(ANGELSCRIPT_HEADERS
		${ANGELSCRIPT_INCLUDE_DIR}/angelscript.h
		${ANGELSCRIPT_DIR}/source/as_array.h
		${ANGELSCRIPT_DIR}/source/as_atomic.h
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
		${ANGELSCRIPT_DIR}/source/as_gc.h
		${ANGELSCRIPT_DIR}/source/as_generic.h
		${ANGELSCRIPT_DIR}/source/as_map.h
		${ANGELSCRIPT_DIR}/source/as_memory.h
		${ANGELSCRIPT_DIR}/source/as_module.h
		${ANGELSCRIPT_DIR}/source/as_namespace.h
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
		${ANGELSCRIPT_DIR}/source/as_symboltable.h
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
		${ANGELSCRIPT_DIR}/source/as_callfunc_ppc.cpp
		${ANGELSCRIPT_DIR}/source/as_callfunc_ppc_64.cpp
		${ANGELSCRIPT_DIR}/source/as_callfunc_sh4.cpp
		${ANGELSCRIPT_DIR}/source/as_callfunc_x86.cpp
		${ANGELSCRIPT_DIR}/source/as_callfunc_x64_gcc.cpp
		${ANGELSCRIPT_DIR}/source/as_callfunc_x64_msvc.cpp
		${ANGELSCRIPT_DIR}/source/as_callfunc_x64_mingw.cpp
		${ANGELSCRIPT_DIR}/source/as_callfunc_xenon.cpp
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

	set(ARCHFLAGS "${CMAKE_SYSTEM_PROCESSOR}")
	if(APPLE AND NOT IOS)
		if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
			set(ARCHFLAGS "aarch64")
		endif()
	endif()
	string(TOLOWER "${ARCHFLAGS}" ARCHFLAGS)

	# Native calling conventions need a bit of assembly on some targets, the rest is chosen by `as_config.h`
	if(MSVC)
		if(ARCHFLAGS MATCHES "^(arm64|aarch64)")
			enable_language(ASM_MARMASM)
			list(APPEND ANGELSCRIPT_SOURCES ${ANGELSCRIPT_DIR}/source/as_callfunc_arm64.cpp ${ANGELSCRIPT_DIR}/source/as_callfunc_arm64_msvc.asm)
		elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
			enable_language(ASM_MASM)
			if(CMAKE_ASM_MASM_COMPILER_WORKS)
				list(APPEND ANGELSCRIPT_SOURCES ${ANGELSCRIPT_DIR}/source/as_callfunc_x64_msvc_asm.asm)
			else()
				message(FATAL_ERROR "MSVC x86_64 target requires a working assembler")
			endif()
		endif()
	elseif(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND ARCHFLAGS MATCHES "^(x86_64|amd64)")
		enable_language(ASM)
		list(APPEND ANGELSCRIPT_SOURCES ${ANGELSCRIPT_DIR}/source/as_callfunc_x64_msvc_clang.S)
	elseif(ARCHFLAGS MATCHES "^(aarch64|arm64)")
		enable_language(ASM)
		if(CMAKE_ASM_COMPILER_WORKS)
			if(NOT APPLE)
				list(APPEND ANGELSCRIPT_SOURCES ${ANGELSCRIPT_DIR}/source/as_callfunc_arm64.cpp ${ANGELSCRIPT_DIR}/source/as_callfunc_arm64_gcc.S)
			else()
				list(APPEND ANGELSCRIPT_SOURCES ${ANGELSCRIPT_DIR}/source/as_callfunc_arm64.cpp ${ANGELSCRIPT_DIR}/source/as_callfunc_arm64_xcode.S)
			endif()
		else()
			message(FATAL_ERROR "ARM target requires a working assembler")
		endif()
	elseif(ARCHFLAGS MATCHES "^arm")
		enable_language(ASM)
		if(CMAKE_ASM_COMPILER_WORKS)
			list(APPEND ANGELSCRIPT_SOURCES ${ANGELSCRIPT_DIR}/source/as_callfunc_arm.cpp ${ANGELSCRIPT_DIR}/source/as_callfunc_arm_gcc.S)
			set_property(SOURCE ${ANGELSCRIPT_DIR}/source/as_callfunc_arm_gcc.S APPEND PROPERTY COMPILE_FLAGS " -Wa,-mimplicit-it=always")
		else()
			message(FATAL_ERROR "ARM target requires a working assembler")
		endif()
	elseif(ARCHFLAGS MATCHES "riscv64")
		enable_language(ASM)
		if(CMAKE_ASM_COMPILER_WORKS)
			list(APPEND ANGELSCRIPT_SOURCES ${ANGELSCRIPT_DIR}/source/as_callfunc_riscv64.cpp ${ANGELSCRIPT_DIR}/source/as_callfunc_riscv64_gcc.S)
		else()
			message(FATAL_ERROR "RISC-V 64 target requires a working assembler")
		endif()
	endif()

	ncine_assign_source_group(PATH_PREFIX "${ANGELSCRIPT_DIR}/source" FILES ${ANGELSCRIPT_SOURCES} ${ANGELSCRIPT_HEADERS} SKIP_EXTERNAL)
	target_sources(Angelscript PRIVATE ${ANGELSCRIPT_SOURCES} ${ANGELSCRIPT_HEADERS})
	target_include_directories(Angelscript PRIVATE "${ANGELSCRIPT_INCLUDE_DIR}" "${ANGELSCRIPT_DIR}/source")

	set(ANGELSCRIPT_FOUND TRUE)
	set(ANGELSCRIPT_STATIC TRUE)
	mark_as_advanced(ANGELSCRIPT_STATIC)
endif()
