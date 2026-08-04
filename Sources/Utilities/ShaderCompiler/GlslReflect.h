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
		/** @brief Member name */
		String Name;
		/** @brief Data type of the member */
		GlslType Type = GlslType::Float;
		/** @brief Name of the user struct type (only when @ref Type is @ref GlslType::Struct) */
		String TypeName;
		/** @brief Array element count, or `0` when the member is not an array */
		std::uint32_t ArraySize = 0;
		/** @brief Whether the array is sized by the symbolic `BATCH_SIZE` constant */
		bool SymbolicArray = false;
		/** @brief std140 offset from the start of the aggregate */
		std::uint32_t Offset = 0;
		/** @brief std140 element stride (arrays only) */
		std::uint32_t ArrayStride = 0;
	};

	/** @brief User struct declaration with its computed std140 layout */
	struct StructInfo
	{
		/** @brief Struct type name */
		String Name;
		/** @brief Struct members in declaration order */
		std::vector<MemberInfo> Fields;
		/** @brief std140 size rounded up to the struct alignment */
		std::uint32_t Size = 0;
		/** @brief std140 base alignment (largest member alignment rounded up to 16) */
		std::uint32_t Align = 0;
	};

	/** @brief Loose (non-block, non-sampler) uniform */
	struct UniformInfo
	{
		/** @brief Uniform name */
		String Name;
		/** @brief Data type of the uniform */
		GlslType Type = GlslType::Float;
		/** @brief Name of the user struct type (only when @ref Type is @ref GlslType::Struct) */
		String TypeName;
		/** @brief Array element count, or `0` when the uniform is not an array */
		std::uint32_t ArraySize = 0;
	};

	/** @brief std140 uniform block */
	struct BlockInfo
	{
		/** @brief Block name */
		String Name;
		/** @brief std140 size covering everything except symbolic arrays */
		std::uint32_t BaseSize = 0;
		/** @brief Element stride of the symbolic `BATCH_SIZE` array, or `0` when the block has none */
		std::uint32_t InstanceStride = 0;
		/** @brief Block members in declaration order */
		std::vector<MemberInfo> Members;
	};

	/** @brief Sampler uniform with its optional "texture_unit(N)" hint unit assignment */
	struct TextureInfo
	{
		/** @brief Sampler uniform name */
		String Name;
		/** @brief Sampler type (@ref GlslType::Sampler2D, @ref GlslType::Sampler3D or @ref GlslType::SamplerCube) */
		GlslType Type = GlslType::Sampler2D;
		/** @brief Texture unit from a `texture_unit(N)` hint, or `-1` when unassigned */
		std::int32_t Unit = -1;
	};

	/** @brief Vertex attribute ("in" declaration in the vertex stage) */
	struct AttributeInfo
	{
		/** @brief Attribute name */
		String Name;
		/** @brief Data type of the attribute */
		GlslType Type = GlslType::Float;
		/** @brief Name of the user struct type (only when @ref Type is @ref GlslType::Struct) */
		String TypeName;
		/** @brief Explicit `layout(location = N)` index, or `-1` when unspecified */
		std::int32_t Location = -1;
	};

	/** @brief Reflection data collected from one stage (or merged for a whole program) */
	struct StageReflection
	{
		/** @brief User struct declarations with their computed std140 layouts */
		std::vector<StructInfo> Structs;
		/** @brief Loose (non-block, non-sampler) uniforms */
		std::vector<UniformInfo> Uniforms;
		/** @brief std140 uniform blocks */
		std::vector<BlockInfo> Blocks;
		/** @brief Sampler uniforms (texture bindings) */
		std::vector<TextureInfo> Textures;
		/** @brief Vertex attributes (empty for the fragment stage) */
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
