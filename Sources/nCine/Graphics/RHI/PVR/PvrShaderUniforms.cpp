#include "PvrShaderUniforms.h"
#include "PvrShaderProgram.h"
#include "PvrBuffer.h"

#include <cstring>

namespace nCine::RHI::PVR
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

	PvrShaderUniforms::PvrShaderUniforms()
		: shaderProgram_(nullptr), maybeDirty_(true)
	{
	}

	PvrShaderUniforms::PvrShaderUniforms(PvrShaderProgram* shaderProgram)
		: PvrShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	PvrShaderUniforms::PvrShaderUniforms(PvrShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: PvrShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void PvrShaderUniforms::SetProgram(PvrShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		shaderProgram_ = shaderProgram;
		uniformCaches_.clear();
		maybeDirty_ = true;

		if (shaderProgram_->GetStatus() == PvrShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void PvrShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != PvrShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		maybeDirty_ = true;
		std::uint32_t offset = 0;
		for (PvrUniformCache& cache : uniformCaches_) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void PvrShaderUniforms::SetDirty(bool isDirty)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != PvrShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		maybeDirty_ = isDirty;
		for (PvrUniformCache& cache : uniformCaches_) {
			cache.SetDirty(isDirty);
		}
	}

	bool PvrShaderUniforms::HasUniform(const char* name) const
	{
		for (const PvrUniformCache& cache : uniformCaches_) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	PvrUniformCache* PvrShaderUniforms::GetUniform(const char* name)
	{
		for (PvrUniformCache& cache : uniformCaches_) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				maybeDirty_ = true;
				return &cache;
			}
		}
		return nullptr;
	}

	void PvrShaderUniforms::CommitUniforms()
	{
		if (shaderProgram_ == nullptr) {
			return;
		}
		if (maybeDirty_ && shaderProgram_->GetStatus() == PvrShaderProgram::Status::LinkedWithIntrospection) {
			shaderProgram_->Use();
			for (PvrUniformCache& cache : uniformCaches_) {
				cache.CommitValue();
			}
			maybeDirty_ = false;
		}
	}

	void PvrShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const PvrUniform& uniform : shaderProgram_->uniforms_) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				uniformCaches_.push_back(PvrUniformCache(&uniform));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	PvrShaderUniformBlocks::UniformRangeAllocator PvrShaderUniformBlocks::uniformRangeAllocator_ = nullptr;

	void PvrShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		uniformRangeAllocator_ = allocator;
	}

	PvrShaderUniformBlocks::PvrShaderUniformBlocks()
		: shaderProgram_(nullptr), dataPointer_(nullptr)
	{
	}

	PvrShaderUniformBlocks::PvrShaderUniformBlocks(PvrShaderProgram* shaderProgram)
		: PvrShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	PvrShaderUniformBlocks::PvrShaderUniformBlocks(PvrShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: PvrShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void PvrShaderUniformBlocks::SetProgram(PvrShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		shaderProgram_ = shaderProgram;
		uniformBlockCaches_.clear();

		if (shaderProgram_->GetStatus() == PvrShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void PvrShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != PvrShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		dataPointer_ = dataPointer;
		std::int32_t offset = 0;
		for (PvrUniformBlockCache& cache : uniformBlockCaches_) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool PvrShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const PvrUniformBlockCache& cache : uniformBlockCaches_) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	PvrUniformBlockCache* PvrShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (PvrUniformBlockCache& cache : uniformBlockCaches_) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void PvrShaderUniformBlocks::CommitUniformBlocks()
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != PvrShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (PvrUniformBlockCache& cache : uniformBlockCaches_) {
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
					for (PvrUniformBlockCache& cache : uniformBlockCaches_) {
						std::memcpy(uboParams_.mapBase + uboParams_.offset + offset, cache.GetDataPointer(), cache.usedSize());
						offset += cache.usedSize();
					}
				} else {
					std::memcpy(uboParams_.mapBase + uboParams_.offset, dataPointer_, totalUsedSize);
				}
			}
		}
	}

	void PvrShaderUniformBlocks::Bind()
	{
		if (uboParams_.object == nullptr) {
			return;
		}

		uboParams_.object->Bind();
		std::size_t moreOffset = 0;
		for (PvrUniformBlockCache& cache : uniformBlockCaches_) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(uboParams_.offset) + moreOffset;
			uboParams_.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.usedSize()));
			moreOffset += std::size_t(cache.usedSize());
		}
	}

	void PvrShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (PvrUniformBlock& block : shaderProgram_->uniformBlocks_) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				uniformBlockCaches_.push_back(PvrUniformBlockCache(&block));
			}
		}
	}
}
