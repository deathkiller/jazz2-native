#pragma once

/**
	@file Emit.h

	Generated C++ header emitter for ShaderCompiler.

	Produces a generated header containing:
	- one raw-string GLSL source pair (vertex/fragment) per program variant, with the
	  variant's "#define NAME (1)" and "#line 1" baked at the top and the shared
	  prelude prepended — the GLSL body is the original text with only the stage
	  conditionals ("#ifdef/#ifndef VERTEX_STAGE|FRAGMENT_STAGE") resolved away;
	- constexpr reflection arrays (uniforms, std140 blocks and their members, texture
	  bindings, vertex attributes) per variant, tied together by ProgramVariant and
	  Program descriptors.

	The plain-struct reflection types live in the fixed "ShaderCompiler" namespace inside a
	shared standalone header, "ShaderCompilerTypes.h" (written via "--emit-types" and included
	by every generated header), so engine code can consume reflection without pulling in
	any program's data. Program data goes into the namespace given on the command line
	(default "ShaderArtifacts").

	Also provides the human-readable reflection dump used by "--check".
*/

#include "GlslReflect.h"

namespace ShaderCompiler
{
	/** @brief Reflection of one program variant (the unnamed base or a named variant), merged across stages */
	struct VariantReflection
	{
		std::string Name;			// empty for the base variant, otherwise the variant name
		std::string Define;			// empty for the base variant, otherwise the baked define
		StageReflection Reflection;
	};

	/** @brief One lowered program (document plus per-variant reflection) to be emitted into a generated header */
	struct ProgramReflection
	{
		const ShaderDocument* Document = nullptr;
		std::vector<VariantReflection> Variants;
	};

	/** @brief Emits the generated C++ header and the "--check" reflection dump */
	class Emitter
	{
	public:
		/** Builds the standalone "ShaderCompilerTypes.h" header with the shared reflection types */
		static std::string BuildTypesHeader();

		/**
			Emits the complete generated header, returns false and fills @p diag on error.
			One input file yields one header, but it may carry multiple programs (a canvas_item
			document plus its "batched" twin) — all of them are emitted into the same namespace.
		*/
		static bool EmitHeader(const std::vector<ProgramReflection>& programs,
			const std::string& ns, const std::string& inputFileName, std::string& output, Diagnostic& diag);

		/** Builds the human-readable reflection dump printed by "--check" (called once per program) */
		static std::string BuildCheckDump(const ShaderDocument& document, const std::vector<VariantReflection>& variants);
	};
}
