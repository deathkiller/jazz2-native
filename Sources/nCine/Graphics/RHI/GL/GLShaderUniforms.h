#pragma once

#include "GLUniformCache.h"
#include "../../../Base/StaticHashMap.h"

#include <string>

namespace nCine::RhiGL
{
	class GLShaderProgram;

	/**
		@brief Manages the uniform caches of a shader program
		
		Owns a @ref GLUniformCache for every active uniform of a shader program that is not part
		of a uniform block. It provides lookup by name, a shared data pointer setup, and a commit
		operation that uploads all dirty uniforms to the GL. The set of managed uniforms can be
		restricted to an include-only or exclude list.
	*/
	class GLShaderUniforms
	{
	public:
		/** @brief Number of buckets in the uniform cache hashmap */
		static constexpr std::uint32_t UniformCachesHashSize = 16;
		// TODO: Using FNV1aHashFunc because other hash functions are causing graphics glitches for some reason
		using UniformHashMapType = StaticHashMap<String, GLUniformCache, UniformCachesHashSize, FNV1aHashFunc<String>>;

		/** @brief Creates an instance not associated with any shader program */
		GLShaderUniforms();
		/** @brief Creates an instance importing all uniforms of the specified shader program */
		explicit GLShaderUniforms(GLShaderProgram* shaderProgram);
		/** @brief Creates an instance importing a filtered subset of the uniforms of the shader program */
		GLShaderUniforms(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		/** @brief Associates a shader program and imports all of its uniforms */
		inline void SetProgram(GLShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		/** @brief Associates a shader program and imports a filtered subset of its uniforms */
		void SetProgram(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		/** @brief Distributes a single host-side data buffer across the managed uniform caches */
		void SetUniformsDataPointer(GLubyte* dataPointer);
		/** @brief Sets the dirty flag of all managed uniform caches */
		void SetDirty(bool isDirty);

		/** @brief Returns the number of managed uniforms */
		inline std::uint32_t GetUniformCount() const {
			return uniformCaches_.size();
		}
		/** @brief Returns whether a uniform with the specified name is managed */
		inline bool HasUniform(const char* name) const {
			return (uniformCaches_.find(String::nullTerminatedView(name)) != nullptr);
		}
		/** @brief Returns the uniform cache with the specified name, or `nullptr` if not found */
		GLUniformCache* GetUniform(const char* name);
		/** @brief Returns the hashmap of all managed uniform caches */
		inline const UniformHashMapType& GetAllUniforms() const {
			return uniformCaches_;
		}
		/**
		 * @brief Notes that a managed uniform cache may have been modified externally
		 *
		 * Called automatically by the non-const @ref GetUniform(), it only has to be called
		 * explicitly when a uniform is written through a long-lived cached pointer.
		 */
		inline void MarkDirty() {
			maybeDirty_ = true;
		}
		/** @brief Uses the program and uploads all dirty uniforms to the GL */
		void CommitUniforms();

	private:
		GLShaderProgram* shaderProgram_;
		// Conservative flag for the commit early-out, set whenever a uniform cache may have been dirtied
		bool maybeDirty_;
		UniformHashMapType uniformCaches_;

		// Imports the uniforms with the option of including only some or excluding others
		void ImportUniforms(const char* includeOnly, const char* exclude);
	};
}
