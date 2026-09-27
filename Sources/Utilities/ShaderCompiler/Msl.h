#pragma once

/**
	@file Msl.h

	Metal Shading Language (MSL, macOS) source-to-source emitter for ShaderCompiler.

	The tool lowers each ".shader" into a MODERN-GLSL stage source (in/out, texture(), "out vec4 COLOR;",
	std140 UBO blocks, gl_VertexID). This emitter re-emits that stage from the shared typed AST
	(@ref GlslTypedAst.h, the same front-end the HLSL and Vulkan emitters use) as one MSL translation unit
	per stage, which the Metal backend hands to `MTLDevice::newLibrary()` at load time - there is no offline
	MSL compiler outside Xcode, so unlike DXBC and SPIR-V the artifact is SOURCE (like the PS Vita's Cg).
	This is TOOL-ONLY: it adds no RHI backend and touches no engine code.

	The key GLSL -> MSL rewrites:
	- Types: vec2/3/4 -> float2/3/4, mat3/4 -> float3x3/float4x4, ivecN -> intN, bvecN -> boolN; precision
	  and layout qualifiers are dropped. Matrix algebra keeps the GLSL `*` operator (MSL's `*` on matrices
	  and vectors has the same column-vector linear-algebra meaning, so no mul() rewrite is needed).
	- Uniforms: MSL has no globals other than `constant` data, so every uniform reaches the shader as an
	  entry-point argument. The loose (default-block) uniforms gather into ONE `_Globals` struct bound at
	  `[[buffer(0)]]` (present only when the merged reflection has any loose uniform), and every
	  `layout(std140) uniform Block { ... }` becomes a `constant Block&` argument at `[[buffer(uboBase + i)]]`
	  (uboBase = 1 when `_Globals` exists, else 0; i = the block's index in the merged reflection). The vertex
	  stream is bound at `[[buffer(30)]]` (@ref MslVertexBufferIndex), well away from the uniform indices.
	  Block members are accessed as `_b<Block>.member`, so both the bare-member and the `instance.member`
	  spellings of the GLSL resolve to the same thing.
	- std140 layout: MSL lays a `constant` struct out with C rules, which agree with std140 for float/int,
	  vec2, vec4, mat3 and mat4 and for structs made of those - the whole shipped set - but not for vec3
	  followed by a scalar, mat2, or arrays of scalars/vec2. The emitter therefore checks every member against
	  its reflected std140 offset, inserts explicit padding where MSL would place a member EARLIER, spells a
	  vec3 `packed_float3` where a later member must land inside its tail padding, and declines a block it
	  cannot make agree (an array whose natural stride differs from the std140 one, a mat2, a bool). The
	  backend copies the engine's std140 bytes verbatim into the bound buffers, so this agreement is what
	  makes every uniform read the right value.
	- Textures: `uniform sampler2D uTex` -> `texture2d<float> uTex [[texture(j)]], sampler uTex_smplr
	  [[sampler(j)]]` (j = the sampler's index in the merged reflection), and `texture(uTex, uv)` ->
	  `uTex.sample(uTex_smplr, uv)` (`textureLod` -> `.sample(..., level(l))`, `texelFetch` -> `.read()`).
	- I/O: vertex attributes form a `[[stage_in]]` struct with `[[attribute(location)]]` members;
	  `gl_VertexID` / `gl_InstanceID` are `[[vertex_id]]` / `[[instance_id]]` arguments; VS `out` varyings +
	  `gl_Position` (`[[position]]`) form the VS return struct; FS `in` varyings + `gl_FragCoord`
	  (`[[position]]`) form the FS `[[stage_in]]` struct. Varyings carry `[[user(locnN)]]` in DECLARATION
	  ORDER so the two stages link by index rather than by name, and integer / `flat` varyings get `[[flat]]`
	  (Metal requires it). A single FS `out` is returned as `float4 [[color(0)]]`; MULTIPLE outputs render
	  to `[[color(0..N)]]` in DECLARATION ORDER through an emitted FsOut struct - the same order the HLSL and
	  SPIR-V emissions assign (see Hlsl.h / Vulkan.h).
	- Helper functions: MSL has no mutable program-scope variables and no implicit access to entry-point
	  arguments, so every helper that (transitively) reads a uniform, block, sampler, attribute, varying or
	  fragment output receives it as an extra trailing parameter (`constant _Globals&`, `constant Block&`,
	  `texture2d<float>` + `sampler`, `thread VsOut&` / `thread FsIn&`, `thread float4& COLOR`, ...), and
	  every call site forwards them - the arrangement SPIRV-Cross produces for the same problem. A GLSL global
	  variable that is not `const` (the trimming pass demotes dead varyings into one) becomes a local of the
	  entry point, passed the same way.
	- Coordinate system: the engine renders top-down on every backend (see RhiFwd.h) - clip-space y = +1 is
	  the top row of the target - which is Metal's own convention, so `gl_Position` is returned as it is.
	  `gl_FragCoord` becomes the fragment `[[position]]`, counted from the top-left corner like the RHI's
	  viewport and scissor rectangles.
	- Built-ins: mix/fract/step/smoothstep/clamp/... keep their names; inversesqrt->rsqrt, dFdx/dFdy->
	  dfdx/dfdy, atan(y,x)->atan2(y,x), mod(a,b)->(a - b*floor(a/b)), radians/degrees are expanded, the
	  relational lessThan()/equal()/... become operators (MSL vector comparisons yield boolN), a vector
	  `==` / `!=` becomes `all(a == b)` / `any(a != b)` (GLSL's is a scalar), and mix() with a boolean
	  selector becomes select().

	Constructs outside the handled subset (control flow the shared parser declines, a std140 layout MSL
	cannot reproduce, a non-`const` global the emitter cannot hoist) make Transform() return false with a
	diagnostic rather than emit invalid MSL.
*/

#include "GlslReflect.h"		// StageReflection (texture units, block strides), Diagnostic, StringView

namespace ShaderCompiler
{
	/** @brief Transforms an already-lowered modern-GLSL stage source into Metal Shading Language */
	class MslEmitter
	{
	public:
		/** @brief Entry point name of the emitted vertex function */
		static constexpr const char* VertexEntryPoint = "VSMain";
		/** @brief Entry point name of the emitted fragment function */
		static constexpr const char* FragmentEntryPoint = "FSMain";
		/**
			@brief `[[buffer(N)]]` index the vertex stream is bound at

			Chosen at the top of Metal's 31-slot buffer table so it never collides with the `_Globals` block
			(buffer 0) and the std140 blocks (buffers 1..N) that follow the reflection order. The Metal backend's
			vertex descriptor uses the same index.
		*/
		static constexpr std::int32_t VertexBufferIndex = 30;

		/**
			Transforms @p modernSource (as produced by ShaderParser::BuildStageSource) into MSL, writing the
			result to @p out. @p vertexStage selects the vertex-vs-fragment lowering; @p reflection (the MERGED
			per-variant reflection) supplies the buffer / texture index assignments, the loose-uniform `_Globals`
			layout and the std140 offsets the emitted structs are checked against. Returns false and fills @p diag
			when the source uses a construct the emitter does not handle.
		*/
		static bool Transform(StringView modernSource, bool vertexStage, const StageReflection& reflection,
			String& out, Diagnostic& diag);
	};
}
