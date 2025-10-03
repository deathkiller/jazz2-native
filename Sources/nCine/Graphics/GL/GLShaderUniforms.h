#pragma once

#include "GLUniformCache.h"
#include "../../Base/StaticHashMap.h"

#include <string>

namespace nCine
{
	class GLShaderProgram;

	/// Handles all the uniforms of a shader program
	class GLShaderUniforms
	{
	public:
		static constexpr std::uint32_t UniformCachesHashSize = 16;
		// TODO: Using FNV1aHashFunc because other hash functions are causing graphics glitches for some reason
		using UniformHashMapType = StaticHashMap<String, GLUniformCache, UniformCachesHashSize, FNV1aHashFunc<String>>;

		GLShaderUniforms();
		explicit GLShaderUniforms(GLShaderProgram* shaderProgram);
		GLShaderUniforms(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(GLShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(GLubyte* dataPointer);
		void SetDirty(bool isDirty);

		inline std::uint32_t GetUniformCount() const {
			return uniformCaches_.size();
		}
		inline bool HasUniform(const char* name) const {
			return (uniformCaches_.find(String::nullTerminatedView(name)) != nullptr);
		}
		GLUniformCache* GetUniform(const char* name);
		inline const UniformHashMapType GetAllUniforms() const {
			return uniformCaches_;
		}
		void CommitUniforms();

	private:
		GLShaderProgram* shaderProgram_;
		UniformHashMapType uniformCaches_;

		/// Imports the uniforms with the option of including only some or excluding others
		void ImportUniforms(const char* includeOnly, const char* exclude);
	};
}
