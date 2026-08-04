#include "D3D11BufferObject.h"
#include "D3D11Device.h"

#include <cstring>

#include <d3d11.h>

namespace nCine::RHI::D3D11
{
	std::uint32_t D3D11BufferObject::_nextHandle = 1;

	D3D11BufferObject::D3D11BufferObject(BufferTarget target)
		: _handle(_nextHandle++), _target(target), _gpuBuffer(nullptr), _gpuBufferCapacity(0), _gpuDirty(true)
	{
	}

	D3D11BufferObject::~D3D11BufferObject()
	{
		if (_gpuBuffer != nullptr) {
			_gpuBuffer->Release();
			_gpuBuffer = nullptr;
		}
	}

	ID3D11Buffer* D3D11BufferObject::GetD3DBuffer() const
	{
		ID3D11Device* device = D3D11Device::GetD3DDevice();
		ID3D11DeviceContext* context = D3D11Device::GetD3DContext();
		if (device == nullptr || context == nullptr || _storage.empty()) {
			return nullptr;
		}

		// (Re)create the buffer if missing or too small (grow-only), then refresh its contents from the store
		if (_gpuBuffer == nullptr || _gpuBufferCapacity < _storage.size()) {
			if (_gpuBuffer != nullptr) {
				_gpuBuffer->Release();
				_gpuBuffer = nullptr;
			}
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = static_cast<UINT>((_storage.size() + 15u) & ~std::size_t(15u));
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = (_target == BufferTarget::Index) ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(device->CreateBuffer(&desc, nullptr, &_gpuBuffer))) {
				_gpuBuffer = nullptr;
				return nullptr;
			}
			_gpuBufferCapacity = desc.ByteWidth;
			_gpuDirty = true;
		}

		if (_gpuDirty) {
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(context->Map(_gpuBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				std::memcpy(mapped.pData, _storage.data(), _storage.size());
				context->Unmap(_gpuBuffer, 0);
			}
			_gpuDirty = false;
		}
		return _gpuBuffer;
	}

	bool D3D11BufferObject::Bind() const
	{
		// No bound-buffer cache is consulted; the ranges are forwarded by BindBufferRange(),
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
		_storage.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(_storage.data(), data, size);
		}
		_gpuDirty = true;
	}

	void D3D11BufferObject::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || size == 0 || offset + size > _storage.size()) {
			return;
		}
		std::memcpy(_storage.data() + offset, data, size);
		_gpuDirty = true;
	}

	void D3D11BufferObject::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		// The host store is a plain resizable buffer, so "immutable storage" is just a (re)allocation;
		// the storage/mapping flags do not apply
		static_cast<void>(flags);
		_storage.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(_storage.data(), data, size);
		}
		_gpuDirty = true;
	}

	void D3D11BufferObject::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, _storage.size());
	}

	void D3D11BufferObject::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (offset > _storage.size()) {
			return;
		}
		if (offset + size > _storage.size()) {
			size = _storage.size() - offset;
		}
		D3D11Device::BindUniformRange(index, _storage.data() + offset, std::uint32_t(size));
	}

	void* D3D11BufferObject::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(length);
		static_cast<void>(access);
		if (offset > _storage.size()) {
			return nullptr;
		}
		return _storage.data() + offset;
	}

	void D3D11BufferObject::FlushMappedBufferRange(std::size_t offset, std::size_t length)
	{
		static_cast<void>(offset);
		static_cast<void>(length);
		_gpuDirty = true;
	}

	bool D3D11BufferObject::Unmap()
	{
		_gpuDirty = true;
		return true;
	}

	void D3D11BufferObject::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
