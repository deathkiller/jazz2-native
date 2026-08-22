#pragma once

#include "GsUniformCache.h"
#include "../RhiFwd.h"

#include <cstdint>
#include <vector>

namespace nCine::RHI::GS
{
	class GsShaderProgram;

	/**
		@brief Manages the loose-uniform caches of a program (aliased as `RHI::ShaderUniforms`)

		Owns a @ref GsUniformCache for every active uniform of a program that is not part of a block,
		distributes a shared host data buffer across them, and on @ref CommitUniforms() publishes every dirty
		value to the program - which is how a generated fixed-function effect reaches a uniform by name at draw
		time, the analogue of uploading uniforms. The managed set can be restricted to an include-only or
		exclude list of null-separated names.
	*/
	class GsShaderUniforms
	{
	public:
		// The GS backend stores the caches in a flat container whose value type is the cache itself (the name
		// lives inside each cache's uniform), so range-iteration yields `GsUniformCache&` directly - matching
		// the OpenGL backend's `GetAllUniforms()` iteration semantics that the render batcher relies on. The
		// `UniformHashMapType` alias mirrors the name used by the OpenGL backend.
		using UniformHashMapType = std::vector<GsUniformCache>;

		GsShaderUniforms();
		explicit GsShaderUniforms(GsShaderProgram* shaderProgram);
		GsShaderUniforms(GsShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(GsShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(GsShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(std::uint8_t* dataPointer);
		void SetDirty(bool isDirty);

		inline std::uint32_t GetUniformCount() const {
			return std::uint32_t(_uniformCaches.size());
		}
		bool HasUniform(const char* name) const;
		GsUniformCache* GetUniform(const char* name);
		/** @brief Returns the container of all managed uniform caches (range-iterates as `GsUniformCache&`) */
		inline const UniformHashMapType& GetAllUniforms() const {
			return _uniformCaches;
		}
		inline void MarkDirty() {
			_maybeDirty = true;
		}
		void CommitUniforms();

	private:
		GsShaderProgram* _shaderProgram;
		bool _maybeDirty;

		UniformHashMapType _uniformCaches;
		// Fingerprints of the _uniformCaches names, in the same order (see @ref HashUniformName)
		std::vector<std::uint32_t> _uniformNameHashes;

		void ImportUniforms(const char* includeOnly, const char* exclude);
	};

	/**
		@brief Manages the uniform-block caches of a program (aliased as `RHI::ShaderUniformBlocks`)

		Owns a @ref GsUniformBlockCache per active block, distributes a shared host data buffer across them,
		copies the block contents into a suballocated range of the streaming uniform buffer on
		@ref CommitUniformBlocks(), and on @ref Bind() forwards each range to the device so the effect running
		the following draw can decode it.
	*/
	class GsShaderUniformBlocks
	{
	public:
		// Flat container whose value type is the block cache itself (the name lives inside each cache's
		// block), so range-iteration yields `GsUniformBlockCache&` directly, matching the OpenGL backend.
		using UniformHashMapType = std::vector<GsUniformBlockCache>;

		/** @brief Function that suballocates a range of the given size from the streaming uniform buffer */
		using UniformRangeAllocator = RHI::BufferRange (*)(std::uint32_t bytes);

		/** @brief Sets the allocator used by @ref CommitUniformBlocks() (registered by the pipeline at startup) */
		static void SetUniformRangeAllocator(UniformRangeAllocator allocator);

		GsShaderUniformBlocks();
		explicit GsShaderUniformBlocks(GsShaderProgram* shaderProgram);
		GsShaderUniformBlocks(GsShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(GsShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(GsShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(std::uint8_t* dataPointer);

		inline std::uint32_t GetUniformBlockCount() const {
			return std::uint32_t(_uniformBlockCaches.size());
		}
		bool HasUniformBlock(const char* name) const;
		GsUniformBlockCache* GetUniformBlock(const char* name);
		/** @brief Returns the container of all managed block caches (range-iterates as `GsUniformBlockCache&`) */
		inline const UniformHashMapType& GetAllUniformBlocks() const {
			return _uniformBlockCaches;
		}
		void CommitUniformBlocks();
		void Bind();

	private:
		static UniformRangeAllocator _uniformRangeAllocator;

		GsShaderProgram* _shaderProgram;
		std::uint8_t* _dataPointer;
		RHI::BufferRange _uboParams;

		UniformHashMapType _uniformBlockCaches;

		void ImportUniformBlocks(const char* includeOnly, const char* exclude);
	};
}
