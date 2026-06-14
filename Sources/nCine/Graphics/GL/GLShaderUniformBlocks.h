#pragma once

#include "../../Base/StaticHashMap.h"

#include "GLUniformBlockCache.h"
#include "../RenderBuffersManager.h"

namespace nCine
{
	class GLShaderProgram;
	class GLBufferObject;

	/**
		@brief Manages the uniform block caches of a shader program
		
		Owns a @ref GLUniformBlockCache for every active uniform block of a shader program. It
		provides lookup by name, a shared data pointer setup, a commit operation that copies the
		block contents into a uniform buffer, and a bind operation that binds the buffer ranges.
		The set of managed blocks can be restricted to an include-only or exclude list.
	*/
	class GLShaderUniformBlocks
	{
	public:
		/** @brief Number of buckets in the uniform block cache hashmap */
		static constexpr std::int32_t UniformBlockCachesHashSize = 4;
		using UniformHashMapType = StaticHashMap<String, GLUniformBlockCache, UniformBlockCachesHashSize>;

		/** @brief Creates an instance not associated with any shader program */
		GLShaderUniformBlocks();
		/** @brief Creates an instance importing all uniform blocks of the specified shader program */
		explicit GLShaderUniformBlocks(GLShaderProgram* shaderProgram);
		/** @brief Creates an instance importing a filtered subset of the uniform blocks of the shader program */
		GLShaderUniformBlocks(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		/** @brief Associates a shader program and imports all of its uniform blocks */
		inline void SetProgram(GLShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		/** @brief Associates a shader program and imports a filtered subset of its uniform blocks */
		void SetProgram(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		/** @brief Distributes a single host-side data buffer across the managed uniform block caches */
		void SetUniformsDataPointer(GLubyte* dataPointer);

		/** @brief Returns the number of managed uniform blocks */
		inline unsigned int GetUniformBlockCount() const {
			return uniformBlockCaches_.size();
		}
		/** @brief Returns whether a uniform block with the specified name is managed */
		inline bool HasUniformBlock(const char* name) const {
			return (uniformBlockCaches_.find(String::nullTerminatedView(name)) != nullptr);
		}
		/** @brief Returns the uniform block cache with the specified name, or `nullptr` if not found */
		GLUniformBlockCache* GetUniformBlock(const char* name);
		/** @brief Returns the hashmap of all managed uniform block caches */
		inline const UniformHashMapType GetAllUniformBlocks() const {
			return uniformBlockCaches_;
		}
		/** @brief Copies the managed blocks into a uniform buffer acquired from the buffers manager */
		void CommitUniformBlocks();

		/** @brief Binds the uniform buffer ranges of the managed blocks for rendering */
		void Bind();

	private:
		GLShaderProgram* shaderProgram_;
		// Pointer to the data of the first uniform block
		GLubyte* dataPointer_;

		// Uniform buffer parameters for binding
		RenderBuffersManager::Parameters uboParams_;

		UniformHashMapType uniformBlockCaches_;

		// Imports the uniform blocks with the option of including only some or excluding others
		void ImportUniformBlocks(const char* includeOnly, const char* exclude);
	};
}
