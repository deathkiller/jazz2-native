#pragma once

/**
	@file RuntimeShader.h

	Runtime entry point of the ShaderCompiler pipeline, compiled into the game (unlike
	ShaderCompilerMain/Emit, which are offline-only). Compiles a ".shader" source — the
	same format the offline tool consumes, including "#include" directives resolved
	through a caller-provided FileReader — into lowered per-variant GLSL sources plus
	the same reflection data the offline tool computes.

	RuntimeProgram owns all storage and exposes the result through the same
	ShaderCompiler::Program/ProgramVariant view structs that the committed generated
	artifacts use, so consumers (batch sizing, the reflection-fed uniform caches)
	handle precompiled and externally-supplied shaders identically.
*/

#include "GlslReflect.h"
#include "../Shaders/Generated/ShaderCompilerTypes.h"

namespace ShaderCompiler
{
	/** @brief One variant of a runtime-compiled program: lowered stage sources plus merged reflection */
	struct RuntimeVariant
	{
		/** @brief Variant name; empty for the unnamed base variant */
		String Name;
		/** @brief Baked variant define; empty for the unnamed base variant */
		String Define;
		/** @brief Lowered vertex-stage GLSL source */
		String VsSource;
		/** @brief Lowered fragment-stage GLSL source */
		String FsSource;
		/** @brief Reflection merged across both stages */
		StageReflection Reflection;
	};

	/**
		@brief A ".shader" program compiled at runtime, owning its sources and reflection

		Not copyable or movable — the Program view returned by GetView() points into the owned storage.
	*/
	class RuntimeProgram
	{
		friend bool CompileRuntimeProgram(StringView content, StringView baseDir, const FileReader& reader, RuntimeProgram& out, Diagnostic& diag);

	public:
		RuntimeProgram() = default;

		RuntimeProgram(const RuntimeProgram&) = delete;
		RuntimeProgram& operator=(const RuntimeProgram&) = delete;

		/** @brief Program name */
		String Name;
		/** @brief "render_mode" flags (bitmask of ShaderCompiler::RenderMode; 0 when no render_mode is declared) */
		std::uint32_t RenderModes = 0;
		/** @brief Compiled variants (the unnamed base variant is `Variants[0]`) */
		std::vector<RuntimeVariant> Variants;

		/** @brief Returns the variant with the given name, or `nullptr` if not found; an empty name returns the unnamed base variant (`Variants[0]`) */
		const RuntimeVariant* FindVariant(StringView name) const;

		/** @brief Returns the artifact-struct view of the program (valid as long as this object lives) */
		const Program& GetView() const {
			return view_;
		}

	private:
		// Backing storage of the view — built once after Variants is final
		std::vector<std::vector<Uniform>> viewUniforms_;
		std::vector<std::vector<std::vector<BlockMember>>> viewBlockMembers_;
		std::vector<std::vector<UniformBlock>> viewBlocks_;
		std::vector<std::vector<TextureBinding>> viewTextures_;
		std::vector<std::vector<Attribute>> viewAttributes_;
		std::vector<ProgramVariant> viewVariants_;
		Program view_ = {};

		void BuildView();
	};

	/**
		@brief Compiles a ".shader" source into a RuntimeProgram

		@param content	The raw ".shader" file content (before include expansion)
		@param baseDir	Directory that "#include" paths are resolved against
		@param reader	Reads referenced include files (the engine passes its virtual filesystem)
		@param out		Receives the compiled program
		@param diag		Receives the error location and message on failure
	*/
	bool CompileRuntimeProgram(StringView content, StringView baseDir, const FileReader& reader, RuntimeProgram& out, Diagnostic& diag);
}
