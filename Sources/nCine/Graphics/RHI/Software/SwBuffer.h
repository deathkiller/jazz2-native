#pragma once

#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RhiSoftware
{
	/**
		@brief Host-memory buffer object of the software backend (aliased as `Rhi::Buffer`)

		Backs a vertex, index or uniform buffer with a plain resizable byte store. Because the software
		backend reads everything from host memory there is no device-side upload: @ref MapBufferRange()
		simply hands back a pointer into the store, and @ref BindBufferRange() forwards a sub-range to the
		device so a bound uniform block can be sampled by the C++ effects at draw time.
	*/
	class SwBuffer
	{
	public:
		explicit SwBuffer(BufferTarget target);
		~SwBuffer() = default;

		SwBuffer(const SwBuffer&) = delete;
		SwBuffer& operator=(const SwBuffer&) = delete;

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
		/** @brief (Re)creates the data store like @ref BufferData(); the software backend has no immutable storage, so the flags are ignored */
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

		/** @brief Sets a debug label for the buffer (ignored by the software backend) */
		void SetObjectLabel(StringView label);

		// Static bound-handle helpers used by the shared VAO pool. The OpenGL backend keeps a per-target
		// bound-buffer cache here; the software backend has no such state, so these are inert. They mirror
		// the OpenGL backend's static helpers so `RenderVaoPool` (which tracks a VAO's element array buffer)
		// compiles unchanged against the `Rhi::Buffer` alias.
		/** @brief Records the buffer handle bound for a target (inert for the software backend) */
		inline static void SetBoundHandle(std::uint32_t target, std::uint32_t glHandle) {
			static_cast<void>(target);
			static_cast<void>(glHandle);
		}
		/** @brief Marks a buffer handle as bound for a target (inert for the software backend) */
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
	};
}
