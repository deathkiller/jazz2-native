#pragma once

#if defined(WITH_RHI_PS1)

#include "Texture.h"
#include "Buffers.h"

#include <cstdint>

namespace nCine::RHI
{
	// =========================================================================
	// Fixed-function draw state
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
		float depth         = 0.5f;  // Maps to OT index (0 = back, kOtLen-1 = front)
		bool  hasTexture    = false;
		std::int32_t textureUnit = 0;
	};

	// =========================================================================
	// ShaderUniforms stub
	// =========================================================================
	class ShaderUniforms
	{
	public:
		ShaderUniforms() = default;

		inline void SetProgram(void*, const char*, const char*) {}
		inline void SetUniformsDataPointer(std::uint8_t*) {}
		inline void SetDirty(bool) {}
		inline bool HasUniform(const char*) const { return false; }
		inline void CommitUniforms() {}

		FFState& GetFFState()             { return ffState_; }
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
		ShaderUniformBlocks() = default;

		inline void SetProgram(void*) {}
		inline void SetUniformsDataPointer(std::uint8_t*) {}
		inline bool HasUniformBlock(const char*) const { return false; }
		inline void Bind() {}
	};

	// =========================================================================
	// UniformCache / UniformBlockCache stubs
	// =========================================================================
	class UniformCache
	{
	public:
		inline void SetFloatVector(const float*) {}
		inline void SetFloatValue(float) {}
		inline void SetIntValue(std::int32_t) {}
	};

	class UniformBlockCache
	{
	public:
		UniformCache* GetUniform(const char*) { return nullptr; }
	};

	// =========================================================================
	// ShaderProgram stub
	// =========================================================================
	class ShaderProgram
	{
	public:
		enum class Status { NotLinked, LinkedWithIntrospection, Linked };
		enum class Introspection { Enabled, NoUniformsInBlocks, Disabled };
		static constexpr std::int32_t DefaultBatchSize = -1;

		ShaderProgram() = default;
		Status GetStatus() const { return Status::LinkedWithIntrospection; }
		std::int32_t GetUniformsSize()      const { return 0; }
		std::int32_t GetUniformBlocksSize() const { return 0; }
		std::int32_t GetAttributeCount()    const { return 0; }
		UniformCache* GetUniform(const char*) { return nullptr; }
		bool IsLinked() const { return true; }
		void Use() {}

		FFState ffState;
	};

}

#endif