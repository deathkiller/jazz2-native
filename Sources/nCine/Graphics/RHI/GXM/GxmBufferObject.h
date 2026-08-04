#pragma once

#include "GxmMemory.h"
#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::GXM
{
	/**
		@brief Buffer object of the sceGxm backend (aliased as `RHI::Buffer`)

		A vertex, index or uniform buffer keeps a resizable byte store the pipeline maps and writes into.
		@ref MapBufferRange() hands back a pointer into that store and @ref BindBufferRange() forwards a
		sub-range to the device.

		Every store is GPU-visible memory (@ref GxmMemory), so the pipeline's writes land where the hardware
		can read them with no upload step at all:

		- **Vertex and index** buffers are read by the GPU directly from it.
		- **Uniform** buffers are read either way depending on the shader. A small block is copied into the
		  draw's reserved default uniform buffer, but a batched instance array is far too large for the
		  SGX's uniform registers, so the compiler places it in a *uniform buffer container* and the range is
		  handed to `sceGxmSetVertexUniformBuffer()` by address instead - which needs it to be GPU-visible.

		The store is grow-only: a @ref BufferData() that fits the existing block reuses it, so the pipeline's
		per-frame ring buffers allocate once. The memory is uncached, which suits a write-once-per-frame
		stream; the small per-draw copies read back out of it, which is a handful of bytes.
	*/
	class GxmBufferObject
	{
	public:
		explicit GxmBufferObject(BufferTarget target);
		~GxmBufferObject();

		GxmBufferObject(const GxmBufferObject&) = delete;
		GxmBufferObject& operator=(const GxmBufferObject&) = delete;

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
			return size_;
		}
		/** @brief Returns the base pointer of the data store */
		inline std::uint8_t* HostData() {
			return data_;
		}
		/** @brief Returns the base pointer of the data store */
		inline const std::uint8_t* HostData() const {
			return data_;
		}
		/**
			@brief Returns the GPU-visible base address of the store, or `nullptr` if it could not be allocated

			The same pointer @ref HostData() returns, only named for the side that consumes it.
		*/
		inline const void* GetGpuData() const {
			return (gpuBlock_.IsValid() ? data_ : nullptr);
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

		/** @brief Returns a pointer to a byte range of the store (there is no real mapping) */
		void* MapBufferRange(std::size_t offset, std::size_t length, MapFlags access);
		/** @brief Flushes a mapped range (a no-op, the store is uncached GPU-visible memory) */
		void FlushMappedBufferRange(std::size_t offset, std::size_t length);
		/** @brief Unmaps the buffer (a no-op, the store is directly addressable) */
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

		// Base and size of the store, whichever of the two backing kinds below provides it
		std::uint8_t* data_;
		std::size_t size_;

		// GPU-visible backing of the store
		GxmMemory::Block gpuBlock_;

		/** @brief (Re)allocates the store to at least @p size bytes, keeping an already large enough one */
		void Reserve(std::size_t size);
	};
}
