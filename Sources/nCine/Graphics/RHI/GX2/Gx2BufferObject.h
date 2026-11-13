#pragma once

#include "Gx2Memory.h"
#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::GX2
{
	/**
		@brief Buffer object of the GX2 backend (aliased as `RHI::Buffer`)

		A vertex, index or uniform buffer keeps a resizable byte store in MEM2 (see @ref Gx2Memory) that the
		pipeline maps and writes into, and that the GPU reads in place - there is no upload step. The CPU's
		data cache is not coherent with the GPU, so every write path ends in a flush of the written range:
		@ref BufferData() and @ref BufferSubData() flush what they copied, @ref FlushMappedBufferRange()
		flushes the range it is given and @ref Unmap() flushes the range the last @ref MapBufferRange()
		handed out.

		The store is grow-only: a @ref BufferData() that fits the existing block reuses it, so the pipeline's
		per-frame ring buffers allocate once.

		GX2 does not rename buffers, so a CPU write must not land in bytes a queued draw still reads. A draw
		stamps every vertex and index buffer it reads with its batch token (@ref MarkUsed()), and a write to a
		buffer whose token has not retired either moves the store to a fresh block (when the write replaces all of
		it: an orphaning @ref BufferData(), a whole-store @ref BufferSubData() or a map that may discard the
		contents; the old block is retired) or waits for the token. A persistent mapping is never synchronised
		here: RenderBuffersManager's section ring fences it. Uniform buffers are never stamped, because the device
		copies their ranges into its own uniform ring at draw time and the GPU never reads them.
	*/
	class Gx2BufferObject
	{
	public:
		explicit Gx2BufferObject(BufferTarget target);
		~Gx2BufferObject();

		Gx2BufferObject(const Gx2BufferObject&) = delete;
		Gx2BufferObject& operator=(const Gx2BufferObject&) = delete;

		/** @brief Returns a synthetic handle uniquely identifying the buffer (used by material sort keys) */
		inline std::uint32_t GetGLHandle() const {
			return _handle;
		}
		/** @brief Returns the binding target of the buffer */
		inline BufferTarget GetTarget() const {
			return _target;
		}
		/** @brief Returns the size in bytes of the data store */
		inline std::size_t GetSize() const {
			return _size;
		}
		/** @brief Returns the base pointer of the data store */
		inline std::uint8_t* HostData() {
			return _data;
		}
		/** @brief Returns the base pointer of the data store */
		inline const std::uint8_t* HostData() const {
			return _data;
		}
		/**
			@brief Returns the GPU-visible base address of the store, or `nullptr` if it could not be allocated

			The same pointer @ref HostData() returns, only named for the side that consumes it.
		*/
		inline const void* GetGpuData() const {
			return (_gpuBlock.IsValid() ? _data : nullptr);
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
		/** @brief Flushes a mapped range out of the CPU cache so the GPU sees it */
		void FlushMappedBufferRange(std::size_t offset, std::size_t length);
		/** @brief Unmaps the buffer, flushing the range the last @ref MapBufferRange() returned */
		bool Unmap();

		/** @brief Sets a debug label for the buffer (ignored) */
		void SetObjectLabel(StringView label);

		/** @brief Records that a draw of the batch with @p token reads the store (see @ref Gx2Device::GetUseToken()) */
		inline void MarkUsed(std::uint32_t token) const {
			_lastUse = token;
		}

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
		static std::uint32_t _nextHandle;

		std::uint32_t _handle;
		BufferTarget _target;

		// Base and size of the store, whichever of the two backing kinds below provides it
		std::uint8_t* _data;
		std::size_t _size;

		// GPU-visible backing of the store
		Gx2Memory::Block _gpuBlock;

		std::size_t _mappedOffset;
		std::size_t _mappedLength;
		// Token of the last batch with a draw that reads the store, 0 if none
		mutable std::uint32_t _lastUse;

		/** @brief Flushes a written range of the store so the GPU sees it */
		void FlushRange(std::size_t offset, std::size_t length);
		/** @brief (Re)allocates the store to at least @p size bytes, keeping an already large enough one; returns `true` for a new (zeroed) block */
		bool Reserve(std::size_t size);
		/**
			@brief Makes the store safe to write while queued draws may still read it (moves it or waits)
			@param discardsAll Whether the previous contents of the whole store may be dropped
		*/
		void PrepareCpuWrite(bool discardsAll);
	};
}
