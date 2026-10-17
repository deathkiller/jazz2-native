#include "GLShaderUniformBlocks.h"
#include "GLShaderProgram.h"
#include "GLBufferObject.h"
#include "../IRhiCapabilities.h"
#include "../../../ServiceLocator.h"
#include "../../../../Main.h"

#include <algorithm>
#include <cstring> // for memcpy()

namespace nCine::RHI::GL
{
	namespace
	{
		/** @brief Size of the buffer range a block is bound with */
		inline GLint GetBoundSize(const GLUniformBlockCache& uniformBlockCache)
		{
#if defined(DEATH_TARGET_EMSCRIPTEN)
			// WebGL rejects a draw whose bound range is smaller than the block the program declares, although a
			// batch shorter than its shader's instance array reads only the instances it holds - so the range
			// always spans the whole block there, and the rest of it is whatever follows in the buffer
			const GLint declaredSize = uniformBlockCache.uniformBlock()->GetSize() - uniformBlockCache.uniformBlock()->GetAlignAmount();
			return std::max(uniformBlockCache.usedSize(), declaredSize);
#else
			return uniformBlockCache.usedSize();
#endif
		}
	}

	GLShaderUniformBlocks::UniformRangeAllocator GLShaderUniformBlocks::_uniformRangeAllocator = nullptr;

