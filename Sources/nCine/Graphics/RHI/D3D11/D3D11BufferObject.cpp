#include "D3D11BufferObject.h"
#include "D3D11Device.h"

#include <cstring>

#include <d3d11.h>

namespace nCine::RhiD3D11
{
	std::uint32_t D3D11BufferObject::nextHandle_ = 1;

	D3D11BufferObject::D3D11BufferObject(BufferTarget target)
		: handle_(nextHandle_++), target_(target), gpuBuffer_(nullptr), gpuBufferCapacity_(0), gpuDirty_(true)
	{
	}

	D3D11BufferObject::~D3D11BufferObject()
	{
		if (gpuBuffer_ != nullptr) {
			gpuBuffer_->Release();
			gpuBuffer_ = nullptr;
		}
	}

	ID3D11Buffer* D3D11BufferObject::GetD3DBuffer() const
	{
		ID3D11Device* device = D3D11Device::GetD3DDevice();
		ID3D11DeviceContext* context = D3D11Device::GetD3DContext();
		if (device == nullptr || context == nullptr || storage_.empty()) {
			return nullptr;
		}

		// (Re)create the buffer if missing or too small (grow-only), then refresh its contents from the store
		if (gpuBuffer_ == nullptr || gpuBufferCapacity_ < storage_.size()) {
			if (gpuBuffer_ != nullptr) {
				gpuBuffer_->Release();
				gpuBuffer_ = nullptr;
			}
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = static_cast<UINT>((storage_.size() + 15u) & ~std::size_t(15u));
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = (target_ == BufferTarget::Index) ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(device->CreateBuffer(&desc, nullptr, &gpuBuffer_))) {
				gpuBuffer_ = nullptr;
				return nullptr;
			}
			gpuBufferCapacity_ = desc.ByteWidth;
			gpuDirty_ = true;
		}

		if (gpuDirty_) {
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(context->Map(gpuBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				std::memcpy(mapped.pData, storage_.data(), storage_.size());
				context->Unmap(gpuBuffer_, 0);
			}
			gpuDirty_ = false;
		}
		return gpuBuffer_;
	}

	bool D3D11BufferObject::Bind() const
	{
		// Slice 2a does not consult a bound-buffer cache; the ranges are forwarded by BindBufferRange(),
		// so this only reports that a bind "was issued"
		return true;
	}

	bool D3D11BufferObject::Unbind() const
	{
		return true;
	}

	void D3D11BufferObject::BufferData(std::size_t size, const void* data, BufferUsage usage)
	{
		static_cast<void>(usage);
		storage_.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(storage_.data(), data, size);
		}
		gpuDirty_ = true;
	}

	void D3D11BufferObject::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || size == 0 || offset + size > storage_.size()) {
			return;
		}
		std::memcpy(storage_.data() + offset, data, size);
		gpuDirty_ = true;
	}

	void D3D11BufferObject::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		// The host store is a plain resizable buffer, so "immutable storage" is just a (re)allocation;
		// the storage/mapping flags do not apply
		static_cast<void>(flags);
		storage_.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(storage_.data(), data, size);
		}
		gpuDirty_ = true;
	}

	void D3D11BufferObject::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, storage_.size());
	}

	void D3D11BufferObject::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (offset > storage_.size()) {
			return;
		}
		if (offset + size > storage_.size()) {
			size = storage_.size() - offset;
		}
		D3D11Device::BindUniformRange(index, storage_.data() + offset, std::uint32_t(size));
	}

	void* D3D11BufferObject::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(length);
		static_cast<void>(access);
		if (offset > storage_.size()) {
			return nullptr;
		}
		return storage_.data() + offset;
	}

	void D3D11BufferObject::FlushMappedBufferRange(std::size_t offset, std::size_t length)
	{
		static_cast<void>(offset);
		static_cast<void>(length);
		gpuDirty_ = true;
	}

	bool D3D11BufferObject::Unmap()
	{
		gpuDirty_ = true;
		return true;
	}

	void D3D11BufferObject::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
