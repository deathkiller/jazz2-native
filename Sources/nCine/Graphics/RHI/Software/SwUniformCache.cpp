#include "SwUniformCache.h"
#include "SwShaderProgram.h"

#include <cstring>

namespace nCine::RHI::Software
{
	const float* SwUniformCache::GetFloatVector() const
	{
		return (_dataPointer != nullptr ? reinterpret_cast<const float*>(_dataPointer) : nullptr);
	}

	float SwUniformCache::GetFloatValue(std::uint32_t index) const
	{
		return (_dataPointer != nullptr ? reinterpret_cast<const float*>(_dataPointer)[index] : 0.0f);
	}

	const std::int32_t* SwUniformCache::GetIntVector() const
	{
		return (_dataPointer != nullptr ? reinterpret_cast<const std::int32_t*>(_dataPointer) : nullptr);
	}

	std::int32_t SwUniformCache::GetIntValue(std::uint32_t index) const
	{
		return (_dataPointer != nullptr ? reinterpret_cast<const std::int32_t*>(_dataPointer)[index] : 0);
	}

	bool SwUniformCache::SetFloatVector(const float* vec)
	{
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckFloat()) {
			return false;
		}
		_isDirty = true;
		std::memcpy(_dataPointer, vec, sizeof(float) * _uniform->GetComponentCount());
		return true;
	}

	bool SwUniformCache::SetFloatValue(float v0)
	{
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckFloat() || !CheckComponents(1)) {
			return false;
		}
		_isDirty = true;
		reinterpret_cast<float*>(_dataPointer)[0] = v0;
		return true;
	}

	bool SwUniformCache::SetFloatValue(float v0, float v1)
	{
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckFloat() || !CheckComponents(2)) {
			return false;
		}
		_isDirty = true;
		float* data = reinterpret_cast<float*>(_dataPointer);
		data[0] = v0;
		data[1] = v1;
		return true;
	}

	bool SwUniformCache::SetFloatValue(float v0, float v1, float v2)
	{
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckFloat() || !CheckComponents(3)) {
			return false;
		}
		_isDirty = true;
		float* data = reinterpret_cast<float*>(_dataPointer);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		return true;
	}

	bool SwUniformCache::SetFloatValue(float v0, float v1, float v2, float v3)
	{
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckFloat() || !CheckComponents(4)) {
			return false;
		}
		_isDirty = true;
		float* data = reinterpret_cast<float*>(_dataPointer);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		data[3] = v3;
		return true;
	}

	bool SwUniformCache::SetIntVector(const std::int32_t* vec)
	{
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckInt()) {
			return false;
		}
		_isDirty = true;
		std::memcpy(_dataPointer, vec, sizeof(std::int32_t) * _uniform->GetComponentCount());
		return true;
	}

	bool SwUniformCache::SetIntValue(std::int32_t v0)
	{
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckInt() || !CheckComponents(1)) {
			return false;
		}
		_isDirty = true;
		reinterpret_cast<std::int32_t*>(_dataPointer)[0] = v0;
		return true;
	}

	bool SwUniformCache::SetIntValue(std::int32_t v0, std::int32_t v1)
	{
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckInt() || !CheckComponents(2)) {
			return false;
		}
		_isDirty = true;
		std::int32_t* data = reinterpret_cast<std::int32_t*>(_dataPointer);
		data[0] = v0;
		data[1] = v1;
		return true;
	}

	bool SwUniformCache::SetIntValue(std::int32_t v0, std::int32_t v1, std::int32_t v2)
	{
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckInt() || !CheckComponents(3)) {
			return false;
		}
		_isDirty = true;
		std::int32_t* data = reinterpret_cast<std::int32_t*>(_dataPointer);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		return true;
	}

	bool SwUniformCache::SetIntValue(std::int32_t v0, std::int32_t v1, std::int32_t v2, std::int32_t v3)
	{
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckInt() || !CheckComponents(4)) {
			return false;
		}
		_isDirty = true;
		std::int32_t* data = reinterpret_cast<std::int32_t*>(_dataPointer);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		data[3] = v3;
		return true;
	}

	bool SwUniformCache::CommitValue()
	{
		if (_uniform == nullptr || _dataPointer == nullptr || !_isDirty) {
			return false;
		}

		// A uniform inside a block is uploaded as part of the whole block, not individually
		if (_uniform->GetBlockIndex() == -1 && _uniform->GetOwner() != nullptr) {
			_uniform->GetOwner()->SetResolvedUniform(_uniform->GetName(), _dataPointer);
		}

		_isDirty = false;
		return true;
	}

	bool SwUniformCache::CheckFloat() const
	{
		return (_uniform != nullptr && _uniform->IsFloat());
	}

	bool SwUniformCache::CheckInt() const
	{
		return (_uniform != nullptr && !_uniform->IsFloat());
	}

	bool SwUniformCache::CheckComponents(std::uint32_t requiredComponents) const
	{
		return (_uniform != nullptr && _uniform->GetComponentCount() == requiredComponents);
	}

	// -------------------------------------------------------------------------------------------------

	SwUniformBlockCache::SwUniformBlockCache(SwUniformBlock* uniformBlock)
		: _uniformBlock(uniformBlock), _dataPointer(nullptr), _usedSize(0)
	{
		_usedSize = uniformBlock->GetSize();
		_uniformCaches.reserve(uniformBlock->_members.size());
		for (SwUniform& member : uniformBlock->_members) {
			_uniformCaches.push_back({member.GetName(), SwUniformCache(&member)});
		}
	}

	std::uint32_t SwUniformBlockCache::GetIndex() const
	{
		return (_uniformBlock != nullptr ? _uniformBlock->GetIndex() : 0);
	}

	std::int32_t SwUniformBlockCache::GetBindingIndex() const
	{
		return (_uniformBlock != nullptr ? _uniformBlock->GetBindingIndex() : 0);
	}

	std::int32_t SwUniformBlockCache::GetSize() const
	{
		return (_uniformBlock != nullptr ? _uniformBlock->GetSize() : 0);
	}

	std::uint8_t SwUniformBlockCache::GetAlignAmount() const
	{
		return (_uniformBlock != nullptr ? _uniformBlock->GetAlignAmount() : 0);
	}

	void SwUniformBlockCache::SetDataPointer(std::uint8_t* dataPointer)
	{
		_dataPointer = dataPointer;
		for (NamedCache& named : _uniformCaches) {
			named.Cache.SetDataPointer(_dataPointer + named.Cache.GetUniform()->GetOffset());
		}
	}

	void SwUniformBlockCache::SetUsedSize(std::int32_t usedSize)
	{
		if (usedSize >= 0) {
			_usedSize = usedSize;
		}
	}

	bool SwUniformBlockCache::CopyData(std::uint32_t destIndex, const std::uint8_t* src, std::uint32_t numBytes)
	{
		if (destIndex + numBytes > std::uint32_t(GetSize()) || numBytes == 0 || src == nullptr || _dataPointer == nullptr) {
			return false;
		}
		std::memcpy(&_dataPointer[destIndex], src, numBytes);
		return true;
	}

	SwUniformCache* SwUniformBlockCache::GetUniform(StringView name)
	{
		for (NamedCache& named : _uniformCaches) {
			if (name == named.Name) {
				return &named.Cache;
			}
		}
		return nullptr;
	}

	void SwUniformBlockCache::SetBlockBinding(std::int32_t blockBinding)
	{
		if (_uniformBlock != nullptr) {
			_uniformBlock->SetBlockBinding(blockBinding);
		}
	}
}
