#pragma once

#include "../../Base/StaticHashMap.h"

#include "GLUniformBlockCache.h"
#include "../RenderBuffersManager.h"

namespace nCine
{
	class GLShaderProgram;
	class GLBufferObject;
	class GLShaderUniforms;


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

#if defined(WITH_OPENGL2)
		/// Set shader uniforms reference for OpenGL 2.x fallback
		void SetShaderUniforms(GLShaderUniforms* shaderUniforms) {
			gl2ShaderUniforms_ = shaderUniforms;
		}
#endif

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

#if defined(WITH_OPENGL2)
		/// Copy fallback cache values to shader uniforms (OpenGL 2.x only)
		/// Called from Material::CommitUniforms() right before drawing
		inline void CommitFallbackCacheToShaderUniforms() {
			if (gl2ShaderUniforms_ != nullptr) {
				CommitFallbackCache();
			}
		}
#endif

	private:
		GLShaderProgram* shaderProgram_;
		/// Pointer to the data of the first uniform block
		GLubyte* dataPointer_;

		/// Uniform buffer parameters for binding
		RenderBuffersManager::Parameters uboParams_;

		UniformHashMapType uniformBlockCaches_;

#if defined(WITH_OPENGL2)
		/// OpenGL 2.x fallback: GLUniformBlockCache instance that stores per-object uniform values
		/// and copies them to shader uniforms during CommitUniformBlocks()
		GLUniformBlockCache gl2FallbackCache_;
		/// Reference to shader uniforms for OpenGL 2.x fallback
		GLShaderUniforms* gl2ShaderUniforms_;
		/// Backing store for fallback uniform values (per Material instance)
		std::uint8_t gl2FallbackBackingStore_[256];
		
		/// Initialize the fallback cache for OpenGL 2.x
		void InitializeFallbackCache();
		/// Commit fallback cache values to shader uniforms
		void CommitFallbackCache();
#endif

		/// Imports the uniform blocks with the option of including only some or excluding others
		void ImportUniformBlocks(const char* includeOnly, const char* exclude);
	};
}
