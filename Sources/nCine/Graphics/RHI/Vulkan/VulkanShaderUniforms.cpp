#include "VulkanShaderUniforms.h"
#include "VulkanShaderProgram.h"
#include "VulkanBufferObject.h"

#include <cstring>

namespace nCine::RHI::Vulkan
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

	VulkanShaderUniforms::VulkanShaderUniforms()
		: shaderProgram_(nullptr), maybeDirty_(true)
	{
	}

	VulkanShaderUniforms::VulkanShaderUniforms(VulkanShaderProgram* shaderProgram)
		: VulkanShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	VulkanShaderUniforms::VulkanShaderUniforms(VulkanShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: VulkanShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void VulkanShaderUniforms::SetProgram(VulkanShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		shaderProgram_ = shaderProgram;
		uniformCaches_.clear();
		maybeDirty_ = true;

		if (shaderProgram_->GetStatus() == VulkanShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void VulkanShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != VulkanShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		maybeDirty_ = true;
		std::uint32_t offset = 0;
		for (VulkanUniformCache& cache : uniformCaches_) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void VulkanShaderUniforms::SetDirty(bool isDirty)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != VulkanShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		maybeDirty_ = isDirty;
		for (VulkanUniformCache& cache : uniformCaches_) {
			cache.SetDirty(isDirty);
		}
	}

	bool VulkanShaderUniforms::HasUniform(const char* name) const
	{
		for (const VulkanUniformCache& cache : uniformCaches_) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	VulkanUniformCache* VulkanShaderUniforms::GetUniform(const char* name)
	{
		for (VulkanUniformCache& cache : uniformCaches_) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				maybeDirty_ = true;
				return &cache;
			}
		}
		return nullptr;
	}

	void VulkanShaderUniforms::CommitUniforms()
	{
		if (shaderProgram_ == nullptr) {
			return;
		}
		if (maybeDirty_ && shaderProgram_->GetStatus() == VulkanShaderProgram::Status::LinkedWithIntrospection) {
			shaderProgram_->Use();
			for (VulkanUniformCache& cache : uniformCaches_) {
				cache.CommitValue();
			}
			maybeDirty_ = false;
		}
	}

	void VulkanShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const VulkanUniform& uniform : shaderProgram_->uniforms_) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				uniformCaches_.push_back(VulkanUniformCache(&uniform));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	VulkanShaderUniformBlocks::UniformRangeAllocator VulkanShaderUniformBlocks::uniformRangeAllocator_ = nullptr;

	void VulkanShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		uniformRangeAllocator_ = allocator;
	}

	VulkanShaderUniformBlocks::VulkanShaderUniformBlocks()
		: shaderProgram_(nullptr), dataPointer_(nullptr)
	{
	}

	VulkanShaderUniformBlocks::VulkanShaderUniformBlocks(VulkanShaderProgram* shaderProgram)
		: VulkanShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	VulkanShaderUniformBlocks::VulkanShaderUniformBlocks(VulkanShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: VulkanShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void VulkanShaderUniformBlocks::SetProgram(VulkanShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		shaderProgram_ = shaderProgram;
		uniformBlockCaches_.clear();

		if (shaderProgram_->GetStatus() == VulkanShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void VulkanShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != VulkanShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		dataPointer_ = dataPointer;
		std::int32_t offset = 0;
		for (VulkanUniformBlockCache& cache : uniformBlockCaches_) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool VulkanShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const VulkanUniformBlockCache& cache : uniformBlockCaches_) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	VulkanUniformBlockCache* VulkanShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (VulkanUniformBlockCache& cache : uniformBlockCaches_) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void VulkanShaderUniformBlocks::CommitUniformBlocks()
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != VulkanShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (VulkanUniformBlockCache& cache : uniformBlockCaches_) {
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
					for (VulkanUniformBlockCache& cache : uniformBlockCaches_) {
						std::memcpy(uboParams_.mapBase + uboParams_.offset + offset, cache.GetDataPointer(), cache.usedSize());
						offset += cache.usedSize();
					}
				} else {
					std::memcpy(uboParams_.mapBase + uboParams_.offset, dataPointer_, totalUsedSize);
				}
			}
		}
	}

	void VulkanShaderUniformBlocks::Bind()
	{
		if (uboParams_.object == nullptr) {
			return;
		}

		uboParams_.object->Bind();
		std::size_t moreOffset = 0;
		for (VulkanUniformBlockCache& cache : uniformBlockCaches_) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(uboParams_.offset) + moreOffset;
			uboParams_.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.usedSize()));
			moreOffset += std::size_t(cache.usedSize());
		}
	}

	void VulkanShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (VulkanUniformBlock& block : shaderProgram_->uniformBlocks_) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				uniformBlockCaches_.push_back(VulkanUniformBlockCache(&block));
			}
		}
	}
}
