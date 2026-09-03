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
	win. Conditionals naming VERTEX_STAGE|FRAGMENT_STAGE around shared globals —
	the "#ifdef"/"#ifndef" forms as well as "#if"/"#elif" expressions built from
	them — are resolved at assembly time; the emitted
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
#include <map>

#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

namespace ShaderCompiler
{
	using namespace Death::Containers;

	/** @brief One line of source text together with its 1-based line number in the original ".shader" file */
	struct SourceLine
	{
		/** @brief The line of source text */
		String Text;
		/** @brief 1-based line number in the original ".shader" file */
		std::int32_t Line = 0;
	};

	/** @brief Error description pointing into the input file */
	struct Diagnostic
	{
		/** @brief Human-readable error message */
		String Message;
		/** @brief 1-based line number the error points to in the input file */
		std::int32_t Line = 0;
	};

	/** @brief Texture unit assignment for a sampler uniform — from a "texture_unit(N)" uniform hint or the implicit canvas TEXTURE registration */
	struct TextureDirective
	{
		/** @brief Sampler uniform name */
		String Name;
		/** @brief Assigned texture unit, or `-1` when unassigned */
		std::int32_t Unit = -1;
		/** @brief 1-based line number the directive came from */
		std::int32_t Line = 0;
	};

	/** @brief One backend a "void fixed_function(...) { ... }" block can name in its parentheses */
	enum class FixedFunctionTarget : std::uint8_t
	{
		Pvr,		/**< Dreamcast-specific override (wins over the generic block for that backend) */
		Gx,			/**< Wii/GameCube-specific override (wins over the generic block for that backend) */
		Gu,			/**< PlayStation Portable-specific override (wins over the generic block for that backend) */
		Gs,			/**< PlayStation 2-specific override (wins over the generic block for that backend) */
		Rdp,		/**< Nintendo 64-specific override (wins over the generic block for that backend) */
		LegacyGl	/**< Legacy-OpenGL-specific override (MorphOS's TinyGL and any other fixed-function GL) */
	};

	/** @brief Spelling of @p target as it is written inside a block's parentheses */
	inline const char* FixedFunctionTargetName(FixedFunctionTarget target)
	{
		switch (target) {
			case FixedFunctionTarget::Gx: return "gx";
			case FixedFunctionTarget::Gu: return "gu";
			case FixedFunctionTarget::Gs: return "gs";
			case FixedFunctionTarget::Rdp: return "rdp";
			case FixedFunctionTarget::LegacyGl: return "legacygl";
			default: return "pvr";
		}
	}

	/**
		@brief The target list of a block, spelled the way the block declares it — e.g. `pvr, gu` (empty for the generic block)

		The canonical spelling (declaration order, one ", " between entries) rather than the raw
		source text, so a provenance comment in a generated header cannot change just because
		somebody reformatted the parentheses in the shader file.
	*/
	inline String FixedFunctionTargetList(const SmallVectorImpl<FixedFunctionTarget>& targets)
	{
		String result;
		for (std::size_t i = 0; i < targets.size(); i++) {
			if (i != 0) {
				result += ", ";
			}
			result += FixedFunctionTargetName(targets[i]);
		}
		return result;
	}

	/**
		@brief One captured "void fixed_function([<target>[, <target>...]]) { ... }" block

		The body is captured verbatim (like the vertex()/fragment() entry bodies) and transpiled to C++
		offline by the fixed-function emitter (@ref ConsoleFixedFunction) — it never becomes part of the
		lowered GLSL stages, so a shader that carries a block emits byte-identical per-shader headers.
	*/
	struct FixedFunctionBlock
	{
		/**
			@brief Which backends the block implements, in declaration order — EMPTY for the generic block

			A comma-separated target list lets one block serve several backends, which is what shaders
			whose implementation for two consoles is literally the same code use instead of keeping two
			byte-identical copies. Capabilities are then validated against the INTERSECTION of the
			listed targets, so a shared block can only use what every one of them can do.
		*/
		SmallVector<FixedFunctionTarget, 0> Targets;
		/** @brief Raw (unpreprocessed) statement lines of the block body */
		SmallVector<SourceLine, 0> Lines;
		/** @brief 1-based line number of the block header */
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
		/** @brief Program name from the "program" directive */
		String ProgramName;
		/** @brief Variant names; the unnamed base variant is always `Variants[0]` (empty name) */
		SmallVector<String, 0> Variants;
		/** @brief Sampler texture-unit assignments (explicit hints and the implicit canvas TEXTURE) */
		SmallVector<TextureDirective, 0> Textures;
		/** @brief Shared globals emitted before both stage bodies */
		SmallVector<SourceLine, 0> Prelude;
		/** @brief Raw (unpreprocessed) vertex-stage GLSL line stream */
		SmallVector<SourceLine, 0> VertexLines;
		/** @brief Raw (unpreprocessed) fragment-stage GLSL line stream */
		SmallVector<SourceLine, 0> FragmentLines;
		/** @brief Captured "fixed_function" blocks (empty for shaders with no console fixed-function implementation); a batched twin shares its primary's blocks */
		SmallVector<FixedFunctionBlock, 0> FixedFunctionBlocks;
		/** @brief Bitmask of @ref RenderModeMask flags (`0` when no "render_mode" is declared) */
		std::uint32_t RenderModes = 0;
		/** @brief Whether the document declares a vertex stage */
		bool HasVertexStage = false;
		/** @brief Whether the document declares a fragment stage */
		bool HasFragmentStage = false;
	};

