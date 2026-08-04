#pragma once

#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::PVR
{
	/**
		@brief Host-memory buffer object of the PVR backend (aliased as `RHI::Buffer`)

		The PVR backend consumes vertices and uniform blocks from host memory (GX draws are fed through the
		CPU write-gather FIFO, so there is no persistent device-side vertex store to upload into), which
		makes this class a plain resizable byte store exactly like the software backend's: @ref
		MapBufferRange() hands back a pointer into the store, and @ref BindBufferRange() forwards a
		sub-range to the device so a bound uniform block can be decoded by the draw dispatch.
	*/
	class PvrBuffer
	{
	public:
		explicit PvrBuffer(BufferTarget target);
		~PvrBuffer() = default;

		PvrBuffer(const PvrBuffer&) = delete;
		PvrBuffer& operator=(const PvrBuffer&) = delete;

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
			return _storage.size();
		}
		/** @brief Returns the base pointer of the host-side data store */
		inline std::uint8_t* HostData() {
			return _storage.data();
		}
		/** @brief Returns the base pointer of the host-side data store */
		inline const std::uint8_t* HostData() const {
			return _storage.data();
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

		/** @brief Sets a debug label for the buffer (ignored by the PVR backend) */
		void SetObjectLabel(StringView label);

		// Static bound-handle helpers used by the shared VAO pool. Inert here for the same reason as the
		// software backend's: there is no bound-buffer cache, the effects read the forwarded ranges.
		/** @brief Records the buffer handle bound for a target (inert for the PVR backend) */
		inline static void SetBoundHandle(std::uint32_t target, std::uint32_t glHandle) {
			static_cast<void>(target);
			static_cast<void>(glHandle);
		}
		/** @brief Marks a buffer handle as bound for a target (inert for the PVR backend) */
		inline static bool BindHandle(std::uint32_t target, std::uint32_t glHandle) {
			static_cast<void>(target);
			static_cast<void>(glHandle);
			return true;
		}

	private:
		static std::uint32_t _nextHandle;

		std::uint32_t _handle;
		BufferTarget _target;
		std::vector<std::uint8_t> _storage;
	};
}
