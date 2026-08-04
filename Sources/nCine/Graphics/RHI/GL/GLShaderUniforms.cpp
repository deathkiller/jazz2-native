#include "GLShaderUniforms.h"
#include "GLShaderProgram.h"
#include "GLUniformCache.h"
#include "../../RenderResources.h"
#include "../../../Base/StaticHashMapIterator.h"
#include "../../../../Main.h"

namespace nCine::RHI::GL
{
	GLShaderUniforms::GLShaderUniforms()
		: _shaderProgram(nullptr), _maybeDirty(true)
	{
	}

	GLShaderUniforms::GLShaderUniforms(GLShaderProgram* shaderProgram)
		: GLShaderUniforms(shaderProgram, nullptr, nullptr)
	{
		SetProgram(shaderProgram);
	}

	GLShaderUniforms::GLShaderUniforms(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: GLShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void GLShaderUniforms::SetProgram(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		DEATH_ASSERT(shaderProgram != nullptr);

		_shaderProgram = shaderProgram;
		_shaderProgram->ProcessDeferredQueries();
		_uniformCaches.clear();
		_maybeDirty = true;

		if (_shaderProgram->GetStatus() == GLShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void GLShaderUniforms::SetUniformsDataPointer(GLubyte* dataPointer)
	{
		DEATH_ASSERT(dataPointer != nullptr);

		if (_shaderProgram->GetStatus() != GLShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_maybeDirty = true;
		std::uint32_t offset = 0;
		for (GLUniformCache& uniformCache : _uniformCaches) {
			uniformCache.SetDataPointer(dataPointer + offset);
			offset += uniformCache.GetUniform()->GetMemorySize();
		}
	}

	void GLShaderUniforms::SetDirty(bool isDirty)
	{
		if (_shaderProgram->GetStatus() != GLShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_maybeDirty = isDirty;
		for (auto& uniform : _uniformCaches) {
			uniform.SetDirty(isDirty);
		}
	}

	GLUniformCache* GLShaderUniforms::GetUniform(const char* name)
	{
		DEATH_ASSERT(name != nullptr);
		GLUniformCache* uniformCache = nullptr;

		if (_shaderProgram != nullptr) {
			uniformCache = _uniformCaches.find(String::nullTerminatedView(name));
			// The caller may write to the returned cache, so the commit early-out has to be pessimistic
			if (uniformCache != nullptr) {
				_maybeDirty = true;
			}
		} else {
			LOGE("Cannot find uniform \"{}\", no shader program associated", name);
		}
		return uniformCache;
	}

	void GLShaderUniforms::CommitUniforms()
	{
		if (_shaderProgram != nullptr) {
			if (_maybeDirty && _shaderProgram->GetStatus() == GLShaderProgram::Status::LinkedWithIntrospection) {
				_shaderProgram->Use();
				for (auto& uniform : _uniformCaches) {
					uniform.CommitValue();
				}
				_maybeDirty = false;
			}
		} else {
			LOGE("No shader program associated");
		}
	}

	void GLShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		constexpr std::uint32_t MaxUniformName = 128;

		std::uint32_t importedCount = 0;
		for (const GLUniform& uniform : _shaderProgram->_uniforms) {
			const char* uniformName = uniform.GetName();
			const char* currentIncludeOnly = includeOnly;
			const char* currentExclude = exclude;
			bool shouldImport = true;

			if (includeOnly != nullptr) {
				shouldImport = false;
				while (currentIncludeOnly != nullptr && currentIncludeOnly[0] != '\0') {
					if (strncmp(currentIncludeOnly, uniformName, MaxUniformName) == 0) {
						shouldImport = true;
						break;
					}
					currentIncludeOnly += strnlen(currentIncludeOnly, MaxUniformName) + 1;
				}
			}

			if (exclude != nullptr) {
				while (currentExclude != nullptr && currentExclude[0] != '\0') {
					if (strncmp(currentExclude, uniformName, MaxUniformName) == 0) {
						shouldImport = false;
						break;
					}
					currentExclude += strnlen(currentExclude, MaxUniformName) + 1;
				}
			}

			if (shouldImport) {
				GLUniformCache uniformCache(&uniform);
				_uniformCaches[uniformName] = uniformCache;
				importedCount++;
			}
		}

		if (importedCount > UniformCachesHashSize) {
			LOGW("More imported uniform blocks ({}) than hashmap buckets ({})", importedCount, UniformCachesHashSize);
		}
	}
}
