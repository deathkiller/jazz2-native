cmake_minimum_required(VERSION 3.15)
project(ShaderCompiler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ncine_helpers.cmake is pure function definitions, so it can also be included by a standalone
# configure of this directory - the module path is only inherited when the game project added us
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake")
include(ncine_helpers)

add_executable(ShaderCompiler)

target_include_directories(ShaderCompiler PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../..
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared
)

set(SOURCES
	Main.cpp
	Trace.cpp
	ConsoleFixedFunction.cpp
	ConsoleFixedFunction.h
	ConstFold.cpp
	ConstFold.h
	Emit.cpp
	Emit.h
	Essl100.cpp
	Essl100.h
	GlslAst.h
	GlslTypedAst.h
	VertexIdRewrite.h
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
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Containers/String.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Containers/String.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Containers/StringView.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Containers/StringView.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Containers/StringUtils.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Containers/StringUtils.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Containers/SmallVector.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Containers/SmallVector.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Containers/GrowableArray.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Containers/DateTime.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Containers/DateTime.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Base/Format.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Base/Format.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Asserts.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Common.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/CommonBase.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Cpu.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Cpu.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Environment.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Environment.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Utf8.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Utf8.h
	# Death::IO dependency - the tool does all of its file and directory access through it
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/IO/FileSystem.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/IO/FileSystem.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/IO/FileStream.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/IO/FileStream.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/IO/MemoryStream.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/IO/MemoryStream.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/IO/Stream.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/IO/Stream.h
	${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/IO/FileAccess.h
)

ncine_assign_source_group(PATH_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/../../ ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

target_sources(ShaderCompiler PRIVATE ${SOURCES})

ncine_apply_compiler_options(ShaderCompiler ALLOW_EXCEPTIONS)
