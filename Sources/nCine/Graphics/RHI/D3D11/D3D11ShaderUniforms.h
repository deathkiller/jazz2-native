#pragma once

#include "D3D11UniformCache.h"
#include "../RhiFwd.h"

#include <cstdint>
#include <vector>

namespace nCine::RHI::D3D11
{
	class D3D11ShaderProgram;

	/**
		@brief Manages the loose-uniform caches of a program (aliased as `RHI::ShaderUniforms`)

		Owns a @ref D3D11UniformCache for every active uniform of a program that is not part of a block,
		distributes a shared host data buffer across them, and on @ref CommitUniforms() publishes every dirty
		value to the program. The managed set can be restricted to an include-only or exclude list of
		null-separated names.
	*/
	class D3D11ShaderUniforms
	{
	public:
		// The caches are stored in a flat container whose value type is the cache itself (the name lives
		// inside each cache's uniform), so range-iteration yields `D3D11UniformCache&` directly — matching
		// the OpenGL backend's `GetAllUniforms()` iteration semantics that the render batcher relies on.
		using UniformHashMapType = std::vector<D3D11UniformCache>;

		D3D11ShaderUniforms();
		explicit D3D11ShaderUniforms(D3D11ShaderProgram* shaderProgram);
		D3D11ShaderUniforms(D3D11ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(D3D11ShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(D3D11ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(std::uint8_t* dataPointer);
		void SetDirty(bool isDirty);

		inline std::uint32_t GetUniformCount() const {
			return std::uint32_t(uniformCaches_.size());
		}
		bool HasUniform(const char* name) const;
		D3D11UniformCache* GetUniform(const char* name);
		/** @brief Returns the container of all managed uniform caches (range-iterates as `D3D11UniformCache&`) */
		inline const UniformHashMapType& GetAllUniforms() const {
			return uniformCaches_;
		}
		inline void MarkDirty() {
			maybeDirty_ = true;
		}
		void CommitUniforms();

	private:
		D3D11ShaderProgram* shaderProgram_;
		bool maybeDirty_;

		UniformHashMapType uniformCaches_;

		void ImportUniforms(const char* includeOnly, const char* exclude);
	};

	/**
		@brief Manages the uniform-block caches of a program (aliased as `RHI::ShaderUniformBlocks`)

		Owns a @ref D3D11UniformBlockCache per active block, distributes a shared host data buffer across
		them, copies the block contents into a suballocated range of the streaming uniform buffer on @ref
		CommitUniformBlocks(), and on @ref Bind() forwards each range to the device.
	*/
	class D3D11ShaderUniformBlocks
	{
	public:
		// Flat container whose value type is the block cache itself (the name lives inside each cache's
		// block), so range-iteration yields `D3D11UniformBlockCache&` directly, matching the OpenGL backend.
		using UniformHashMapType = std::vector<D3D11UniformBlockCache>;

		/** @brief Function that suballocates a range of the given size from the streaming uniform buffer */
		using UniformRangeAllocator = RHI::BufferRange (*)(std::uint32_t bytes);

		/** @brief Sets the allocator used by @ref CommitUniformBlocks() (registered by the pipeline at startup) */
		static void SetUniformRangeAllocator(UniformRangeAllocator allocator);

		D3D11ShaderUniformBlocks();
		explicit D3D11ShaderUniformBlocks(D3D11ShaderProgram* shaderProgram);
		D3D11ShaderUniformBlocks(D3D11ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(D3D11ShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(D3D11ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(std::uint8_t* dataPointer);

		inline std::uint32_t GetUniformBlockCount() const {
			return std::uint32_t(uniformBlockCaches_.size());
		}
		bool HasUniformBlock(const char* name) const;
		D3D11UniformBlockCache* GetUniformBlock(const char* name);
		/** @brief Returns the container of all managed block caches (range-iterates as `D3D11UniformBlockCache&`) */
		inline const UniformHashMapType& GetAllUniformBlocks() const {
			return uniformBlockCaches_;
		}
		void CommitUniformBlocks();
		void Bind();

	private:
		static UniformRangeAllocator uniformRangeAllocator_;

		D3D11ShaderProgram* shaderProgram_;
		std::uint8_t* dataPointer_;
		RHI::BufferRange uboParams_;

		UniformHashMapType uniformBlockCaches_;

		void ImportUniformBlocks(const char* includeOnly, const char* exclude);
	};
}
