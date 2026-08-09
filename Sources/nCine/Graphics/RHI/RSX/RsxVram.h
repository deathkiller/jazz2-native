#pragma once

#include <cstddef>
#include <cstdint>

namespace nCine::RHI::RSX
{
	/**
		@brief GPU-visible memory of the RSX backend

		Every address the RSX reads or writes - vertex and index streams, texture texels, fragment program
		microcode, the display surfaces and the depth buffer - has to be expressed to the GPU as an *offset*
		into one of its two address windows rather than as a PPE pointer, so it cannot come from the C++
		heap. This wraps allocation and the address-to-offset translation into one call and keeps both halves
		together, which is what the command builders need: `rsxSetSurface()`, `rsxBindVertexArrayAttrib()`
		and `rsxLoadTexture()` all take an offset plus a location, never a pointer.

		**Two locations, and the choice is not free.** `Local` is the 256 MB of GDDR3 on the far side of the
		GPU: the RSX reads it at full speed, but the PPE reaches it over the bus with *uncached, write-combined*
		access, so writing texels into it is fast and reading them back is catastrophically slow (a read is
		roughly two orders of magnitude worse than main memory). `Main` is ordinary XDR that the GPU reads
		over FlexIO at a fraction of local bandwidth. The split this backend uses follows from that: render
		targets, the display buffers and the depth buffer are `Local` because only the GPU touches them;
		streamed geometry and uniform staging are `Main` because the CPU writes them every frame; textures are
		`Local` because they are written once and sampled many times.

		This is also why nothing here ever reads back through a mapping. `RsxTexture::GetTexImage()` and the
		streaming-texture path both write forward only - a caller that wants texels back keeps its own copy.

		**Fragment programs are a third case that looks like a fourth.** The RSX fetches fragment microcode
		by offset exactly like a texture, so it is allocated here too rather than through a shader-specific
		allocator - but it MUST be `Local`, because the fragment engine cannot fetch microcode from main
		memory at all. @ref AllocFragmentProgram() only names that constraint; it is otherwise @ref Alloc().

		Unlike the sceGxm backend's `GxmMemory`, there is no kernel block to keep: PSL1GHT's `rsxMemalign()`
		suballocates the RSX heap that `rsxInit()` set up, so a block is just its base pointer.
	*/
	namespace RsxVram
	{
		/** @brief Which of the GPU's two address windows a block lives in (it selects the `GCM_LOCATION_*` a command takes) */
		enum class Location
		{
			/** @brief GDDR3 video memory: full GPU bandwidth, uncached and effectively unreadable for the PPE */
			Local,
			/** @brief Main XDR memory mapped into the GPU's IO window: cached for the PPE, slower for the GPU */
			Main
		};

		/** @brief A GPU-visible allocation: its CPU-side base address and the offset the RSX addresses it by */
		struct Block
		{
			/** @brief Base address of the block, or `nullptr` when the allocation failed */
			void* Base = nullptr;
			/** @brief Offset of the block within its location's address window, as the RSX commands take it */
			std::uint32_t Offset = 0;
			/** @brief Number of bytes reserved (the request rounded up to the requested alignment) */
			std::uint32_t Size = 0;
			/** @brief Address window @ref Offset is relative to */
			Location Where = Location::Local;

			inline bool IsValid() const {
				return (Base != nullptr);
			}

			/** @brief Returns the `GCM_LOCATION_RSX` / `GCM_LOCATION_CELL` value the RSX commands expect */
			std::uint8_t GetGcmLocation() const;
		};

		/**
			@brief Brings up the main-memory heap

			Called once by `RsxDevice::CreateSwapchain()` after `rsxInit()`, which is what maps the IO window
			this suballocates. The local heap needs nothing here - `rsxInit()` sets it up itself - so only
			main memory is passed in. Returns `false` if the heap could not be set up, which the device
			treats as a failed session rather than trying to run without one.

			@param mainHeapBase  Base of the host region handed to `rsxInit()` as its IO address
			@param mainHeapSize  Size of that region in bytes
		*/
		bool Initialize(void* mainHeapBase, std::uint32_t mainHeapSize);
		/** @brief Releases the main-memory heap (the RSX heap belongs to the context and goes with it) */
		void Shutdown();

		/**
			@brief Allocates GPU-visible memory

			@param size       Requested size in bytes
			@param alignment  Required alignment; 64 is the smallest the hardware accepts for anything it
			                  fetches, and surfaces want more (see @ref AllocSurface())
			@param where      Which address window to allocate from - see the class documentation for what
			                  that costs on each side
		*/
		Block Alloc(std::uint32_t size, std::uint32_t alignment, Location where);

		/**
			@brief Allocates a render-target or display surface in local memory

			Surfaces are aligned far harder than ordinary allocations: the display controller scans out of a
			buffer the hardware requires to be 64-byte aligned with a pitch that is a multiple of 64, and a
			surface the GPU renders into wants the same so a tile can be written without a read-modify-write
			at its edge. The pitch is derived here rather than passed in, so no caller can get the pair wrong.

			@param width         Surface width in pixels
			@param height        Surface height in pixels
			@param bytesPerPixel Size of one pixel (4 for the X8R8G8B8 surfaces this backend renders into)
			@param pitchOut      Receives the row stride in bytes the surface was created with
		*/
		Block AllocSurface(std::uint32_t width, std::uint32_t height, std::uint32_t bytesPerPixel, std::uint32_t& pitchOut);

		/**
			@brief Allocates storage for fragment program microcode

			Local memory is not a preference here: the fragment engine fetches microcode over the same path
			it fetches textures and cannot reach main memory, so a fragment program placed in XDR renders
			nothing at all (and does so silently).
		*/
		Block AllocFragmentProgram(std::uint32_t size);

		/** @brief Releases a block obtained from any of the allocators above and invalidates it */
		void Free(Block& block);

		/** @brief Returns the number of bytes currently reserved in local memory (for the memory report) */
		std::uint32_t GetLocalAllocatedBytes();
		/** @brief Returns the number of bytes currently reserved in main memory (for the memory report) */
		std::uint32_t GetMainAllocatedBytes();
	}
}
