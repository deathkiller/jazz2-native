#pragma once

/**
	@file Vulkan.h

	Vulkan-flavored GLSL ("#version 450") source-to-source emitter for ShaderCompiler.

	The tool lowers each ".shader" into a MODERN-GLSL stage source (in/out, texture(), "out vec4 COLOR;",
	std140 UBO blocks, gl_VertexID) that serves the desktop GL 3.3 / GLES3 path. Vulkan consumes SPIR-V,
	which is produced offline by compiling a Vulkan-flavored GLSL variant through glslang (see the
	"--spirv-check" mode and the SPIR-V embedding in Emit.cpp). This emitter transforms an already-lowered
	modern-GLSL stage source into that Vulkan GLSL — TOOL-ONLY: it adds no RHI backend and touches no engine
	code. Vulkan GLSL is close to desktop GLSL 330, so the function
	bodies pass through verbatim (unlike the HLSL emitter, which re-emits an AST); only declarations and the
	vertex-id built-ins change:

	- "#version 450" is baked at the very top (SPIR-V is compiled offline, so there is no runtime #version
	  injection as on the GL path).
	- Loose (default-block) uniforms are ILLEGAL in Vulkan GLSL — every "uniform mat4 uProjectionMatrix;"
	  is gathered into ONE instance-name-less block "layout(set,binding,std140) uniform _Globals { ... };",
	  so the body's bare "uProjectionMatrix" still resolves (mirrors the HLSL "_Globals" cbuffer). Its members
	  and their std140 layout come from the merged reflection, so both stages agree.
	- Each "layout(std140) uniform Block { ... }" gains an explicit "layout(set = S, binding = B, std140)".
	- Each "uniform sampler2D uTex;" (combined image sampler) gains "layout(set = S, binding = B)".
	- SPIR-V has no automatic interface locations, so every vertex attribute ("in" in the vertex stage), every
	  varying ("out" in the vertex stage / "in" in the fragment stage) and every fragment output ("out vec4
	  COLOR;" and any additional "out" declarations) gains an explicit "layout(location = N)".
	- "gl_VertexID" -> "gl_VertexIndex" and "gl_InstanceID" -> "gl_InstanceIndex" (Vulkan keeps the vertex-id
	  and integer math, unlike the ES2 profile that has to synthesize a corner attribute).

	DESCRIPTOR SET / BINDING SCHEME (the Vulkan backend builds the VkDescriptorSetLayout from the SAME
	reflection, so the numbers here must be reconstructible from it):
	  - Everything lives in descriptor SET 0.
	  - UBOs come first, then combined-image-samplers, both in reflection order:
	      binding 0                              : the "_Globals" UBO (gathered loose uniforms) — present only
	                                               when the merged reflection has any loose uniform.
	      binding (uboBase + i)                  : the i-th std140 block (reflection.Blocks[i]),
	                                               with uboBase = (reflection.Uniforms.empty() ? 0 : 1).
	      binding (uboBase + BlockCount + j)     : the j-th sampler (reflection.Textures[j]).
	  - Vertex attribute locations use reflection.Attributes[k].Location when >= 0, else the attribute's
	    declaration index. Varying locations are assigned in declaration order within the stage (matching
	    across stages because both stages declare the varyings in the same order).

	FRAGMENT OUTPUT LOCATIONS (the render-target contract): fragment "out" declarations receive sequential
	locations 0..N-1 in DECLARATION ORDER of the emitted stage source. A single output (the usual case) is
	therefore location 0, exactly what the GL path yields for one unassigned output. With MULTIPLE outputs,
	note that user-declared extras (shared globals) precede the tool-appended "out vec4 COLOR;" in the
	emitted source, so COLOR takes the LAST location. The GL runtime uses no glBindFragDataLocation and the
	emitted GL source carries no explicit output locations (driver assignment is only pinned for the
	single-output case), so declaration order is the tool-defined contract every offline target follows:
	the HLSL emitter maps the same order to SV_Target0..N. Explicit "layout(location)" qualifiers on
	fragment outputs are NOT honored here (the emitter re-assigns by order); the HLSL emitter declines
	them, keeping any divergence from the GL source impossible to express.
*/

#include "GlslReflect.h"		// StageReflection (uniforms, blocks, textures, attributes), Diagnostic, StringView

namespace ShaderCompiler
{
	/** @brief Transforms an already-lowered modern-GLSL stage source into Vulkan-flavored GLSL ("#version 450") */
	class VulkanGlslEmitter
	{
	public:
		/**
			Transforms @p modernSource (as produced by ShaderParser::BuildStageSource) into Vulkan GLSL,
			writing the result to @p out. @p vertexStage selects the vertex-vs-fragment lowering; @p reflection
			(the MERGED per-variant reflection) supplies the descriptor set/binding assignments and the loose-
			uniform "_Globals" block layout. Returns false and fills @p diag when the source uses a construct
			the emitter does not handle.
		*/
		static bool Transform(StringView modernSource, bool vertexStage, const StageReflection& reflection,
			String& out, Diagnostic& diag);
	};
}
