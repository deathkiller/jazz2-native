#include "SwShaderUniforms.h"
#include "SwShaderProgram.h"
#include "SwBuffer.h"

#include <cstring>

namespace nCine::RHI::Software
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

	SwShaderUniforms::SwShaderUniforms()
		: shaderProgram_(nullptr), maybeDirty_(true)
	{
	}

	SwShaderUniforms::SwShaderUniforms(SwShaderProgram* shaderProgram)
		: SwShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	SwShaderUniforms::SwShaderUniforms(SwShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: SwShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void SwShaderUniforms::SetProgram(SwShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		shaderProgram_ = shaderProgram;
		uniformCaches_.clear();
		maybeDirty_ = true;

		if (shaderProgram_->GetStatus() == SwShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void SwShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != SwShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		maybeDirty_ = true;
		std::uint32_t offset = 0;
		for (SwUniformCache& cache : uniformCaches_) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void SwShaderUniforms::SetDirty(bool isDirty)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != SwShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		maybeDirty_ = isDirty;
		for (SwUniformCache& cache : uniformCaches_) {
			cache.SetDirty(isDirty);
		}
	}

	bool SwShaderUniforms::HasUniform(const char* name) const
	{
		for (const SwUniformCache& cache : uniformCaches_) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	SwUniformCache* SwShaderUniforms::GetUniform(const char* name)
	{
		for (SwUniformCache& cache : uniformCaches_) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				maybeDirty_ = true;
				return &cache;
			}
		}
		return nullptr;
	}

	void SwShaderUniforms::CommitUniforms()
	{
		if (shaderProgram_ == nullptr) {
			return;
		}
		if (maybeDirty_ && shaderProgram_->GetStatus() == SwShaderProgram::Status::LinkedWithIntrospection) {
			shaderProgram_->Use();
			for (SwUniformCache& cache : uniformCaches_) {
				cache.CommitValue();
			}
			maybeDirty_ = false;
		}
	}

	void SwShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const SwUniform& uniform : shaderProgram_->uniforms_) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				uniformCaches_.push_back(SwUniformCache(&uniform));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	SwShaderUniformBlocks::UniformRangeAllocator SwShaderUniformBlocks::uniformRangeAllocator_ = nullptr;

	void SwShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		uniformRangeAllocator_ = allocator;
	}

	SwShaderUniformBlocks::SwShaderUniformBlocks()
		: shaderProgram_(nullptr), dataPointer_(nullptr)
	{
	}

	SwShaderUniformBlocks::SwShaderUniformBlocks(SwShaderProgram* shaderProgram)
		: SwShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	SwShaderUniformBlocks::SwShaderUniformBlocks(SwShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: SwShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void SwShaderUniformBlocks::SetProgram(SwShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		shaderProgram_ = shaderProgram;
		uniformBlockCaches_.clear();

		if (shaderProgram_->GetStatus() == SwShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void SwShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != SwShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		dataPointer_ = dataPointer;
		std::int32_t offset = 0;
		for (SwUniformBlockCache& cache : uniformBlockCaches_) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool SwShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const SwUniformBlockCache& cache : uniformBlockCaches_) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	SwUniformBlockCache* SwShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (SwUniformBlockCache& cache : uniformBlockCaches_) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void SwShaderUniformBlocks::CommitUniformBlocks()
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != SwShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (SwUniformBlockCache& cache : uniformBlockCaches_) {
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
					for (SwUniformBlockCache& cache : uniformBlockCaches_) {
						std::memcpy(uboParams_.mapBase + uboParams_.offset + offset, cache.GetDataPointer(), cache.usedSize());
						offset += cache.usedSize();
					}
				} else {
					std::memcpy(uboParams_.mapBase + uboParams_.offset, dataPointer_, totalUsedSize);
				}
			}
		}
	}

	void SwShaderUniformBlocks::Bind()
	{
		if (uboParams_.object == nullptr) {
			return;
		}

		uboParams_.object->Bind();
		std::size_t moreOffset = 0;
		for (SwUniformBlockCache& cache : uniformBlockCaches_) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(uboParams_.offset) + moreOffset;
			uboParams_.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.usedSize()));
			moreOffset += std::size_t(cache.usedSize());
		}
	}

	void SwShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (SwUniformBlock& block : shaderProgram_->uniformBlocks_) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				uniformBlockCaches_.push_back(SwUniformBlockCache(&block));
			}
		}
	}
}
