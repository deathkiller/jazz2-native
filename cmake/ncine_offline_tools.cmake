# The offline tools (see `Sources/Utilities`), added after the game's own target, or in its place when the game is
# turned off - see `NCINE_BUILD_GAME`, `NCINE_BUILD_ASSET_PACKER` and `NCINE_BUILD_SHADER_COMPILER` in
# "ncine_options.cmake" for when each is built. The binary directories are given explicitly: without them CMake mirrors
# the source layout and a tool would be built into "<build>/Sources/Utilities/...", one level deeper than it needs to
# be and under a directory that otherwise holds no build output at all.

# ShaderCompiler: preprocesses the ".shader" files into the generated headers that are committed to the
# repository (see Sources/Utilities/ShaderCompiler/Main.cpp), so the game's own build never runs it
if(NCINE_BUILD_SHADER_COMPILER)
	add_subdirectory("${NCINE_SOURCE_DIR}/Utilities/ShaderCompiler" "Utilities/ShaderCompiler")
	set_target_properties(ShaderCompiler PROPERTIES FOLDER "Utilities")
endif()

# AssetPacker: converts original game data ahead of time (see Sources/Utilities/AssetPacker/Main.cpp). It links only
# the base layer and the converters rather than the engine, from the source lists of "ncine_sources.cmake".
if(NCINE_BUILD_ASSET_PACKER)
	add_subdirectory("${NCINE_SOURCE_DIR}/Utilities/AssetPacker" "Utilities/AssetPacker")
	set_target_properties(AssetPacker PROPERTIES FOLDER "Utilities")
endif()
