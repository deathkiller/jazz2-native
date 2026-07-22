cmake_minimum_required(VERSION 3.15)
project(ShaderCompiler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ShaderCompiler)

target_include_directories(ShaderCompiler PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared
)

set(SOURCES
	Main.cpp
	Trace.cpp
	ConstFold.cpp
	ConstFold.h
	Emit.cpp
	Emit.h
	Essl100.cpp
	Essl100.h
	GlslAst.h
	GlslReflect.cpp
	GlslReflect.h
	GlslToCpp.cpp
	GlslToCpp.h
	Hlsl.cpp
	Hlsl.h
	ShaderParser.cpp
	ShaderParser.h
	Vulkan.cpp
	Vulkan.h
	# Runtime facade, also compiled into the game - built here to catch drift early
	# (requires the committed Shaders/Generated/ShaderCompilerTypes.h)
	RuntimeShader.cpp
	RuntimeShader.h
	# Death::Containers dependency
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Containers/String.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Containers/String.h
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Containers/StringView.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Containers/StringView.h
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Base/Format.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Base/Format.h
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Asserts.h
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Common.h
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/CommonBase.h
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Cpu.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Cpu.h
)

ncine_assign_source_group(PATH_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/../ ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

target_sources(ShaderCompiler PRIVATE ${SOURCES})

include(ncine_helpers)
ncine_apply_compiler_options(ShaderCompiler ALLOW_EXCEPTIONS)
