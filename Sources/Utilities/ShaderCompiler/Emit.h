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

#include <cstdint>
#include <functional>
#include <vector>

namespace ShaderCompiler
{
	/** @brief Reflection of one program variant (the unnamed base or a named variant), merged across stages */
	struct VariantReflection
	{
		/** @brief Variant name; empty for the unnamed base variant */
		String Name;
		/** @brief Baked variant define; empty for the unnamed base variant */
		String Define;
		/** @brief Reflection merged across the vertex and fragment stages */
		StageReflection Reflection;
	};

	/**
		@brief Offline SPIR-V compiler callback injected into EmitHeader

		Compiles the Vulkan-flavored GLSL @p vulkanGlsl of one stage (@p vertexStage selects vertex vs.
		fragment) to SPIR-V words in @p spirv, returning false (and optionally filling @p log) when glslang
		is unavailable or the compile fails. Passing an empty function omits SPIR-V (the VkVsSpirv/VkFsSpirv
		fields are emitted as nullptr/0). Injected by the caller so Emit stays free of the process-spawning
		and glslang-locating code, which lives in the offline Main.cpp.
	*/
	using SpirvCompileFn = std::function<bool(StringView vulkanGlsl, bool vertexStage, std::vector<std::uint32_t>& spirv, String& log)>;

	/**
		@brief Offline DXBC compiler callback injected into EmitHeader

		Compiles the HLSL @p hlsl of one stage (@p vertexStage selects vertex vs. fragment) to DXBC bytecode
		in @p dxbc, returning false (and optionally filling @p log) when the compile fails. Passing an empty
		function omits DXBC — the HLSL sources are embedded instead (the HlslVsDxbc/HlslFsDxbc fields are
		emitted as nullptr/0 and the D3D11 backend runtime-compiles the text). Injected by the caller so Emit
		stays free of the d3dcompiler_47-loading code, which lives in the offline Main.cpp.
	*/
	using DxbcCompileFn = std::function<bool(StringView hlsl, bool vertexStage, std::vector<std::uint8_t>& dxbc, String& log)>;

	/**
		@brief The stage artifacts only a platform-specific compiler can produce

		Direct3D 11 bytecode needs `D3DCompile`, which exists on Windows alone, and SPIR-V needs a glslang
		binary; neither is available on most machines that build this project. They are therefore kept OUT
		of the per-shader header and written to one aggregate per backend, exactly as the console targets
		already are - so a regeneration on a machine that cannot rebuild them leaves the committed ones
		alone instead of replacing them with nulls.

		That mattered more than it looks: an absent SPIR-V module does not degrade, it makes
		@relativeref{nCine::RHI::Vulkan,VulkanShaderProgram} skip every draw that uses the program.

		Each string is a complete namespace body defining, for every variant, the stage arrays and a size
		constant beside each one. The sizes travel WITH the arrays rather than being baked into the
		per-shader table, so an aggregate that is a generation behind stays self-consistent - it describes
		the blobs it actually holds.
	*/
	struct BackendArtifacts
	{
		/** @brief Direct3D 11 stage artifacts: the DXBC blobs, or the HLSL sources when no DXBC compiler ran */
		String D3d11;
		/** @brief Vulkan stage modules: the SPIR-V words */
		String Vulkan;
	};

	/** @brief One lowered program (document plus per-variant reflection) to be emitted into a generated header */
	struct ProgramReflection
	{
		/** @brief The lowered source document this reflection was computed from */
		const ShaderDocument* Document = nullptr;
		/** @brief Per-variant merged reflection (the unnamed base variant is `Variants[0]`) */
		std::vector<VariantReflection> Variants;
	};

	/** @brief Emits the generated C++ header and the "--check" reflection dump */
	class Emitter
	{
	public:
		/** Builds the standalone "ShaderCompilerTypes.h" header with the shared reflection types */
		static String BuildTypesHeader();

		/**
			Emits the complete generated header, returns false and fills @p diag on error.
			One input file yields one header, but it may carry multiple programs (a canvas_item
			document plus its "batched" twin) — all of them are emitted into the same namespace.

			The Direct3D 11 and Vulkan stage artifacts do not go into @p output — they are appended to
			@p artifacts instead, for the caller to write into the per-backend aggregates (see
			@ref BackendArtifacts). @p output references them by symbol, so it is complete and correct
			whatever compilers were available when it was produced.
		*/
		static bool EmitHeader(const std::vector<ProgramReflection>& programs, StringView ns, StringView inputFileName,
			const SpirvCompileFn& compileSpirv, const DxbcCompileFn& compileDxbc, String& output,
			BackendArtifacts& artifacts, Diagnostic& diag);

		/** Builds the human-readable reflection dump printed by "--check" (called once per program) */
		static String BuildCheckDump(const ShaderDocument& document, const std::vector<VariantReflection>& variants);
	};
}
