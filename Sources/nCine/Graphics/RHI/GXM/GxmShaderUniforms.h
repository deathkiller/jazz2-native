#pragma once

#include "GxmUniformCache.h"
#include "../RhiFwd.h"

#include <cstdint>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine::RHI::GXM
{
	class GxmShaderProgram;

	/**
		@brief Manages the loose-uniform caches of a program (aliased as `RHI::ShaderUniforms`)

		Owns a @ref GxmUniformCache for every active uniform of a program that is not part of a block,
		distributes a shared host data buffer across them, and on @ref CommitUniforms() publishes every dirty
		value to the program. The managed set can be restricted to an include-only or exclude list of
		null-separated names.
	*/
	class GxmShaderUniforms
	{
	public:
		// The caches are stored in a flat container whose value type is the cache itself (the name lives
		// inside each cache's uniform), so range-iteration yields `GxmUniformCache&` directly — matching
		// the OpenGL backend's `GetAllUniforms()` iteration semantics that the render batcher relies on.
		using UniformHashMapType = SmallVector<GxmUniformCache, 0>;

		GxmShaderUniforms();
		explicit GxmShaderUniforms(GxmShaderProgram* shaderProgram);
		GxmShaderUniforms(GxmShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(GxmShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(GxmShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(std::uint8_t* dataPointer);
		void SetDirty(bool isDirty);

		inline std::uint32_t GetUniformCount() const {
			return std::uint32_t(_uniformCaches.size());
		}
		bool HasUniform(const char* name) const;
		GxmUniformCache* GetUniform(const char* name);
		/** @brief Returns the container of all managed uniform caches (range-iterates as `GxmUniformCache&`) */
		inline const UniformHashMapType& GetAllUniforms() const {
			return _uniformCaches;
		}
		inline void MarkDirty() {
			_maybeDirty = true;
		}
		void CommitUniforms();

	private:
		GxmShaderProgram* _shaderProgram;
		bool _maybeDirty;

		UniformHashMapType _uniformCaches;

		void ImportUniforms(const char* includeOnly, const char* exclude);
	};

	/**
		@brief Manages the uniform-block caches of a program (aliased as `RHI::ShaderUniformBlocks`)

		Owns a @ref GxmUniformBlockCache per active block, distributes a shared host data buffer across
		them, copies the block contents into a suballocated range of the streaming uniform buffer on @ref
		CommitUniformBlocks(), and on @ref Bind() forwards each range to the device.
	*/
	class GxmShaderUniformBlocks
	{
	public:
		// Flat container whose value type is the block cache itself (the name lives inside each cache's
		// block), so range-iteration yields `GxmUniformBlockCache&` directly, matching the OpenGL backend.
		using UniformHashMapType = SmallVector<GxmUniformBlockCache, 0>;

		/** @brief Function that suballocates a range of the given size from the streaming uniform buffer */
		using UniformRangeAllocator = RHI::BufferRange (*)(std::uint32_t bytes);

		/** @brief Sets the allocator used by @ref CommitUniformBlocks() (registered by the pipeline at startup) */
		static void SetUniformRangeAllocator(UniformRangeAllocator allocator);

		GxmShaderUniformBlocks();
		explicit GxmShaderUniformBlocks(GxmShaderProgram* shaderProgram);
		GxmShaderUniformBlocks(GxmShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(GxmShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(GxmShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(std::uint8_t* dataPointer);

		inline std::uint32_t GetUniformBlockCount() const {
			return std::uint32_t(_uniformBlockCaches.size());
		}
		bool HasUniformBlock(const char* name) const;
		GxmUniformBlockCache* GetUniformBlock(const char* name);
		/** @brief Returns the container of all managed block caches (range-iterates as `GxmUniformBlockCache&`) */
		inline const UniformHashMapType& GetAllUniformBlocks() const {
			return _uniformBlockCaches;
		}
		void CommitUniformBlocks();
		void Bind();

	private:
		static UniformRangeAllocator _uniformRangeAllocator;

		GxmShaderProgram* _shaderProgram;
		std::uint8_t* _dataPointer;
		RHI::BufferRange _uboParams;

		UniformHashMapType _uniformBlockCaches;

		void ImportUniformBlocks(const char* includeOnly, const char* exclude);
	};
}