	void GLShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		_uniformRangeAllocator = allocator;
	}

	GLShaderUniformBlocks::GLShaderUniformBlocks()
		: _shaderProgram(nullptr), _dataPointer(nullptr)
	{
	}

	GLShaderUniformBlocks::GLShaderUniformBlocks(GLShaderProgram* shaderProgram)
		: GLShaderUniformBlocks(shaderProgram, nullptr, nullptr)
	{
	}

	GLShaderUniformBlocks::GLShaderUniformBlocks(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: GLShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void GLShaderUniformBlocks::Bind()
	{
#if defined(RHI_GL_PROFILE_ES2)
		// ES2 has no uniform buffer objects: push each managed block's members to the program's loose
		// uniforms. Material::Bind() has already called Use(), so the program is current for glUniform*.
		for (GLUniformBlockCache& uniformBlockCache : _uniformBlockCaches) {
			uniformBlockCache.CommitAsLooseUniforms();
		}
		// Dead code when the software backend is selected: `RHI::BufferRange::object` is then a `SwBuffer*`
		// whose definition this GL translation unit does not include, and the pipeline never calls this here
#elif !defined(WITH_RHI_SOFTWARE)
#if defined(DEATH_DEBUG)
		static const std::int32_t offsetAlignment = theServiceLocator().GetRhiCapabilities().GetValue(IRhiCapabilities::IntValues::UniformBufferOffsetAlignment);
#endif
		if (_uboParams.object) {
			_uboParams.object->Bind();

			GLintptr moreOffset = 0;
			for (GLUniformBlockCache& uniformBlockCache : _uniformBlockCaches) {
				uniformBlockCache.SetBlockBinding(uniformBlockCache.GetIndex());
				const GLintptr offset = static_cast<GLintptr>(_uboParams.offset) + moreOffset;
#if defined(DEATH_DEBUG)
				DEATH_DEBUG_ASSERT(offset % offsetAlignment == 0);
#endif
				_uboParams.object->BindBufferRange(uniformBlockCache.GetBindingIndex(), offset, GetBoundSize(uniformBlockCache));
				moreOffset += uniformBlockCache.usedSize();
			}
		}
#endif
	}

	void GLShaderUniformBlocks::SetProgram(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		DEATH_ASSERT(shaderProgram);

		_shaderProgram = shaderProgram;
		_shaderProgram->ProcessDeferredQueries();
		_uniformBlockCaches.clear();

		if (shaderProgram->GetStatus() == GLShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void GLShaderUniformBlocks::SetUniformsDataPointer(GLubyte* dataPointer)
	{
		DEATH_ASSERT(dataPointer);

		if (_shaderProgram->GetStatus() != GLShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_dataPointer = dataPointer;
		std::int32_t offset = 0;
		for (GLUniformBlockCache& uniformBlockCache : _uniformBlockCaches) {
			uniformBlockCache.SetDataPointer(dataPointer + offset);
			offset += uniformBlockCache.uniformBlock()->GetSize() - uniformBlockCache.uniformBlock()->GetAlignAmount();
		}
	}

	GLUniformBlockCache* GLShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		DEATH_ASSERT(name);
		GLUniformBlockCache* uniformBlockCache = nullptr;

		if (_shaderProgram != nullptr) {
			uniformBlockCache = _uniformBlockCaches.find(String::nullTerminatedView(name));
		} else {
			LOGE("Cannot find uniform block \"{}\", no shader program associated", name);
		}
		return uniformBlockCache;
	}

	void GLShaderUniformBlocks::CommitUniformBlocks()
	{
#if defined(RHI_GL_PROFILE_ES2)
		// ES2 has no UBOs: block data is pushed to loose uniforms at Bind() time, nothing to commit here
		return;
#else
		if (_shaderProgram != nullptr) {
			if (_shaderProgram->GetStatus() == GLShaderProgram::Status::LinkedWithIntrospection) {
				std::int32_t totalUsedSize = 0;
				// Where Bind() makes a range reach past the data of its block, the allocation has to cover it too
				std::int32_t totalBoundSize = 0;
				bool hasMemoryGaps = false;
				for (GLUniformBlockCache& uniformBlockCache : _uniformBlockCaches) {
					// There is a gap if at least one block cache (not in last position) uses less memory than its size
					if (uniformBlockCache.GetDataPointer() != _dataPointer + totalUsedSize) {
						hasMemoryGaps = true;
					}
					totalBoundSize = std::max(totalBoundSize, totalUsedSize + GetBoundSize(uniformBlockCache));
					totalUsedSize += uniformBlockCache.usedSize();
				}

				if (totalUsedSize > 0) {
					DEATH_ASSERT(_uniformRangeAllocator != nullptr);
					_uboParams = _uniformRangeAllocator(std::uint32_t(totalBoundSize));
					if (_uboParams.mapBase != nullptr) {
						if (hasMemoryGaps) {
							std::int32_t offset = 0;
							for (GLUniformBlockCache& uniformBlockCache : _uniformBlockCaches) {
								std::memcpy(_uboParams.mapBase + _uboParams.offset + offset, uniformBlockCache.GetDataPointer(), uniformBlockCache.usedSize());
								offset += uniformBlockCache.usedSize();
							}
						} else {
							std::memcpy(_uboParams.mapBase + _uboParams.offset, _dataPointer, totalUsedSize);
						}
					}
				}
			}
		} else {
			LOGE("No shader program associated");
		}
#endif
	}

	void GLShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		const std::uint32_t MaxUniformBlockName = 128;

		std::uint32_t importedCount = 0;
		for (GLUniformBlock& uniformBlock : _shaderProgram->_uniformBlocks) {
			const char* uniformBlockName = uniformBlock.GetName();
			const char* currentIncludeOnly = includeOnly;
			const char* currentExclude = exclude;
			bool shouldImport = true;

			if (includeOnly != nullptr) {
				shouldImport = false;
				while (currentIncludeOnly != nullptr && currentIncludeOnly[0] != '\0') {
					if (strncmp(currentIncludeOnly, uniformBlockName, MaxUniformBlockName) == 0) {
						shouldImport = true;
						break;
					}
					currentIncludeOnly += strnlen(currentIncludeOnly, MaxUniformBlockName) + 1;
				}
			}

			if (exclude != nullptr) {
				while (currentExclude != nullptr && currentExclude[0] != '\0') {
					if (strncmp(currentExclude, uniformBlockName, MaxUniformBlockName) == 0) {
						shouldImport = false;
						break;
					}
					currentExclude += strnlen(currentExclude, MaxUniformBlockName) + 1;
				}
			}

			if (shouldImport) {
				_uniformBlockCaches.emplace(uniformBlockName, &uniformBlock);
				importedCount++;
			}
		}

		if (importedCount > UniformBlockCachesHashSize) {
			LOGW("More imported uniform blocks ({}) than hashmap buckets ({})", importedCount, UniformBlockCachesHashSize);
		}
	}
}
