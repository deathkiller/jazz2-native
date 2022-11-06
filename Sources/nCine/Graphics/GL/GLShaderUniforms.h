#pragma once

#include "GLUniformCache.h"
#include "../../Base/StaticHashMap.h"

#include <string>

namespace nCine
{
	class GLShaderProgram;

	/// A class to handle all the uniforms of a shader program using a hashmap
	class GLShaderUniforms
	{
	public:
		static constexpr int UniformCachesHashSize = 16;
		using UniformHashMapType = StaticHashMap<String, GLUniformCache, UniformCachesHashSize>;

		GLShaderUniforms();
		explicit GLShaderUniforms(GLShaderProgram* shaderProgram);
		GLShaderUniforms(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		inline void setProgram(GLShaderProgram* shaderProgram) {
			setProgram(shaderProgram, nullptr, nullptr);
		}
		void setProgram(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void setUniformsDataPointer(GLubyte* dataPointer);
		void setDirty(bool isDirty);

		inline unsigned int numUniforms() const {
			return uniformCaches_.size();
		}
		inline bool hasUniform(const char* name) const {
			return (uniformCaches_.find(String::nullTerminatedView(name)) != nullptr);
		}
		GLUniformCache* uniform(const char* name);
		inline const UniformHashMapType allUniforms() const {
			return uniformCaches_;
		}
		void commitUniforms();

	private:
		GLShaderProgram* shaderProgram_;
		UniformHashMapType uniformCaches_;

		/// Imports the uniforms with the option of including only some or excluing others
		void importUniforms(const char* includeOnly, const char* exclude);
	};

}
