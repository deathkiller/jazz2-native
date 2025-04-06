#pragma once

#include "../../Base/StaticHashMap.h"

#include "GLUniformBlockCache.h"
#include "../RenderBuffersManager.h"

namespace nCine
{
	class GLShaderProgram;
	class GLBufferObject;

	/// Handles all the uniform blocks of a shader program
	class GLShaderUniformBlocks
	{
	public:
		static constexpr std::int32_t UniformBlockCachesHashSize = 4;
		using UniformHashMapType = StaticHashMap<String, GLUniformBlockCache, UniformBlockCachesHashSize>;

		GLShaderUniformBlocks();
		explicit GLShaderUniformBlocks(GLShaderProgram* shaderProgram);
		GLShaderUniformBlocks(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(GLShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(GLubyte* dataPointer);

		inline unsigned int GetUniformBlockCount() const {
			return uniformBlockCaches_.size();
		}
		inline bool HasUniformBlock(const char* name) const {
			return (uniformBlockCaches_.find(String::nullTerminatedView(name)) != nullptr);
		}
		GLUniformBlockCache* GetUniformBlock(const char* name);
		inline const UniformHashMapType GetAllUniformBlocks() const {
			return uniformBlockCaches_;
		}
		void CommitUniformBlocks();

		void Bind();

	private:
		GLShaderProgram* shaderProgram_;
		/// Pointer to the data of the first uniform block
		GLubyte* dataPointer_;

		/// Uniform buffer parameters for binding
		RenderBuffersManager::Parameters uboParams_;

		UniformHashMapType uniformBlockCaches_;

		/// Imports the uniform blocks with the option of including only some or excluding others
		void ImportUniformBlocks(const char* includeOnly, const char* exclude);
	};
}