	/** @brief Reads the content of the file at @p path into @p content, returns false on failure */
	using FileReader = Function<bool(StringView path, String& content)>;

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
		static bool ParseDocuments(StringView content, SmallVectorImpl<ShaderDocument>& documents, Diagnostic& diag);

		/** Splits raw file content into lines (handles CRLF/CR and backslash-newline continuations) */
		static void SplitLines(StringView content, SmallVectorImpl<SourceLine>& lines);

		/** Removes line comments and block comments in place (newlines and line numbering are preserved) */
		static void StripComments(SmallVectorImpl<SourceLine>& lines);

		/**
			Expands `#include "relative/path"` lines recursively (textually), relative to @p baseDir,
			reading files through @p reader (the offline tool passes a filesystem reader, the engine
			passes its own virtual filesystem). Runs on the raw text before parsing, so both reflection
			and the emitted sources see the included text inlined. As a consequence, line numbers in
			diagnostics refer to the include-expanded stream.
		*/
		static bool ExpandIncludes(String& content, StringView baseDir, FileReader& reader, std::int32_t depth, String& error);

		/**
			Builds the compilable GLSL source of one stage (baked variant define + "#line 1" + shared
			prelude + stage body). The conditionals naming SOFTWARE_RENDERER, NO_DYNAMIC_BRANCHING or
			LOW_POWER_GPU are resolved here according to @p softwareRenderer, @p noDynamicBranching
			and @p lowPowerGpu, like the stage macros are at assembly time - the built sources never
			contain any of the macros themselves. Both the
			"#ifdef"/"#ifndef" forms (with an optional "#else" and the matching "#endif") and "#if"/"#elif"
			expressions built from them are recognized, so one directive can replace a nest of them
			("#if !SOFTWARE_RENDERER && !NO_DYNAMIC_BRANCHING"). An expression that also names a macro this
			resolver does not own ("#if DITHER && !SOFTWARE_RENDERER") keeps its conditional for the GLSL
			compiler and loses only the backend macros.

			Whatever conditional survives is then lowered for the GLSL preprocessor that will read it: a
			purely boolean "#if" collapses to "#ifdef"/"#ifndef" or has its flags wrapped in "defined(...)",
			because the ES profiles reject an undefined macro inside an "#if" expression - see
			LowerEmittedCondition.

			All three default to false, so an emission that passes none comes out byte-for-byte unchanged.
			Only the offline GLSL-to-C++ software-fragment transpiler sets @p softwareRenderer, so a shader
			can carry a cheaper software-renderer variant of a too-expensive fragment path. Only the
			PlayStation 3 emission sets @p noDynamicBranching: a fragment stage that compiles to NV40
			IF/LOOP/BRK control flow does not survive that toolchain - the branch body overwrites registers
			the surrounding code is still holding, which silently corrupted the textured background's
			horizon tint - so a shader gates any dynamically branching block on it.

			Only the PS Vita (Cg / sceGxm) emission sets @p lowPowerGpu. Unlike the two above it says
			nothing about what the target CAN compile - the SGX543 runs every one of these shaders as
			written - only about how much per-pixel work it can sustain: it is a handheld part shading a
			full screen from a shared memory bus, and an operation that is invisible on a desktop GPU (a
			nine-tap voronoi of sin()-based hashes, say) is most of its frame. A shader gates a cheaper
			approximation of such a path on it, so a low-power part gets a substitute rather than the
			feature being dropped for everyone.
		*/
		static String BuildStageSource(const ShaderDocument& document, bool vertexStage, StringView define,
			bool softwareRenderer = false, bool noDynamicBranching = false, bool lowPowerGpu = false);

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
		bool Run(const SmallVectorImpl<SourceLine>& input, SmallVectorImpl<SourceLine>& output, Diagnostic& diag);

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
