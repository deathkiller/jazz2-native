cmake_minimum_required(VERSION 3.15)
project(ShaderCompiler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ShaderCompiler
	ShaderCompilerMain.cpp
	ShaderParser.cpp
	ConstFold.cpp
	GlslReflect.cpp
	Emit.cpp
	# Runtime facade, also compiled into the game — built here to catch drift early
	# (requires the committed Shaders/Generated/ShaderCompilerTypes.h)
	RuntimeShader.cpp
)
