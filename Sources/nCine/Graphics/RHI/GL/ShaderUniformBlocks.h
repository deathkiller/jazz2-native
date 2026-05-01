#pragma once

#if defined(WITH_RHI_GL)

#include "../../../Base/StaticHashMap.h"

#include "UniformBlockCache.h"
#include "../RenderBufferParams.h"

namespace nCine::RHI
{
	class ShaderProgram;
	class Buffer;

	/// Handles all the uniform blocks of a shader program
	class ShaderUniformBlocks
	{
	public:
		static constexpr std::int32_t UniformBlockCachesHashSize = 4;
		using UniformHashMapType = StaticHashMap<String, UniformBlockCache, UniformBlockCachesHashSize>;

		ShaderUniformBlocks();
		explicit ShaderUniformBlocks(ShaderProgram* shaderProgram);
		ShaderUniformBlocks(ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(ShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(GLubyte* dataPointer);

		inline unsigned int GetUniformBlockCount() const {
			return uniformBlockCaches_.size();
		}
		inline bool HasUniformBlock(const char* name) const {
			return (uniformBlockCaches_.find(String::nullTerminatedView(name)) != nullptr);
		}
		UniformBlockCache* GetUniformBlock(const char* name);
		inline const UniformHashMapType GetAllUniformBlocks() const {
			return uniformBlockCaches_;
		}
		void CommitUniformBlocks();

		void Bind();

	private:
		ShaderProgram* shaderProgram_;
		/// Pointer to the data of the first uniform block
		GLubyte* dataPointer_;

		/// Uniform buffer parameters for binding
		BufferParams uboParams_;

		UniformHashMapType uniformBlockCaches_;

		/// Imports the uniform blocks with the option of including only some or excluding others
		void ImportUniformBlocks(const char* includeOnly, const char* exclude);
	};
}

#endif // defined(WITH_RHI_GL)
