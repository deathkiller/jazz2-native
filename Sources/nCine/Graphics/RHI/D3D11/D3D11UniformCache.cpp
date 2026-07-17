#include "D3D11UniformCache.h"
#include "D3D11ShaderProgram.h"

#include <cstring>

namespace nCine::RhiD3D11
{
	const float* D3D11UniformCache::GetFloatVector() const
	{
		return (dataPointer_ != nullptr ? reinterpret_cast<const float*>(dataPointer_) : nullptr);
	}

	float D3D11UniformCache::GetFloatValue(std::uint32_t index) const
	{
		return (dataPointer_ != nullptr ? reinterpret_cast<const float*>(dataPointer_)[index] : 0.0f);
	}

	const std::int32_t* D3D11UniformCache::GetIntVector() const
	{
		return (dataPointer_ != nullptr ? reinterpret_cast<const std::int32_t*>(dataPointer_) : nullptr);
	}

	std::int32_t D3D11UniformCache::GetIntValue(std::uint32_t index) const
	{
		return (dataPointer_ != nullptr ? reinterpret_cast<const std::int32_t*>(dataPointer_)[index] : 0);
	}

	bool D3D11UniformCache::SetFloatVector(const float* vec)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckFloat()) {
			return false;
		}
		isDirty_ = true;
		std::memcpy(dataPointer_, vec, sizeof(float) * uniform_->GetComponentCount());
		return true;
	}

	bool D3D11UniformCache::SetFloatValue(float v0)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckFloat() || !CheckComponents(1)) {
			return false;
		}
		isDirty_ = true;
		reinterpret_cast<float*>(dataPointer_)[0] = v0;
		return true;
	}

	bool D3D11UniformCache::SetFloatValue(float v0, float v1)
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

	bool D3D11UniformCache::SetFloatValue(float v0, float v1, float v2)
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

	bool D3D11UniformCache::SetFloatValue(float v0, float v1, float v2, float v3)
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

	bool D3D11UniformCache::SetIntVector(const std::int32_t* vec)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckInt()) {
			return false;
		}
		isDirty_ = true;
		std::memcpy(dataPointer_, vec, sizeof(std::int32_t) * uniform_->GetComponentCount());
		return true;
	}

	bool D3D11UniformCache::SetIntValue(std::int32_t v0)
	{
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckInt() || !CheckComponents(1)) {
			return false;
		}
		isDirty_ = true;
		reinterpret_cast<std::int32_t*>(dataPointer_)[0] = v0;
		return true;
	}

	bool D3D11UniformCache::SetIntValue(std::int32_t v0, std::int32_t v1)
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

	bool D3D11UniformCache::SetIntValue(std::int32_t v0, std::int32_t v1, std::int32_t v2)
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

	bool D3D11UniformCache::SetIntValue(std::int32_t v0, std::int32_t v1, std::int32_t v2, std::int32_t v3)
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

	bool D3D11UniformCache::CommitValue()
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

	bool D3D11UniformCache::CheckFloat() const
	{
		return (uniform_ != nullptr && uniform_->IsFloat());
	}

	bool D3D11UniformCache::CheckInt() const
	{
		return (uniform_ != nullptr && !uniform_->IsFloat());
	}

	bool D3D11UniformCache::CheckComponents(std::uint32_t requiredComponents) const
	{
		return (uniform_ != nullptr && uniform_->GetComponentCount() == requiredComponents);
	}

	// -------------------------------------------------------------------------------------------------

	D3D11UniformBlockCache::D3D11UniformBlockCache(D3D11UniformBlock* uniformBlock)
		: uniformBlock_(uniformBlock), dataPointer_(nullptr), usedSize_(0)
	{
		usedSize_ = uniformBlock->GetSize();
		uniformCaches_.reserve(uniformBlock->members_.size());
		for (D3D11Uniform& member : uniformBlock->members_) {
			uniformCaches_.push_back({member.GetName(), D3D11UniformCache(&member)});
		}
	}

	std::uint32_t D3D11UniformBlockCache::GetIndex() const
	{
		return (uniformBlock_ != nullptr ? uniformBlock_->GetIndex() : 0);
	}

	std::int32_t D3D11UniformBlockCache::GetBindingIndex() const
	{
		return (uniformBlock_ != nullptr ? uniformBlock_->GetBindingIndex() : 0);
	}

	std::int32_t D3D11UniformBlockCache::GetSize() const
	{
		return (uniformBlock_ != nullptr ? uniformBlock_->GetSize() : 0);
	}

	std::uint8_t D3D11UniformBlockCache::GetAlignAmount() const
	{
		return (uniformBlock_ != nullptr ? uniformBlock_->GetAlignAmount() : 0);
	}

	void D3D11UniformBlockCache::SetDataPointer(std::uint8_t* dataPointer)
	{
		dataPointer_ = dataPointer;
		for (NamedCache& named : uniformCaches_) {
			named.Cache.SetDataPointer(dataPointer_ + named.Cache.GetUniform()->GetOffset());
		}
	}

	void D3D11UniformBlockCache::SetUsedSize(std::int32_t usedSize)
	{
		if (usedSize >= 0) {
			usedSize_ = usedSize;
		}
	}

	bool D3D11UniformBlockCache::CopyData(std::uint32_t destIndex, const std::uint8_t* src, std::uint32_t numBytes)
	{
		if (destIndex + numBytes > std::uint32_t(GetSize()) || numBytes == 0 || src == nullptr || dataPointer_ == nullptr) {
			return false;
		}
		std::memcpy(&dataPointer_[destIndex], src, numBytes);
		return true;
	}

	D3D11UniformCache* D3D11UniformBlockCache::GetUniform(StringView name)
	{
		for (NamedCache& named : uniformCaches_) {
			if (name == named.Name) {
				return &named.Cache;
			}
		}
		return nullptr;
	}

	void D3D11UniformBlockCache::SetBlockBinding(std::int32_t blockBinding)
	{
		if (uniformBlock_ != nullptr) {
			uniformBlock_->SetBlockBinding(blockBinding);
		}
	}
}
