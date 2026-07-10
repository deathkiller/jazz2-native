#include "GLShaderUniforms.h"
#include "GLShaderProgram.h"
#include "GLUniformCache.h"
#include "../RenderResources.h"
#include "../../Base/StaticHashMapIterator.h"
#include "../../../Main.h"

namespace nCine
{
	GLShaderUniforms::GLShaderUniforms()
		: shaderProgram_(nullptr), maybeDirty_(true)
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

		shaderProgram_ = shaderProgram;
		shaderProgram_->ProcessDeferredQueries();
		uniformCaches_.clear();
		maybeDirty_ = true;

		if (shaderProgram_->GetStatus() == GLShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void GLShaderUniforms::SetUniformsDataPointer(GLubyte* dataPointer)
	{
		DEATH_ASSERT(dataPointer != nullptr);

		if (shaderProgram_->GetStatus() != GLShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		maybeDirty_ = true;
		std::uint32_t offset = 0;
		for (GLUniformCache& uniformCache : uniformCaches_) {
			uniformCache.SetDataPointer(dataPointer + offset);
			offset += uniformCache.GetUniform()->GetMemorySize();
		}
	}

	void GLShaderUniforms::SetDirty(bool isDirty)
	{
		if (shaderProgram_->GetStatus() != GLShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		maybeDirty_ = isDirty;
		for (auto& uniform : uniformCaches_) {
			uniform.SetDirty(isDirty);
		}
	}

	GLUniformCache* GLShaderUniforms::GetUniform(const char* name)
	{
		DEATH_ASSERT(name != nullptr);
		GLUniformCache* uniformCache = nullptr;

		if (shaderProgram_ != nullptr) {
			uniformCache = uniformCaches_.find(String::nullTerminatedView(name));
			// The caller may write to the returned cache, so the commit early-out has to be pessimistic
			if (uniformCache != nullptr) {
				maybeDirty_ = true;
			}
		} else {
			LOGE("Cannot find uniform \"{}\", no shader program associated", name);
		}
		return uniformCache;
	}

	void GLShaderUniforms::CommitUniforms()
	{
		if (shaderProgram_ != nullptr) {
			if (maybeDirty_ && shaderProgram_->GetStatus() == GLShaderProgram::Status::LinkedWithIntrospection) {
				shaderProgram_->Use();
				for (auto& uniform : uniformCaches_) {
					uniform.CommitValue();
				}
				maybeDirty_ = false;
			}
		} else {
			LOGE("No shader program associated");
		}
	}

	void GLShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		constexpr std::uint32_t MaxUniformName = 128;

		std::uint32_t importedCount = 0;
		for (const GLUniform& uniform : shaderProgram_->uniforms_) {
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
				uniformCaches_[uniformName] = uniformCache;
				importedCount++;
			}
		}

		if (importedCount > UniformCachesHashSize) {
			LOGW("More imported uniform blocks ({}) than hashmap buckets ({})", importedCount, UniformCachesHashSize);
		}
	}
}
