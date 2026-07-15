#pragma once

/**
	@file ShaderParser.h

	Input-format parser and mini preprocessor for ShaderCompiler.

	A ".shader" file is a custom shader language: top-level directive keywords
	(program/batched/variant/render_mode/shader_type, all plain semicolon-
	terminated statements), "varying" and "attribute" declarations (an attribute is
	emitted as a vertex-stage-only "in" global, with a leading "layout(...)" qualifier
	kept in front of "in"), uniform hints ("texture_unit(N)" assigns the sampler's
	texture unit), shared globals and
	vertex()/fragment() entry points. ParseDocuments lowers the file into one or more
	ShaderDocuments (the internal representation with expanded per-stage line streams),
	preserving original line numbers for error reporting; everything downstream
	(variants, preprocessing, reflection, BuildStageSource, emission) runs per document.

	Two modes exist. The default ("shader_type custom;", implied when no shader_type
	statement is present) has no template: globals are shared verbatim by both stages,
	vertex() becomes the vertex main() verbatim and the fragment() body becomes the
	fragment main() verbatim — COLOR is the fragment output variable itself
	("out vec4 COLOR;"), undefined until written, there is no default and no epilogue,
	so an early "return" inside fragment() is safe (referencing fragColor is
	an error — that name does not exist). "shader_type canvas_item;" opts into the
	sprite template vertex stage, the UV/TEXTURE/COLOR/PALETTE_OFFSET built-ins and the
	batched twin declared by "batched <Name>;"; a canvas vertex() body is spliced
	verbatim before the real epilogue (gl_Position + varying stores), so "return"
	inside canvas vertex() is a parse error. Every program's variant list starts with
	the unnamed base variant (Name "", no defines, always Variants[0]); "variant
	<NAME>;" adds one named variant per declaration. A canvas document that references TEXTURE without
	declaring uTexture gets "uniform sampler2D uTexture;" auto-declared (with texture
	unit 0) — explicit declarations, with or without a "texture_unit(N)" hint,
	win. "#ifdef/#ifndef VERTEX_STAGE|FRAGMENT_STAGE"
	conditionals around shared globals are resolved at assembly time — the emitted
	sources contain no stage macros at all; stage-specific helper functions need no
	guards — unused functions are eliminated per stage after assembly (fixpoint; main
	is the root), and unused uniforms/blocks/defines/structs are eliminated per stage
	afterwards under a hard reflection-preservation rule (a uniform or block leaves a
	stage only when the same declaration survives in the other stage, so the merged
	reflection never changes). The optional "precision mediump|highp;" directive
	selects the GL ES float precision of the fragment prologue.

	Preprocessor implements the object-like subset of the C preprocessor
	(#define/#undef, #if/#ifdef/#ifndef/#elif/#else/#endif with integer constant
	expressions) that is required to produce a per-variant declaration stream for
	reflection. It is NOT used for the emitted shader sources, which keep the
	original text verbatim. Two special rules apply (see README.md): GL_ES is never
	predefined (reflection is taken from the desktop GL view) and BATCH_SIZE is
	treated as a symbolic constant (defined with value 1 inside #if expressions,
	kept symbolic when used as an array size).
*/

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include <Containers/String.h>
#include <Containers/StringView.h>

namespace ShaderCompiler
{
	using namespace Death::Containers;

	/** @brief One line of source text together with its 1-based line number in the original ".shader" file */
	struct SourceLine
	{
		String Text;
		std::int32_t Line = 0;
	};

	/** @brief Error description pointing into the input file */
	struct Diagnostic
	{
		String Message;
		std::int32_t Line = 0;
	};

	/** @brief Texture unit assignment for a sampler uniform — from a "texture_unit(N)" uniform hint or the implicit canvas TEXTURE registration */
	struct TextureDirective
	{
		String Name;
		std::int32_t Unit = -1;
		std::int32_t Line = 0;
	};

