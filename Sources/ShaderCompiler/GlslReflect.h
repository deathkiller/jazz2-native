#pragma once

/**
	@file GlslReflect.h

	GLSL-subset declaration parser and std140 layout calculator for ShaderCompiler.

	Operates on the preprocessed declaration stream produced by Preprocessor
	(comments stripped, inactive conditional branches removed, macros expanded).
	Only global-scope declarations are parsed — function bodies are skipped by
	brace counting:

	- struct <Name> { <members> };
	- layout (std140) uniform <BlockName> { <members> } [instanceName];
	- uniform <type> <name>[N];             (sampler types become texture bindings)
	- in <type> <name>;                     (vertex stage only — vertex attributes)

	Supported types: float, int, uint, bool, vec2/3/4, ivec2/3/4, uvec2/3/4,
	bvec2/3/4, mat2, mat3, mat4, sampler2D, sampler3D, samplerCube and previously
	declared user structs. Arrays may be sized by a decimal integer literal or by
	the symbolic BATCH_SIZE constant (uniform block members only) — symbolic
	arrays record their std140 element stride so the runtime can compute the batch
	size as maxUniformBlockSize / stride.

	std140 rules used (per OpenGL spec):
	- scalars 4/4 (bool = 4), vec2 8/8, vec3 12 with align 16, vec4 16/16;
	- matN = N columns with vec4 stride: mat2 32, mat3 48, mat4 64, align 16;
	- array base alignment = element alignment rounded up to 16, element stride =
	  element size rounded up to that alignment;
	- struct base alignment = max member alignment rounded up to 16, struct size =
	  end offset rounded up to the struct alignment.
*/

#include "ShaderParser.h"

namespace ShaderCompiler
{
	/** @brief Data type of a uniform, block member or vertex attribute */
	enum class GlslType : std::uint8_t
	{
		Float,
		Int,
		UInt,
		Bool,
		Vec2,
		Vec3,
		Vec4,
		IVec2,
		IVec3,
		IVec4,
		UVec2,
		UVec3,
		UVec4,
		BVec2,
		BVec3,
		BVec4,
		Mat2,
		Mat3,
		Mat4,
		Sampler2D,
		Sampler3D,
		SamplerCube,
		Struct
	};

	/** @brief Member of a struct or std140 uniform block */
	struct MemberInfo
	{
		std::string Name;
		GlslType Type = GlslType::Float;
		std::string TypeName;
		std::uint32_t ArraySize = 0;	// 0 = not an array
		bool SymbolicArray = false;		// sized by the symbolic BATCH_SIZE constant
		std::uint32_t Offset = 0;		// std140 offset from the start of the aggregate
		std::uint32_t ArrayStride = 0;	// std140 element stride (arrays only)
	};

	/** @brief User struct declaration with its computed std140 layout */
	struct StructInfo
	{
		std::string Name;
		std::vector<MemberInfo> Fields;
		std::uint32_t Size = 0;
		std::uint32_t Align = 0;
	};

	/** @brief Loose (non-block, non-sampler) uniform */
	struct UniformInfo
	{
		std::string Name;
		GlslType Type = GlslType::Float;
		std::string TypeName;
		std::uint32_t ArraySize = 0;	// 0 = not an array
	};

	/** @brief std140 uniform block */
	struct BlockInfo
	{
		std::string Name;
		std::uint32_t BaseSize = 0;			// std140 size covering everything except symbolic arrays
		std::uint32_t InstanceStride = 0;	// element stride of the symbolic BATCH_SIZE array (0 if none)
		std::vector<MemberInfo> Members;
	};

	/** @brief Sampler uniform with its optional "texture_unit(N)" hint unit assignment */
	struct TextureInfo
	{
		std::string Name;
		GlslType Type = GlslType::Sampler2D;
		std::int32_t Unit = -1;		// -1 = not assigned
	};

	/** @brief Vertex attribute ("in" declaration in the vertex stage) */
	struct AttributeInfo
	{
		std::string Name;
		GlslType Type = GlslType::Float;
		std::string TypeName;
		std::int32_t Location = -1;	// -1 = unspecified
	};

	/** @brief Reflection data collected from one stage (or merged for a whole program) */
	struct StageReflection
	{
		std::vector<StructInfo> Structs;
		std::vector<UniformInfo> Uniforms;
		std::vector<BlockInfo> Blocks;
		std::vector<TextureInfo> Textures;
		std::vector<AttributeInfo> Attributes;
	};

	/** @brief Parses global-scope GLSL declarations and computes std140 layouts */
	class GlslReflector
	{
	public:
		/** Reflects one preprocessed stage, returns false and fills @p diag on error */
		static bool ReflectStage(const std::vector<SourceLine>& lines, bool vertexStage, StageReflection& result, Diagnostic& diag);

		/** Merges vertex and fragment reflection into a program-level view (GL-style, deduplicated by name) */
		static bool MergeStages(const StageReflection& vertex, const StageReflection& fragment, StageReflection& merged, Diagnostic& diag);
	};
}
