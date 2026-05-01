#pragma once

#if defined(WITH_RHI_SW)

#include "Buffers.h"
#include "Texture.h"

#include <cstdint>

namespace nCine::RHI
{
	// =========================================================================
	// Fixed-function shader state
	// Without programmable shaders we describe per-draw parameters as a plain
	// struct that the software rasterizer reads directly.
	// =========================================================================
	struct FFState
	{
		// Current transform (model-view-projection as 4×4, column-major)
		float mvpMatrix[16] = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};

		float color[4] = { 1, 1, 1, 1 };		// Tint / solid colour
		float texRect[4] = { 0, 0, 1, 1 };		// UV sub-rect (x, y, w, h)
		float spriteSize[2] = { 1, 1 };			// Sprite pixel size
		float depth = 0.0f;
		bool  hasTexture = false;
		std::int32_t textureUnit = 0;
	};

	// =========================================================================
	// UniformCache / UniformBlockCache stubs
	// =========================================================================
	class UniformCache
	{
	public:
		// Float setters
		inline bool SetFloatVector(const float* /*values*/) { return true; }
		inline bool SetFloatValue(float /*v0*/) { return true; }
		inline bool SetFloatValue(float /*v0*/, float /*v1*/) { return true; }
		inline bool SetFloatValue(float /*v0*/, float /*v1*/, float /*v2*/) { return true; }
		inline bool SetFloatValue(float /*v0*/, float /*v1*/, float /*v2*/, float /*v3*/) { return true; }
		// Integer setters
		inline bool SetIntVector(const std::int32_t* /*values*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/, std::int32_t /*v1*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/, std::int32_t /*v1*/, std::int32_t /*v2*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/, std::int32_t /*v1*/, std::int32_t /*v2*/, std::int32_t /*v3*/) { return true; }
		// Getter
		inline std::int32_t GetIntValue(std::int32_t /*componentIdx*/) const { return 0; }
		// Dirty flag — no-op for the SW backend
		inline void SetDirty(bool /*isDirty*/) {}
	};

	class UniformBlockCache
	{
	public:
		UniformCache* GetUniform(const char* /*name*/) { return nullptr; }

		// Size / alignment — always 0 for the SW backend (no UBO)
		inline std::uint32_t GetSize()        const { return 0; }
		inline std::uint32_t GetAlignAmount() const { return 0; }

		// Data pointer — not backed by real memory in SW
		inline const std::uint8_t* GetDataPointer() const { return nullptr; }

		// Copy operations — no-ops
		inline bool CopyData(const std::uint8_t* /*src*/)                                           { return false; }
		inline bool CopyData(std::uint32_t /*destIndex*/, const std::uint8_t* /*src*/, std::uint32_t /*bytes*/) { return false; }
		inline void SetUsedSize(std::uint32_t /*size*/) {}
	};

	// =========================================================================
	// ShaderUniforms stub — fixed-function: just stores named float values
	// =========================================================================
	class ShaderUniforms
	{
	public:
		// Dummy map type — the SW backend has no real per-uniform storage
		using UniformHashMapType = int;

		ShaderUniforms() = default;

		// Stub API to match RHI::ShaderUniforms usage pattern
		inline void SetProgram(void* /*program*/, const char* /*includeOnly*/, const char* /*exclude*/) {}
		inline void SetUniformsDataPointer(std::uint8_t* /*ptr*/) {}
		inline void SetDirty(bool /*isDirty*/) {}
		inline bool HasUniform(const char* /*name*/) const { return false; }
		inline void CommitUniforms() {}

		inline std::int32_t GetUniformCount() const { return 0; }
		inline UniformCache* GetUniform(const char* /*name*/) { return nullptr; }
		inline UniformHashMapType GetAllUniforms() const { return 0; }

		FFState& GetFFState() { return ffState_; }
		const FFState& GetFFState() const { return ffState_; }

	private:
		FFState ffState_;
	};

	// =========================================================================
	// ShaderUniformBlocks stub
	// =========================================================================
	class ShaderUniformBlocks
	{
	public:
		// Dummy map type — the SW backend has no real per-block storage
		using UniformHashMapType = int;

		ShaderUniformBlocks() = default;

		inline void SetProgram(void* /*program*/) {}
		inline void SetUniformsDataPointer(std::uint8_t* /*ptr*/) {}
		inline bool HasUniformBlock(const char* /*name*/) const { return false; }
		inline void Bind() {}
		inline void CommitUniformBlocks() {}

		inline UniformBlockCache* GetUniformBlock(const char* /*name*/) { return nullptr; }
		inline UniformHashMapType GetAllUniformBlocks() const { return 0; }
	};

	// =========================================================================
	// ShaderProgram stub — no GPU shaders; renders via fixed-function pipeline
	// =========================================================================

	/// Fragment shader input — provides pixel color, UV, all bound textures, and custom data.
	struct FragmentShaderInput
	{
		std::uint8_t* rgba;							///< In/out pixel color (4 bytes, RGBA order). Modified in place.
		float u, v;									///< Interpolated texture coordinates
		std::int32_t x, y;							///< Destination pixel coordinates
		std::int32_t texWidth, texHeight;			///< Dimensions of the primary texture
		Texture* const* textures;					///< Array of bound textures (MaxTextureUnits)
		const float* color;							///< Instance/vertex color (4 floats, RGBA)
		void* userData;								///< Custom data set via Material::SetFragmentShader()
	};

	/// Fragment shader callback type for SW renderer.
	/// Called after texture sampling, before blending.
	using FragmentShaderFn = void(*)(const FragmentShaderInput& input);

	class ShaderProgram
	{
	public:
		// Satisfy the interface expected by Material / RenderResources
		enum class Status { NotLinked, LinkedWithIntrospection, Linked };
		enum class Introspection { Enabled, NoUniformsInBlocks, Disabled };
		static constexpr std::int32_t DefaultBatchSize = -1;

		ShaderProgram() = default;
		Status GetStatus() const { return Status::LinkedWithIntrospection; }
		std::int32_t GetUniformsSize()      const { return 0; }
		std::int32_t GetUniformBlocksSize() const { return 0; }
		std::int32_t GetAttributeCount()    const { return 0; }
		std::int32_t GetBatchSize()         const { return 0; }
		UniformCache* GetUniform(const char* /*name*/) { return nullptr; }
		bool IsLinked() const { return true; }
		void Use() {}

		/// Vertex-format binding — no-op for the fixed-function SW pipeline
		void DefineVertexFormat(const Buffer* /*vbo*/, const Buffer* /*ibo*/, std::uint32_t /*vboOffset*/) {}

		FFState ffState; // direct parameter access for fixed-function draws
		FragmentShaderFn fragmentShader = nullptr; // optional per-program fragment callback
	};
}

#endif