	/** @brief "render_mode" flags (bit values match ShaderCompiler::RenderMode in the generated ShaderCompilerTypes.h) */
	enum RenderModeMask : std::uint32_t
	{
		RenderModeBlendMix = 0x01,
		RenderModeBlendAdd = 0x02,
		RenderModeBlendSub = 0x04,
		RenderModeBlendMul = 0x08,
		RenderModeBlendPremulAlpha = 0x10,
		RenderModeUnshaded = 0x20
	};

	/** @brief Lowered ".shader" document — directives plus raw (unpreprocessed) per-stage GLSL line streams */
	struct ShaderDocument
	{
		String ProgramName;
		std::vector<String> Variants;
		std::vector<TextureDirective> Textures;
		std::vector<SourceLine> Prelude;
		std::vector<SourceLine> VertexLines;
		std::vector<SourceLine> FragmentLines;
		std::uint32_t RenderModes = 0;		// Bitmask of RenderModeMask flags (0 when no render_mode is declared)
		bool HasVertexStage = false;
		bool HasFragmentStage = false;
	};

	/** @brief Reads the content of the file at @p path into @p content, returns false on failure */
	using FileReader = std::function<bool(StringView path, String& content)>;

	/** @brief Parses and lowers the ".shader" input language */
	class ShaderParser
	{
	public:
		/**
			Parses the whole (include-expanded) file content and lowers it into one or more
			ShaderDocuments. Custom-mode files (the default, no "shader_type" statement) produce
			exactly one document; "shader_type canvas_item;" files produce the primary document
			plus, when "batched <Name>;" is present, its batched twin.
		*/
		static bool ParseDocuments(StringView content, std::vector<ShaderDocument>& documents, Diagnostic& diag);

		/** Splits raw file content into lines (handles CRLF/CR and backslash-newline continuations) */
		static void SplitLines(StringView content, std::vector<SourceLine>& lines);

		/** Removes line comments and block comments in place (newlines and line numbering are preserved) */
		static void StripComments(std::vector<SourceLine>& lines);

		/**
			Expands `#include "relative/path"` lines recursively (textually), relative to @p baseDir,
			reading files through @p reader (the offline tool passes a filesystem reader, the engine
			passes its own virtual filesystem). Runs on the raw text before parsing, so both reflection
			and the emitted sources see the included text inlined. As a consequence, line numbers in
			diagnostics refer to the include-expanded stream.
		*/
		static bool ExpandIncludes(String& content, StringView baseDir, const FileReader& reader, std::int32_t depth, String& error);

		/** Builds the compilable GLSL source of one stage (baked variant define + "#line 1" + shared prelude + stage body) */
		static String BuildStageSource(const ShaderDocument& document, bool vertexStage, StringView define);

		/** Returns the directory part of @p path, or "." if it has none */
		static String DirectoryOf(StringView path);
	};

	/** @brief Object-like macro preprocessor used to produce the per-variant declaration stream for reflection */
	class Preprocessor
	{
	public:
		/** Predefines an object-like macro (used to bake variant defines, e.g. DITHER=1) */
		void Define(StringView name, StringView body);

		/** Runs the preprocessor, appending active (macro-expanded) lines to @p output */
		bool Run(const std::vector<SourceLine>& input, std::vector<SourceLine>& output, Diagnostic& diag);

		/** Returns true if the macro is defined (BATCH_SIZE always reports as defined) */
		bool IsDefined(StringView name) const;

		/** Retrieves the body of a defined object-like macro, returns false if not defined or function-like */
		bool TryGetMacroBody(StringView name, String& body) const;

		/** Evaluates a #if/#elif integer constant expression */
		bool EvaluateExpression(StringView expression, std::int32_t line, std::int32_t depth, std::int64_t& value, Diagnostic& diag) const;

	private:
		struct Macro
		{
			String Body;
			bool FunctionLike = false;
		};

		std::map<String, Macro> _macros;

		String ExpandMacros(StringView text) const;
	};
}
