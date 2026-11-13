#pragma once

#include <cstddef>
#include <cstdint>

struct GX2Surface;

namespace nCine::RHI::GX2
{
	/**
		@brief GPU-visible memory of the GX2 backend

		The Wii U's GPU (the R700-class "Latte") reads system memory directly, so unlike the PlayStation 3's
		RSX there is no address window and no offset translation: a block is a plain CPU pointer that is also
		the address the GPU fetches from. What the CPU has to do is keep its caches out of the way. The
		Espresso's data cache is not coherent with the GPU, so every CPU write the GPU is meant to see has to
		be flushed with @ref FlushCpuWrites() (`GX2Invalidate()` with the matching mode, which also drops the
		GPU's own cache lines for that range) before the draw that reads it is submitted.

		<b>MEM2 by default, MEM1 for the hottest render targets.</b> Buffers, ordinary textures and anything else
		that has to survive the background come from the default heap in MEM2 (`MEMAllocFromDefaultHeapEx()`).
		MEM1 (32 MB of fast on-chip memory) is owned by wiiu-sdl2: its video driver puts a heap over all of it
		when the application gets the foreground and destroys that heap when it loses it (HOME menu). Colour
		surfaces are taken from that heap through GX2R (@ref AllocColorSurfaceMem1()), which allocates with the
		allocator SDL installed (`GX2RSetAllocator()`). Such a block is only valid while in the foreground:
		@ref Gx2Device::EnterBackground() frees every one of them and the owners allocate them again when next
		used. On the console, render targets in MEM1 halve the time of every pass that draws into them.
	*/
	namespace Gx2Memory
	{
		/** @brief One allocation; `Base` is both the CPU and the GPU address */
		struct Block
		{
			void* Base = nullptr;
			std::uint32_t Size = 0;
			/** @brief GX2R resource flags of a block from MEM1 (@ref AllocColorSurfaceMem1()), 0 for MEM2 */
			std::uint32_t Mem1Flags = 0;

			inline bool IsValid() const {
				return (Base != nullptr);
			}
			inline bool IsMem1() const {
				return (Mem1Flags != 0);
			}
		};

		/**
			@brief Allocates GPU-visible memory from MEM2

			@param size       Requested size in bytes
			@param alignment  Required alignment. Vertex and index buffers need 64 bytes
			                  (`GX2_VERTEX_BUFFER_ALIGNMENT`), uniform blocks 256 (`GX2_UNIFORM_BLOCK_ALIGNMENT`)
			                  and surfaces whatever `GX2CalcSurfaceSizeAndAlignment()` reports
		*/
		Block Alloc(std::uint32_t size, std::uint32_t alignment);
		/**
			@brief Allocates the image of a colour surface in MEM1, or returns an invalid block if it doesn't fit

			Only valid while the application is in the foreground (see the namespace description). The surface's
			size and alignment must have been computed (`GX2CalcSurfaceSizeAndAlignment()`); the surface itself is
			not modified. The block is released with @ref Free() like any other.
		*/
		Block AllocColorSurfaceMem1(const GX2Surface& surface);
		/** @brief Releases a block and resets it; the caller must know the GPU is done with it */
		void Free(Block& block);

		/** @brief Flushes CPU writes to a range out of the data cache and invalidates the GPU caches given by @p mode */
		void FlushCpuWrites(const void* base, std::uint32_t size, std::uint32_t mode);

		/** @brief Bytes currently allocated through @ref Alloc() */
		std::uint32_t GetAllocatedBytes();
		/** @brief Bytes currently allocated through @ref AllocColorSurfaceMem1() */
		std::uint32_t GetMem1Bytes();
	}
}
