#pragma once

#if defined(WITH_RHI_DC)

#include "Texture.h"
#include "Buffers.h"

#include <cstdint>

namespace nCine::RHI
{
	// =========================================================================
	// Fixed-function draw state
	// Mirrors the SW backend's FFState; used by the RenderCommand pipeline
	// to pass per-draw parameters without programmable shaders.
	// =========================================================================
	struct FFState
	{
		float mvpMatrix[16] = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};

		float color[4]      = { 1, 1, 1, 1 };
		float texRect[4]    = { 0, 0, 1, 1 };
		float spriteSize[2] = { 1, 1 };
		float depth         = 0.5f;       // Z in [0..1]; PVR uses 1/Z internally
		bool  hasTexture    = false;
		std::int32_t textureUnit = 0;
	};

	// =========================================================================
	// ShaderUniforms stub
	// =========================================================================
	class UniformCache; // forward decl for UniformHashMapType

	class ShaderUniforms
	{
	public:
		using UniformHashMapType = int;

		ShaderUniforms() = default;

		inline void SetProgram(void* /*program*/, const char* /*includeOnly*/, const char* /*exclude*/) {}
		inline void SetUniformsDataPointer(std::uint8_t* /*ptr*/) {}
		inline void SetDirty(bool /*isDirty*/) {}
		inline bool HasUniform(const char* /*name*/) const { return false; }
		inline void CommitUniforms() {}

		inline std::int32_t GetUniformCount() const { return 0; }
		inline UniformCache* GetUniform(const char* /*name*/) { return nullptr; }
		inline UniformHashMapType GetAllUniforms() const { return 0; }

		FFState& GetFFState()             { return ffState_; }
		const FFState& GetFFState() const { return ffState_; }

	private:
		FFState ffState_;
	};

	// =========================================================================
	// ShaderUniformBlocks stub
	// =========================================================================
	class UniformBlockCache; // forward decl for UniformHashMapType

	class ShaderUniformBlocks
	{
	public:
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
	// UniformCache / UniformBlockCache stubs
	// =========================================================================
	class UniformCache
	{
	public:
		inline bool SetFloatVector(const float* /*values*/) { return true; }
		inline bool SetFloatValue(float /*v0*/) { return true; }
		inline bool SetFloatValue(float /*v0*/, float /*v1*/) { return true; }
		inline bool SetFloatValue(float /*v0*/, float /*v1*/, float /*v2*/) { return true; }
		inline bool SetFloatValue(float /*v0*/, float /*v1*/, float /*v2*/, float /*v3*/) { return true; }
		inline bool SetIntVector(const std::int32_t* /*values*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/, std::int32_t /*v1*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/, std::int32_t /*v1*/, std::int32_t /*v2*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/, std::int32_t /*v1*/, std::int32_t /*v2*/, std::int32_t /*v3*/) { return true; }
		inline std::int32_t GetIntValue(std::int32_t /*componentIdx*/) const { return 0; }
		// Dirty flag — no-op for the DC backend
		inline void SetDirty(bool /*isDirty*/) {}
	};

	class UniformBlockCache
	{
	public:
		UniformCache* GetUniform(const char* /*name*/) { return nullptr; }

		inline std::uint32_t GetSize()        const { return 0; }
		inline std::uint32_t GetAlignAmount() const { return 0; }
		inline const std::uint8_t* GetDataPointer() const { return nullptr; }
		inline bool CopyData(const std::uint8_t* /*src*/)                                                       { return false; }
		inline bool CopyData(std::uint32_t /*destIndex*/, const std::uint8_t* /*src*/, std::uint32_t /*bytes*/) { return false; }
		inline void SetUsedSize(std::uint32_t /*size*/) {}
	};

	// =========================================================================
	// ShaderProgram stub - no GPU shaders on Dreamcast
	// =========================================================================
	class ShaderProgram
	{
	public:
		enum class Status { NotLinked, LinkedWithIntrospection, Linked };
		enum class Introspection { Enabled, NoUniformsInBlocks, Disabled };
		static constexpr std::int32_t DefaultBatchSize = -1;

		ShaderProgram() = default;
		Status GetStatus() const { return Status::LinkedWithIntrospection; }
		std::int32_t GetUniformsSize()      const { return 0;    }
		std::int32_t GetUniformBlocksSize() const { return 0; }
		std::int32_t GetAttributeCount()    const { return 0;    }
		std::int32_t GetBatchSize()         const { return 0;    }
		UniformCache* GetUniform(const char* /*name*/) { return nullptr; }
		bool IsLinked() const { return true; }
		void Use() {}

		/// Vertex-format binding — no-op for the fixed-function DC pipeline
		void DefineVertexFormat(const Buffer* /*vbo*/, const Buffer* /*ibo*/, std::uint32_t /*vboOffset*/) {}

		FFState ffState; // direct access for fixed-function draws
	};

}

#endif