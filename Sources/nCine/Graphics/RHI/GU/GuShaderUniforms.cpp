#include "GuShaderUniforms.h"
#include "GuShaderProgram.h"
#include "GuBuffer.h"

#include <cstring>

namespace nCine::RHI::GU
{
	namespace
	{
		// Matches a name against the null-separated, double-null-terminated include/exclude lists exactly
		// like the OpenGL backend's uniform importers do
		bool ShouldImport(const char* name, const char* includeOnly, const char* exclude)
		{
			bool shouldImport = true;
			if (includeOnly != nullptr) {
				shouldImport = false;
				const char* current = includeOnly;
				while (current != nullptr && current[0] != '\0') {
					if (std::strcmp(current, name) == 0) {
						shouldImport = true;
						break;
					}
					current += std::strlen(current) + 1;
				}
			}
			if (exclude != nullptr) {
				const char* current = exclude;
				while (current != nullptr && current[0] != '\0') {
					if (std::strcmp(current, name) == 0) {
						shouldImport = false;
						break;
					}
					current += std::strlen(current) + 1;
				}
			}
			return shouldImport;
		}
	}

	GuShaderUniforms::GuShaderUniforms()
		: shaderProgram_(nullptr), maybeDirty_(true)
	{
	}

	GuShaderUniforms::GuShaderUniforms(GuShaderProgram* shaderProgram)
		: GuShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	GuShaderUniforms::GuShaderUniforms(GuShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: GuShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void GuShaderUniforms::SetProgram(GuShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		shaderProgram_ = shaderProgram;
		uniformCaches_.clear();
		maybeDirty_ = true;

		if (shaderProgram_->GetStatus() == GuShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void GuShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != GuShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		maybeDirty_ = true;
		std::uint32_t offset = 0;
		for (GuUniformCache& cache : uniformCaches_) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void GuShaderUniforms::SetDirty(bool isDirty)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != GuShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		maybeDirty_ = isDirty;
		for (GuUniformCache& cache : uniformCaches_) {
			cache.SetDirty(isDirty);
		}
	}

	bool GuShaderUniforms::HasUniform(const char* name) const
	{
		for (const GuUniformCache& cache : uniformCaches_) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	GuUniformCache* GuShaderUniforms::GetUniform(const char* name)
	{
		for (GuUniformCache& cache : uniformCaches_) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				maybeDirty_ = true;
				return &cache;
			}
		}
		return nullptr;
	}

	void GuShaderUniforms::CommitUniforms()
	{
		if (shaderProgram_ == nullptr) {
			return;
		}
		if (maybeDirty_ && shaderProgram_->GetStatus() == GuShaderProgram::Status::LinkedWithIntrospection) {
			shaderProgram_->Use();
			for (GuUniformCache& cache : uniformCaches_) {
				cache.CommitValue();
			}
			maybeDirty_ = false;
		}
	}

	void GuShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const GuUniform& uniform : shaderProgram_->uniforms_) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				uniformCaches_.push_back(GuUniformCache(&uniform));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	GuShaderUniformBlocks::UniformRangeAllocator GuShaderUniformBlocks::uniformRangeAllocator_ = nullptr;

	void GuShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		uniformRangeAllocator_ = allocator;
	}

	GuShaderUniformBlocks::GuShaderUniformBlocks()
		: shaderProgram_(nullptr), dataPointer_(nullptr)
	{
	}

	GuShaderUniformBlocks::GuShaderUniformBlocks(GuShaderProgram* shaderProgram)
		: GuShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	GuShaderUniformBlocks::GuShaderUniformBlocks(GuShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: GuShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void GuShaderUniformBlocks::SetProgram(GuShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		shaderProgram_ = shaderProgram;
		uniformBlockCaches_.clear();

		if (shaderProgram_->GetStatus() == GuShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void GuShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != GuShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		dataPointer_ = dataPointer;
		std::int32_t offset = 0;
		for (GuUniformBlockCache& cache : uniformBlockCaches_) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool GuShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const GuUniformBlockCache& cache : uniformBlockCaches_) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	GuUniformBlockCache* GuShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (GuUniformBlockCache& cache : uniformBlockCaches_) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void GuShaderUniformBlocks::CommitUniformBlocks()
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != GuShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (GuUniformBlockCache& cache : uniformBlockCaches_) {
			if (cache.GetDataPointer() != dataPointer_ + totalUsedSize) {
				hasMemoryGaps = true;
			}
			totalUsedSize += cache.usedSize();
		}

		if (totalUsedSize > 0 && uniformRangeAllocator_ != nullptr) {
			uboParams_ = uniformRangeAllocator_(std::uint32_t(totalUsedSize));
			if (uboParams_.mapBase != nullptr) {
				if (hasMemoryGaps) {
					std::int32_t offset = 0;
					for (GuUniformBlockCache& cache : uniformBlockCaches_) {
						std::memcpy(uboParams_.mapBase + uboParams_.offset + offset, cache.GetDataPointer(), cache.usedSize());
						offset += cache.usedSize();
					}
				} else {
					std::memcpy(uboParams_.mapBase + uboParams_.offset, dataPointer_, totalUsedSize);
				}
			}
		}
	}

	void GuShaderUniformBlocks::Bind()
	{
		if (uboParams_.object == nullptr) {
			return;
		}

		uboParams_.object->Bind();
		std::size_t moreOffset = 0;
		for (GuUniformBlockCache& cache : uniformBlockCaches_) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(uboParams_.offset) + moreOffset;
			uboParams_.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.usedSize()));
			moreOffset += std::size_t(cache.usedSize());
		}
	}

	void GuShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (GuUniformBlock& block : shaderProgram_->uniformBlocks_) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				uniformBlockCaches_.push_back(GuUniformBlockCache(&block));
			}
		}
	}
}
