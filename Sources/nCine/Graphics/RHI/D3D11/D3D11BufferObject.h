#pragma once

#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Containers/StringView.h>

using namespace Death::Containers;

// Direct3D 11 interface referenced only as an opaque pointer here (definition pulled in by the .cpp)
struct ID3D11Buffer;

namespace nCine::RHI::D3D11
{
	/**
		@brief Buffer object of the Direct3D 11 backend (aliased as `RHI::Buffer`)

		A vertex, index or uniform buffer keeps a resizable host byte store the pipeline maps and writes into.
		@ref MapBufferRange() hands back a pointer into the store and @ref BindBufferRange() forwards a
		sub-range to the device. Real `ID3D11Buffer` objects (with `Map`/`Unmap` for the streaming uniform
		ring) are created from this same surface.
	*/
	class D3D11BufferObject
	{
	public:
		explicit D3D11BufferObject(BufferTarget target);
		~D3D11BufferObject();

		D3D11BufferObject(const D3D11BufferObject&) = delete;
		D3D11BufferObject& operator=(const D3D11BufferObject&) = delete;

		/** @brief Returns a GPU buffer (vertex/index) whose contents are refreshed from the host store when dirty */
		ID3D11Buffer* GetD3DBuffer() const;

		/** @brief Returns a synthetic handle uniquely identifying the buffer (used by material sort keys) */
		inline std::uint32_t GetGLHandle() const {
			return handle_;
		}
		/** @brief Returns the binding target of the buffer */
		inline BufferTarget GetTarget() const {
			return target_;
		}
		/** @brief Returns the size in bytes of the data store */
		inline std::size_t GetSize() const {
			return storage_.size();
		}
		/** @brief Returns the base pointer of the host-side data store */
		inline std::uint8_t* HostData() {
			return storage_.data();
		}
		/** @brief Returns the base pointer of the host-side data store */
		inline const std::uint8_t* HostData() const {
			return storage_.data();
		}

		/** @brief Marks the buffer as the currently bound one for its target (always issues the "bind") */
		bool Bind() const;
		/** @brief Marks no buffer as bound for this object's target */
		bool Unbind() const;

		/** @brief (Re)creates the data store with the given size, optional initial data and usage hint */
		void BufferData(std::size_t size, const void* data, BufferUsage usage);
		/** @brief Updates a subset of the data store starting at the given byte offset */
		void BufferSubData(std::size_t offset, std::size_t size, const void* data);
		/** @brief (Re)creates the data store like @ref BufferData(); there is no immutable storage, so the flags are ignored */
		void BufferStorage(std::size_t size, const void* data, MapFlags flags);

		/** @brief Binds the whole buffer to a uniform binding point index */
		void BindBufferBase(std::uint32_t index);
		/** @brief Binds a byte range of the buffer to a uniform binding point index (forwarded to the device) */
		void BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size);

		/** @brief Returns a host pointer to a byte range of the store (there is no real mapping) */
		void* MapBufferRange(std::size_t offset, std::size_t length, MapFlags access);
		/** @brief Flushes a mapped range (a no-op, the store is host memory) */
		void FlushMappedBufferRange(std::size_t offset, std::size_t length);
		/** @brief Unmaps the buffer (a no-op, the store is host memory) */
		bool Unmap();

		/** @brief Sets a debug label for the buffer (ignored) */
		void SetObjectLabel(StringView label);

		// Static bound-handle helpers used by the shared VAO pool. The OpenGL backend keeps a per-target
		// bound-buffer cache here; this backend has no such state, so these are inert. They mirror the OpenGL
		// backend's static helpers so `RenderVaoPool` (which tracks a VAO's element array buffer) compiles
		// unchanged against the `RHI::Buffer` alias.
		/** @brief Records the buffer handle bound for a target (inert) */
		inline static void SetBoundHandle(std::uint32_t target, std::uint32_t glHandle) {
			static_cast<void>(target);
			static_cast<void>(glHandle);
		}
		/** @brief Marks a buffer handle as bound for a target (inert) */
		inline static bool BindHandle(std::uint32_t target, std::uint32_t glHandle) {
			static_cast<void>(target);
			static_cast<void>(glHandle);
			return true;
		}

	private:
		static std::uint32_t nextHandle_;

		std::uint32_t handle_;
		BufferTarget target_;
		std::vector<std::uint8_t> storage_;

		// GPU buffer mirroring the host store for the vertex/index targets (uniform buffers stay host-only and
		// are forwarded as ranges). Refreshed lazily from the store when @ref gpuDirty_ is set.
		mutable ID3D11Buffer* gpuBuffer_;
		mutable std::size_t gpuBufferCapacity_;
		mutable bool gpuDirty_;
	};
}
