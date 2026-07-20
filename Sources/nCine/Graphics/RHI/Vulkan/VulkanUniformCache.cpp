#include "VulkanUniformCache.h"
#include "VulkanShaderProgram.h"

#include <cstring>

namespace nCine::RHI::Vulkan
{
	const float* VulkanUniformCache::GetFloatVector() const
	{
		return (dataPointer_ != nullptr ? reinterpret_cast<const float*>(dataPointer_) : nullptr);
	}

	float VulkanUniformCache::GetFloatValue(std::uint32_t index) const
	{
		return (dataPointer_ != nullptr ? reinterpret_cast<const float*>(dataPointer_)[index] : 0.0f);
	}

	const std::int32_t* VulkanUniformCache::GetIntVector() const
	{
		return (dataPointer_ != nullptr ? reinterpret_cast<const std::int32_t*>(dataPointer_) : nullptr);
	}

	std::int32_t VulkanUniformCache::GetIntValue(std::uint32_t index) const
	{
		return (dataPointer_ != nullptr ? reinterpret_cast<const std::int32_t*>(dataPointer_)[index] : 0);
	}

	bool VulkanUniformCache::SetFloatVector(const float* vec)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckFloat()) {
			return false;
		}
		isDirty_ = true;
		std::memcpy(dataPointer_, vec, sizeof(float) * uniform_->GetComponentCount());
		return true;
	}

	bool VulkanUniformCache::SetFloatValue(float v0)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckFloat() || !CheckComponents(1)) {
			return false;
		}
		isDirty_ = true;
		reinterpret_cast<float*>(dataPointer_)[0] = v0;
		return true;
	}

	bool VulkanUniformCache::SetFloatValue(float v0, float v1)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckFloat() || !CheckComponents(2)) {
			return false;
		}
		isDirty_ = true;
		float* data = reinterpret_cast<float*>(dataPointer_);
		data[0] = v0;
		data[1] = v1;
		return true;
	}

	bool VulkanUniformCache::SetFloatValue(float v0, float v1, float v2)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckFloat() || !CheckComponents(3)) {
			return false;
		}
		isDirty_ = true;
		float* data = reinterpret_cast<float*>(dataPointer_);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		return true;
	}

	bool VulkanUniformCache::SetFloatValue(float v0, float v1, float v2, float v3)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckFloat() || !CheckComponents(4)) {
			return false;
		}
		isDirty_ = true;
		float* data = reinterpret_cast<float*>(dataPointer_);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		data[3] = v3;
		return true;
	}

	bool VulkanUniformCache::SetIntVector(const std::int32_t* vec)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckInt()) {
			return false;
		}
		isDirty_ = true;
		std::memcpy(dataPointer_, vec, sizeof(std::int32_t) * uniform_->GetComponentCount());
		return true;
	}

	bool VulkanUniformCache::SetIntValue(std::int32_t v0)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckInt() || !CheckComponents(1)) {
			return false;
		}
		isDirty_ = true;
		reinterpret_cast<std::int32_t*>(dataPointer_)[0] = v0;
		return true;
	}

	bool VulkanUniformCache::SetIntValue(std::int32_t v0, std::int32_t v1)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckInt() || !CheckComponents(2)) {
			return false;
		}
		isDirty_ = true;
		std::int32_t* data = reinterpret_cast<std::int32_t*>(dataPointer_);
		data[0] = v0;
		data[1] = v1;
		return true;
	}

	bool VulkanUniformCache::SetIntValue(std::int32_t v0, std::int32_t v1, std::int32_t v2)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckInt() || !CheckComponents(3)) {
			return false;
		}
		isDirty_ = true;
		std::int32_t* data = reinterpret_cast<std::int32_t*>(dataPointer_);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		return true;
	}

	bool VulkanUniformCache::SetIntValue(std::int32_t v0, std::int32_t v1, std::int32_t v2, std::int32_t v3)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckInt() || !CheckComponents(4)) {
			return false;
		}
		isDirty_ = true;
		std::int32_t* data = reinterpret_cast<std::int32_t*>(dataPointer_);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		data[3] = v3;
		return true;
	}

	bool VulkanUniformCache::CommitValue()
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !isDirty_) {
			return false;
		}

		// A uniform inside a block is uploaded as part of the whole block, not individually
		if (uniform_->GetBlockIndex() == -1 && uniform_->GetOwner() != nullptr) {
			uniform_->GetOwner()->SetResolvedUniform(uniform_->GetName(), dataPointer_);
		}

		isDirty_ = false;
		return true;
	}

	bool VulkanUniformCache::CheckFloat() const
	{
		return (uniform_ != nullptr && uniform_->IsFloat());
	}

	bool VulkanUniformCache::CheckInt() const
	{
		return (uniform_ != nullptr && !uniform_->IsFloat());
	}

	bool VulkanUniformCache::CheckComponents(std::uint32_t requiredComponents) const
	{
		return (uniform_ != nullptr && uniform_->GetComponentCount() == requiredComponents);
	}

	// -------------------------------------------------------------------------------------------------

	VulkanUniformBlockCache::VulkanUniformBlockCache(VulkanUniformBlock* uniformBlock)
		: uniformBlock_(uniformBlock), dataPointer_(nullptr), usedSize_(0)
	{
		usedSize_ = uniformBlock->GetSize();
		uniformCaches_.reserve(uniformBlock->members_.size());
		for (VulkanUniform& member : uniformBlock->members_) {
			uniformCaches_.push_back({member.GetName(), VulkanUniformCache(&member)});
		}
	}

	std::uint32_t VulkanUniformBlockCache::GetIndex() const
	{
		return (uniformBlock_ != nullptr ? uniformBlock_->GetIndex() : 0);
	}

	std::int32_t VulkanUniformBlockCache::GetBindingIndex() const
	{
		return (uniformBlock_ != nullptr ? uniformBlock_->GetBindingIndex() : 0);
	}

	std::int32_t VulkanUniformBlockCache::GetSize() const
	{
		return (uniformBlock_ != nullptr ? uniformBlock_->GetSize() : 0);
	}

	std::uint8_t VulkanUniformBlockCache::GetAlignAmount() const
	{
		return (uniformBlock_ != nullptr ? uniformBlock_->GetAlignAmount() : 0);
	}

	void VulkanUniformBlockCache::SetDataPointer(std::uint8_t* dataPointer)
	{
		dataPointer_ = dataPointer;
		for (NamedCache& named : uniformCaches_) {
			named.Cache.SetDataPointer(dataPointer_ + named.Cache.GetUniform()->GetOffset());
		}
	}

	void VulkanUniformBlockCache::SetUsedSize(std::int32_t usedSize)
	{
		if (usedSize >= 0) {
			usedSize_ = usedSize;
		}
	}

	bool VulkanUniformBlockCache::CopyData(std::uint32_t destIndex, const std::uint8_t* src, std::uint32_t numBytes)
	{
		if (destIndex + numBytes > std::uint32_t(GetSize()) || numBytes == 0 || src == nullptr || dataPointer_ == nullptr) {
			return false;
		}
		std::memcpy(&dataPointer_[destIndex], src, numBytes);
		return true;
	}

	VulkanUniformCache* VulkanUniformBlockCache::GetUniform(StringView name)
	{
		for (NamedCache& named : uniformCaches_) {
			if (name == named.Name) {
				return &named.Cache;
			}
		}
		return nullptr;
	}

	void VulkanUniformBlockCache::SetBlockBinding(std::int32_t blockBinding)
	{
		if (uniformBlock_ != nullptr) {
			uniformBlock_->SetBlockBinding(blockBinding);
		}
	}
}
