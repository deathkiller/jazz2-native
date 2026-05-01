#if defined(WITH_RHI_GL)

#include "ShaderUniformBlocks.h"
#include "ShaderProgram.h"
#include "../../RenderBuffersManager.h"
#include "../../RenderResources.h"
#include "../../../ServiceLocator.h"
#include "../../../../Main.h"

#include <cstring> // for memcpy()

namespace nCine::RHI
{
	ShaderUniformBlocks::ShaderUniformBlocks()
		: shaderProgram_(nullptr), dataPointer_(nullptr)
	{
	}

	ShaderUniformBlocks::ShaderUniformBlocks(ShaderProgram* shaderProgram)
		: ShaderUniformBlocks(shaderProgram, nullptr, nullptr)
	{
	}

	ShaderUniformBlocks::ShaderUniformBlocks(ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: ShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void ShaderUniformBlocks::Bind()
	{
#if defined(DEATH_DEBUG)
		static const std::int32_t offsetAlignment = theServiceLocator().GetGfxCapabilities().GetValue(IGfxCapabilities::IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT);
#endif
		if (uboParams_.object) {
			uboParams_.object->Bind();

			GLintptr moreOffset = 0;
			for (UniformBlockCache& uniformBlockCache : uniformBlockCaches_) {
				uniformBlockCache.SetBlockBinding(uniformBlockCache.GetIndex());
				const GLintptr offset = static_cast<GLintptr>(uboParams_.offset) + moreOffset;
#if defined(DEATH_DEBUG)
				DEATH_DEBUG_ASSERT(offset % offsetAlignment == 0);
#endif
				uboParams_.object->BindBufferRange(uniformBlockCache.GetBindingIndex(), offset, uniformBlockCache.usedSize());
				moreOffset += uniformBlockCache.usedSize();
			}
		}
	}

	void ShaderUniformBlocks::SetProgram(ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		DEATH_ASSERT(shaderProgram);

		shaderProgram_ = shaderProgram;
		shaderProgram_->ProcessDeferredQueries();
		uniformBlockCaches_.clear();

		if (shaderProgram->GetStatus() == ShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void ShaderUniformBlocks::SetUniformsDataPointer(GLubyte* dataPointer)
	{
		DEATH_ASSERT(dataPointer);

		if (shaderProgram_->GetStatus() != ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		dataPointer_ = dataPointer;
		std::int32_t offset = 0;
		for (UniformBlockCache& uniformBlockCache : uniformBlockCaches_) {
			uniformBlockCache.SetDataPointer(dataPointer + offset);
			offset += uniformBlockCache.uniformBlock()->GetSize() - uniformBlockCache.uniformBlock()->GetAlignAmount();
		}
	}

	UniformBlockCache* ShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		DEATH_ASSERT(name);
		UniformBlockCache* uniformBlockCache = nullptr;

		if (shaderProgram_ != nullptr) {
			uniformBlockCache = uniformBlockCaches_.find(String::nullTerminatedView(name));
		} else {
			LOGE("Cannot find uniform block \"{}\", no shader program associated", name);
		}
		return uniformBlockCache;
	}

	void ShaderUniformBlocks::CommitUniformBlocks()
	{
		if (shaderProgram_ != nullptr) {
			if (shaderProgram_->GetStatus() == ShaderProgram::Status::LinkedWithIntrospection) {
				std::int32_t totalUsedSize = 0;
				bool hasMemoryGaps = false;
				for (UniformBlockCache& uniformBlockCache : uniformBlockCaches_) {
					// There is a gap if at least one block cache (not in last position) uses less memory than its size
					if (uniformBlockCache.GetDataPointer() != dataPointer_ + totalUsedSize) {
						hasMemoryGaps = true;
					}
					totalUsedSize += uniformBlockCache.usedSize();
				}

				if (totalUsedSize > 0) {
					const RenderBuffersManager::BufferTypes bufferType = RenderBuffersManager::BufferTypes::Uniform;
					uboParams_ = RenderResources::GetBuffersManager().AcquireMemory(bufferType, totalUsedSize);
					if (uboParams_.mapBase != nullptr) {
						if (hasMemoryGaps) {
							std::int32_t offset = 0;
							for (UniformBlockCache& uniformBlockCache : uniformBlockCaches_) {
								std::memcpy(uboParams_.mapBase + uboParams_.offset + offset, uniformBlockCache.GetDataPointer(), uniformBlockCache.usedSize());
								offset += uniformBlockCache.usedSize();
							}
						} else {
							std::memcpy(uboParams_.mapBase + uboParams_.offset, dataPointer_, totalUsedSize);
						}
					}
				}
			}
		} else {
			LOGE("No shader program associated");
		}
	}

	void ShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		const std::uint32_t MaxUniformBlockName = 128;

		std::uint32_t importedCount = 0;
		for (UniformBlock& uniformBlock : shaderProgram_->uniformBlocks_) {
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
				uniformBlockCaches_.emplace(uniformBlockName, &uniformBlock);
				importedCount++;
			}
		}

		if (importedCount > UniformBlockCachesHashSize) {
			LOGW("More imported uniform blocks ({}) than hashmap buckets ({})", importedCount, UniformBlockCachesHashSize);
		}
	}
}

#endif // defined(WITH_RHI_GL)
