#pragma once

#include "GxUniformCache.h"
#include "../RhiFwd.h"

#include <cstdint>
#include <vector>

namespace nCine::RHI::GX
{
	class GxShaderProgram;

	/**
		@brief Manages the loose-uniform caches of a program (aliased as `RHI::ShaderUniforms`)

		Owns a @ref GxUniformCache for every active uniform of a program that is not part of a block,
		distributes a shared host data buffer across them, and on @ref CommitUniforms() publishes every
		dirty value to the program (the software analogue of uploading uniforms). The managed set can be
		restricted to an include-only or exclude list of null-separated names.
	*/
	class GxShaderUniforms
	{
	public:
		// The GX backend stores the caches in a flat container whose value type is the cache
		// itself (the name lives inside each cache's uniform), so range-iteration yields `GxUniformCache&`
		// directly — matching the OpenGL backend's `GetAllUniforms()` iteration semantics that the render
		// batcher relies on. The `UniformHashMapType` alias mirrors the name used by the OpenGL backend.
		using UniformHashMapType = std::vector<GxUniformCache>;

		GxShaderUniforms();
		explicit GxShaderUniforms(GxShaderProgram* shaderProgram);
		GxShaderUniforms(GxShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(GxShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(GxShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(std::uint8_t* dataPointer);
		void SetDirty(bool isDirty);

		inline std::uint32_t GetUniformCount() const {
			return std::uint32_t(_uniformCaches.size());
		}
		bool HasUniform(const char* name) const;
		GxUniformCache* GetUniform(const char* name);
		/** @brief Returns the container of all managed uniform caches (range-iterates as `GxUniformCache&`) */
		inline const UniformHashMapType& GetAllUniforms() const {
			return _uniformCaches;
		}
		inline void MarkDirty() {
			_maybeDirty = true;
		}
		void CommitUniforms();

	private:
		GxShaderProgram* _shaderProgram;
		bool _maybeDirty;

		UniformHashMapType _uniformCaches;

		void ImportUniforms(const char* includeOnly, const char* exclude);
	};

	/**
		@brief Manages the uniform-block caches of a program (aliased as `RHI::ShaderUniformBlocks`)

		Owns a @ref GxUniformBlockCache per active block, distributes a shared host data buffer across
		them, copies the block contents into a suballocated range of the streaming uniform buffer on
		@ref CommitUniformBlocks(), and on @ref Bind() forwards each range to the device so the effect
		running the following draw can sample it.
	*/
	class GxShaderUniformBlocks
	{
	public:
		// Flat container whose value type is the block cache itself (the name lives inside each cache's
		// block), so range-iteration yields `GxUniformBlockCache&` directly, matching the OpenGL backend.
		using UniformHashMapType = std::vector<GxUniformBlockCache>;

		/** @brief Function that suballocates a range of the given size from the streaming uniform buffer */
		using UniformRangeAllocator = RHI::BufferRange (*)(std::uint32_t bytes);

		/** @brief Sets the allocator used by @ref CommitUniformBlocks() (registered by the pipeline at startup) */
		static void SetUniformRangeAllocator(UniformRangeAllocator allocator);

		GxShaderUniformBlocks();
		explicit GxShaderUniformBlocks(GxShaderProgram* shaderProgram);
		GxShaderUniformBlocks(GxShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(GxShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(GxShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(std::uint8_t* dataPointer);

		inline std::uint32_t GetUniformBlockCount() const {
			return std::uint32_t(_uniformBlockCaches.size());
		}
		bool HasUniformBlock(const char* name) const;
		GxUniformBlockCache* GetUniformBlock(const char* name);
		/** @brief Returns the container of all managed block caches (range-iterates as `GxUniformBlockCache&`) */
		inline const UniformHashMapType& GetAllUniformBlocks() const {
			return _uniformBlockCaches;
		}
		void CommitUniformBlocks();
		void Bind();

	private:
		static UniformRangeAllocator _uniformRangeAllocator;

		GxShaderProgram* _shaderProgram;
		std::uint8_t* _dataPointer;
		RHI::BufferRange _uboParams;

		UniformHashMapType _uniformBlockCaches;

		void ImportUniformBlocks(const char* includeOnly, const char* exclude);
	};
}
