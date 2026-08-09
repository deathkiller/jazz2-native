#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace nCine::RHI::GS
{
	/**
		@brief Main-memory scratch a GIF transfer can be issued from

		Every path that puts something in the Graphics Synthesizer's local memory - a texture store, a palette
		bake, a CLUT, the lighting compositor's surface - fills a contiguous image here first and hands its
		address to `draw_texture_transfer()`. That builds a DMA **chain** whose `REF` tags point back at this
		memory rather than copying it, which puts two requirements on the buffer that an ordinary container
		does not meet:

		- **Qword alignment.** A DMA address ignores its low four bits. A buffer that starts 8 bytes into a
		  qword is therefore not transferred from where it starts - it is transferred from 8 bytes *before*
		  it, and the whole image lands shifted. `std::malloc()` promises only the alignment of the largest
		  fundamental type, which on this target is 8; whether a given allocation happens to be qword-aligned
		  as well is down to where it fell in the heap. `alignas` on a container does not help, because it
		  aligns the container OBJECT and not the block it points at.
		- **A cache line of its own at each end.** The caller has to write this back out of the EE's
		  write-back data cache before the DMA reads it (@ref GsDevice::WritebackForDma()). Writing back a
		  range that shares its first or last 64-byte line with an unrelated object is harmless but
		  imprecise; owning whole lines keeps the operation to exactly this buffer.

		Allocated with one over-sized `std::malloc()` and an aligned pointer inside it, rather than
		`std::aligned_alloc()` or `memalign()`, so the buffer has no dependency on which of the two the
		target's C library actually provides.

		Only ever grows: the renderer is single-threaded and these are reused every frame, so shrinking would
		trade a reallocation for memory the next frame asks for again.
	*/
	class GsStagingBuffer
	{
	public:
		/** @brief Alignment of the returned pointer - a whole EE data-cache line, which is also qword-aligned */
		static constexpr std::size_t Alignment = 64;

		GsStagingBuffer() = default;

		~GsStagingBuffer()
		{
			std::free(_block);
		}

		GsStagingBuffer(const GsStagingBuffer&) = delete;
		GsStagingBuffer& operator=(const GsStagingBuffer&) = delete;

		/** @brief Returns the base of the buffer, aligned to @ref Alignment (`nullptr` before a reserve) */
		inline std::uint8_t* Data() const {
			return _data;
		}
		/** @brief Returns the number of usable bytes */
		inline std::size_t GetSize() const {
			return _size;
		}

		/**
			@brief Makes the buffer at least @p bytes long, without preserving what was in it

			Returns `nullptr` if the allocation failed, in which case the previous buffer is kept - a caller
			that checks the result therefore never writes into a buffer shorter than it asked for.
		*/
		std::uint8_t* Reserve(std::size_t bytes)
		{
			if (bytes <= _size) {
				return _data;
			}

			// Over-allocate by the alignment so there is always an aligned address inside the block
			void* block = std::malloc(bytes + Alignment);
			if (block == nullptr) {
				return nullptr;
			}

			std::free(_block);
			_block = block;
			const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(block);
			_data = reinterpret_cast<std::uint8_t*>((address + (Alignment - 1)) & ~std::uintptr_t(Alignment - 1));
			_size = bytes;
			return _data;
		}

	private:
		void* _block = nullptr;
		std::uint8_t* _data = nullptr;
		std::size_t _size = 0;
	};
}
