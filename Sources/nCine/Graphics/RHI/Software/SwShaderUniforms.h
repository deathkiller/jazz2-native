#pragma once

#include "SwUniformCache.h"
#include "../RhiFwd.h"

#include <cstdint>
#include <vector>

namespace nCine::RHI::Software
{
	class SwShaderProgram;

	/**
		@brief Manages the loose-uniform caches of a program (aliased as `RHI::ShaderUniforms`)

		Owns a @ref SwUniformCache for every active uniform of a program that is not part of a block,
		distributes a shared host data buffer across them, and on @ref CommitUniforms() publishes every
		dirty value to the program (the software analogue of uploading uniforms). The managed set can be
		restricted to an include-only or exclude list of null-separated names.
	*/
	class SwShaderUniforms
	{
	public:
		// The software backend stores the caches in a flat container whose value type is the cache
		// itself (the name lives inside each cache's uniform), so range-iteration yields `SwUniformCache&`
		// directly — matching the OpenGL backend's `GetAllUniforms()` iteration semantics that the render
		// batcher relies on. The `UniformHashMapType` alias mirrors the name used by the OpenGL backend.
		using UniformHashMapType = std::vector<SwUniformCache>;

		SwShaderUniforms();
		explicit SwShaderUniforms(SwShaderProgram* shaderProgram);
		SwShaderUniforms(SwShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(SwShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(SwShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(std::uint8_t* dataPointer);
		void SetDirty(bool isDirty);

		inline std::uint32_t GetUniformCount() const {
			return std::uint32_t(uniformCaches_.size());
		}
		bool HasUniform(const char* name) const;
		SwUniformCache* GetUniform(const char* name);
		/** @brief Returns the container of all managed uniform caches (range-iterates as `SwUniformCache&`) */
		inline const UniformHashMapType& GetAllUniforms() const {
			return uniformCaches_;
		}
		inline void MarkDirty() {
			maybeDirty_ = true;
		}
		void CommitUniforms();

	private:
		SwShaderProgram* shaderProgram_;
		bool maybeDirty_;

		UniformHashMapType uniformCaches_;

		void ImportUniforms(const char* includeOnly, const char* exclude);
	};

	/**
		@brief Manages the uniform-block caches of a program (aliased as `RHI::ShaderUniformBlocks`)

		Owns a @ref SwUniformBlockCache per active block, distributes a shared host data buffer across
		them, copies the block contents into a suballocated range of the streaming uniform buffer on
		@ref CommitUniformBlocks(), and on @ref Bind() forwards each range to the device so the effect
		running the following draw can sample it.
	*/
	class SwShaderUniformBlocks
	{
	public:
		// Flat container whose value type is the block cache itself (the name lives inside each cache's
		// block), so range-iteration yields `SwUniformBlockCache&` directly, matching the OpenGL backend.
		using UniformHashMapType = std::vector<SwUniformBlockCache>;

		/** @brief Function that suballocates a range of the given size from the streaming uniform buffer */
		using UniformRangeAllocator = RHI::BufferRange (*)(std::uint32_t bytes);

		/** @brief Sets the allocator used by @ref CommitUniformBlocks() (registered by the pipeline at startup) */
		static void SetUniformRangeAllocator(UniformRangeAllocator allocator);

		SwShaderUniformBlocks();
		explicit SwShaderUniformBlocks(SwShaderProgram* shaderProgram);
		SwShaderUniformBlocks(SwShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(SwShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(SwShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(std::uint8_t* dataPointer);

		inline std::uint32_t GetUniformBlockCount() const {
			return std::uint32_t(uniformBlockCaches_.size());
		}
		bool HasUniformBlock(const char* name) const;
		SwUniformBlockCache* GetUniformBlock(const char* name);
		/** @brief Returns the container of all managed block caches (range-iterates as `SwUniformBlockCache&`) */
		inline const UniformHashMapType& GetAllUniformBlocks() const {
			return uniformBlockCaches_;
		}
		void CommitUniformBlocks();
		void Bind();

	private:
		static UniformRangeAllocator uniformRangeAllocator_;

		SwShaderProgram* shaderProgram_;
		std::uint8_t* dataPointer_;
		RHI::BufferRange uboParams_;

		UniformHashMapType uniformBlockCaches_;

		void ImportUniformBlocks(const char* includeOnly, const char* exclude);
	};
}
