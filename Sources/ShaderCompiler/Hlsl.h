#pragma once

/**
	@file Hlsl.h

	HLSL (Direct3D 11, Shader Model 4/5) source-to-source emitter for ShaderCompiler.

	The tool lowers each ".shader" into a MODERN-GLSL stage source (in/out, texture(), "out vec4 COLOR;",
	std140 UBO blocks, gl_VertexID). This emitter grows a real per-statement/expression AST from that
	source (on the SAME reusable lexer + preprocessor the GLSL-to-C++ transpiler uses — @ref GlslExprTokenizer
	and @ref Preprocessor) and emits an HLSL translation of one stage, then the offline `--hlsl-check` mode
	compiles each stage via d3dcompiler_47's `D3DCompile` to validate it. This is TOOL-ONLY: it adds no RHI
	backend and touches no engine code.

	The key GLSL -> HLSL rewrites:
	- Types: vec2/3/4 -> float2/3/4, mat3/4 -> float3x3/float4x4, ivecN -> intN, bvecN -> boolN; precision
	  and layout qualifiers are dropped.
	- Matrix algebra: a `*` whose operands are matrix/vector (never scalar) becomes `mul(a, b)` — GLSL
	  `M * v` -> HLSL `mul(M, v)` (column-vector convention). A lightweight type inference over the AST
	  decides matrix-vs-componentwise per multiply.
	- Uniforms: `layout(std140) uniform Block { ... }` -> `cbuffer Block : register(b#)`; loose uniforms
	  gather into one `cbuffer`. Block members are global names in HLSL, so bare-member and instance-name
	  qualified (`block.member`) accesses both resolve to the member name.
	- Textures: `uniform sampler2D uTex` -> `Texture2D uTex : register(t#); SamplerState uTex_sampler : register(s#);`
	  and `texture(uTex, uv)` -> `uTex.Sample(uTex_sampler, uv)` (`textureLod` -> `.SampleLevel`).
	- I/O: vertex attributes + `gl_VertexID` (SV_VertexID) form the VS input struct; VS `out` varyings +
	  `gl_Position` (SV_Position) form the VS output struct; FS `in` varyings + `gl_FragCoord` (SV_Position)
	  form the PS input struct; the FS `out vec4 COLOR` is returned as SV_Target. Attributes/varyings/gl_*
	  are copied to/from file-scope `static` globals so helper functions read them exactly as in GLSL.
	- Built-ins: mix->lerp, fract->frac, dFdx/dFdy->ddx/ddy, inversesqrt->rsqrt, atan(y,x)->atan2(y,x),
	  mod(a,b)->(a - b*floor(a/b)); dot/length/normalize/clamp/... keep their names.

	Constructs outside the handled subset (e.g. multiple fragment outputs, geometry/compute features) make
	Transform() return false with a diagnostic rather than emit invalid HLSL.
*/

#include "GlslReflect.h"		// StageReflection (texture units, block strides), Diagnostic, StringView

namespace ShaderCompiler
{
	/** @brief Transforms an already-lowered modern-GLSL stage source into HLSL (Shader Model 4/5) */
	class HlslEmitter
	{
	public:
		/**
			Transforms @p modernSource (as produced by ShaderParser::BuildStageSource) into HLSL, writing the
			result to @p out. @p vertexStage selects the vertex-vs-fragment lowering; @p reflection supplies
			texture-unit register assignments and the batched instance-block stride (for the emitted array
			size). Returns false and fills @p diag when the source uses a construct the emitter does not handle.
		*/
		static bool Transform(StringView modernSource, bool vertexStage, const StageReflection& reflection,
			String& out, Diagnostic& diag);
	};
}
