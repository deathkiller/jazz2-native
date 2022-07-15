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
			return (uniformCaches_.find(name) != nullptr);
		}
		GLUniformCache* uniform(const char* name);
		void commitUniforms();

	private:
		GLShaderProgram* shaderProgram_;

		static constexpr int UniformCachesHashSize = 16;
		StaticHashMap<std::string, GLUniformCache, UniformCachesHashSize> uniformCaches_;
		/// A dummy uniform cache returned when a uniform is not found in the hashmap
		static GLUniformCache uniformNotFound_;

		/// Imports the uniforms with the option of including only some or excluing others
		void importUniforms(const char* includeOnly, const char* exclude);
	};

}
