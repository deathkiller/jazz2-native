cmake_minimum_required(VERSION 3.15)
project(ShaderCompiler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ShaderCompiler
	Main.cpp
	ShaderParser.cpp
	ConstFold.cpp
	GlslReflect.cpp
	Emit.cpp
	Essl100.cpp
	Hlsl.cpp
	Vulkan.cpp
	GlslToCpp.cpp
	# Runtime facade, also compiled into the game — built here to catch drift early
	# (requires the committed Shaders/Generated/ShaderCompilerTypes.h)
	RuntimeShader.cpp
	# Death::Containers dependency — the tool now uses String/StringView/format
	# (mirrors the Shared/Containers/*.cpp the engine compiles in cmake/ncine_sources.cmake)
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Containers/String.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Containers/StringView.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Base/Format.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Cpu.cpp
)

target_include_directories(ShaderCompiler PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
	${CMAKE_CURRENT_SOURCE_DIR}/../Shared
)
