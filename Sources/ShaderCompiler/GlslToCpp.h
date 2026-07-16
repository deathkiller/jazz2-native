#pragma once

/**
	@file GlslToCpp.h

	GLSL-to-C++ fragment-shader transpiler for ShaderCompiler (Phase A vertical slice).

	Turns a shader's lowered fragment GLSL — the very text the tool bakes into a generated header's
	`<Program>_Fs` string — into a plain C++ function the CPU software renderer can call once per pixel,
	so an effect runs in software exactly as its GLSL specifies instead of being hand-ported. The
	emitted function is written against the runtime library in
	`nCine/Graphics/RHI/Software/SwShaderRuntime.h` (`vec2`/`vec3`/`vec4`, the GLSL built-ins, the
	`swTexture()` sampler shim and `packColor()`).

	The transpiler reuses the existing expression lexer (@ref GlslExprTokenizer) and the shader
	preprocessor (@ref Preprocessor, to resolve the baked variant define, drop the `GL_ES` block and
	strip `#line`) and grows a real per-statement/expression AST from the token stream. It supports a
	bounded subset: `float`/`int`/`bool` and `vec`/`ivec`/`bvec` types, `in`/`out`/`uniform` globals,
	local declarations, `=`/`+=`/`-=`/`*=`/`/=` assignments, `if`/`else`, C-style `for`, `return`, the
	ternary operator, arithmetic/comparison/logical operators, member and read-swizzle access,
	constructors, built-in and user-helper calls. Anything outside the subset (matrices, arrays,
	derivatives such as `dFdx`/`fwidth`, `texelFetch`/`textureSize`, or a read of any varying other than
	`vTexCoords`/`vColor`) makes @ref GlslToCppResult::Supported `false` with a reason and NO emitted
	code, so unsupported shaders are cleanly declined rather than mistranslated.
*/

#include <cstdint>
#include <vector>

#include <Containers/String.h>
#include <Containers/StringView.h>

namespace ShaderCompiler
{
	using namespace Death::Containers;

	/** @brief A sampler uniform's texture-unit assignment, taken from the program's reflection */
	struct SamplerBinding
	{
		String Name;
		std::int32_t Unit = -1;
	};

	/**
		@brief One scalar/vector member of the per-instance std140 block, with its baked byte offset

		The constant-varying analysis reads instance-block members (spriteSize, texRect, palOffset, ...) from
		the bound uniform block at these std140 offsets, so a per-instance-constant varying can be recomputed
		on the CPU. The offsets come from the program's block reflection (a batched shader contributes the
		fields of its instance struct, whose offsets are relative to the per-instance start). Matrix members
		are omitted (never referenced by a supported varying).
	*/
	struct GlslInstanceMember
	{
		String Name;
		std::uint32_t Offset = 0;			// std140 offset from the start of one instance
		std::uint32_t ComponentCount = 1;	// 1 for float/int/bool, N for vecN
	};

	/** @brief Outcome of transpiling one fragment shader to C++ */
	struct GlslToCppResult
	{
		/** @brief `true` when the shader fits the supported subset and @ref Code was emitted */
		bool Supported = false;
		/** @brief Why the shader was declined (only meaningful when @ref Supported is `false`) */
		String UnsupportedReason;
		/** @brief The emitted C++ (a `<Program>_Uniforms` struct plus the fragment function[s]) */
		String Code;
		/**
			@brief `true` when a `<Program>_ComputeVaryings(void*, const std::uint8_t*)` was emitted

			Set when the fragment reads at least one per-instance-constant varying. The device calls the
			function once per instance to fill those varyings (struct fields) from the bound instance block
			before running the fragment.
		*/
		bool HasComputeVaryings = false;
		/**
			@brief Names of the constant-varying fields added to `<Program>_Uniforms`

			They live in the same struct as the loose uniforms but are filled by `<Program>_ComputeVaryings`
			(not by `ResolveUniform`), so the caller excludes them from the loose-uniform field list.
		*/
		std::vector<String> ConstVaryingNames;
	};

	/** @brief Transpiles lowered fragment GLSL into a C++ software-renderer fragment function */
	class GlslToCpp
	{
	public:
		/**
			Transpiles @p fragmentGlsl (the lowered `<Program>_Fs` text of one variant) into a
			`void <programName>_Fragment(const nCine::RhiSoftware::FragmentShaderInput&)` function.
			@p samplers maps each sampler uniform to the texture unit it is bound to (from reflection),
			so `texture(sampler, uv)` lowers to `sw::swTexture(in, <unit>, uv)`. @p vertexGlsl is the lowered
			`<Program>_Vs` text of the same variant: it is analyzed for per-instance-constant varyings (a
			varying whose vertex assignment does not depend on the per-vertex input), which the fragment may
			then read. @p instanceMembers gives the std140 byte offsets of the instance-block members those
			varyings read. Returns a result whose @ref GlslToCppResult::Supported flag says whether emission
			succeeded.
		*/
		static GlslToCppResult TranspileFragment(StringView programName, StringView fragmentGlsl,
			StringView vertexGlsl, const std::vector<SamplerBinding>& samplers,
			const std::vector<GlslInstanceMember>& instanceMembers);
	};
}
