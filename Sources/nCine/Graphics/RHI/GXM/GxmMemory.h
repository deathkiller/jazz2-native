#pragma once

#include <cstddef>
#include <cstdint>

#include <psp2/kernel/sysmem.h>
#include <psp2/gxm.h>

namespace nCine::RHI::GXM
{
	/**
		@brief GPU-visible memory of the sceGxm backend

		Every address the GPU reads or writes - vertex and index streams, texture texels, uniform buffers,
		the driver's own ring buffers and the display surfaces - has to live in a kernel memory block that
		was handed to `sceGxmMapMemory()`, so it cannot come from the C++ heap. This wraps that two-step
		allocate-and-map dance into one call, keeps the `SceUID` needed to release the block, and rounds the
		request up to the page size the block type requires (the kernel rejects a size that is not a
		multiple of it).

		Every block is allocated **uncached**. The Vita exposes no user-mode data-cache write-back call
		(`ksceKernelCpuDcacheWritebackRange` is kernel-only), so a cached mapping could not be made visible
		to the GPU reliably - the classic "nothing renders" failure the GU backend flushes around on the PSP
		has no user-space remedy here. The cost is only on the CPU side of an upload; the GPU has its own
		cache in front of this memory either way.

		Two address spaces sit alongside the mappable one and get their own entry points because they are
		mapped through different calls and hand back a USSE offset rather than only a pointer: the vertex
		and fragment USSE code windows the shader patcher uploads compiled programs into.
	*/
	namespace GxmMemory
	{
		/** @brief Value @ref Block::Uid carries when an allocation failed (the SDK has no `SCE_UID_INVALID_UID` macro) */
		constexpr SceUID InvalidUid = SceUID(-1);

		/** @brief Which address space a block was mapped into (it decides the unmap call @ref Free() has to issue) */
		enum class Kind
		{
			Mapped,
			VertexUsse,
			FragmentUsse
		};

		/** @brief A GPU-visible allocation: the kernel block that owns it and its CPU-side base address */
		struct Block
		{
			/** @brief Kernel memory-block identifier, or @ref InvalidUid when the allocation failed */
			SceUID Uid = InvalidUid;
			/** @brief Address space the block is mapped into */
			Kind MappedAs = Kind::Mapped;
			/** @brief Base address of the block, or `nullptr` when the allocation failed */
			void* Base = nullptr;
			/** @brief Number of bytes actually reserved (the request rounded up to the block's page size) */
			std::uint32_t Size = 0;
			/** @brief USSE offset of the block, for the two USSE allocators (0 otherwise) */
			std::uint32_t UsseOffset = 0;

			inline bool IsValid() const {
				return (Base != nullptr);
			}
		};

		/**
			@brief Allocates GPU-visible memory from main LPDDR and maps it for the GPU

			@param name     Debug name of the kernel block (shown by the memory-usage tools)
			@param size     Requested size in bytes, rounded up to the block type's 4 KB page size
			@param attribs  What the GPU may do with the memory (read, write or both)
		*/
		Block Alloc(const char* name, std::uint32_t size, SceGxmMemoryAttribFlags attribs);

		/**
			@brief Allocates GPU-visible memory from CDRAM and maps it for the GPU

			The 128 MB of CDRAM is the memory the display controller scans out of and the fastest memory the
			GPU can render into, so the display surfaces and the off-screen render targets are placed here.
			Falls back to LPDDR (@ref Alloc()) when CDRAM is exhausted, which keeps a large level loading
			rather than failing outright. Its allocation granularity is 256 KB, so small requests are better
			served by @ref Alloc().
		*/
		Block AllocCdram(const char* name, std::uint32_t size, SceGxmMemoryAttribFlags attribs);

		/**
			@brief Acquires CDRAM for a render target's texels, at an address reserved for that exact geometry

			A colour surface is identified to the driver by its base address, and a target of a given size always
			comes back here at the same address: blocks handed out through this pair are retired for reuse rather
			than released, and only ever reused for the identical stride and height. That matters because the
			pipeline destroys and rebuilds its whole viewport chain whenever the render passes change (entering a
			level, leaving it, splitting the screen), so a plain allocator hands the address a retired target used
			to occupy to the next target of a *different* size - and a driver that has cached anything about that
			address, an emulator especially, then has two conflicting descriptions of one surface and drops one of
			them. Trading a bounded amount of retained CDRAM for stable addresses avoids the whole class of
            problem; past @ref MaxRetainedSurfaces distinct geometries it degrades to @ref AllocCdram().
		*/
		Block AcquireSurface(const char* name, std::uint32_t stride, std::uint32_t height);
		/** @brief Retires a block from @ref AcquireSurface(), keeping its address reserved for the same geometry */
		void ReleaseSurface(Block& block);
		/** @brief Releases every retained render-target surface before the GXM device is terminated */
		void ReleaseRetainedSurfaces();

		/** @brief Allocates memory in the vertex USSE window (for the shader patcher's compiled vertex programs) */
		Block AllocVertexUsse(const char* name, std::uint32_t size);
		/** @brief Allocates memory in the fragment USSE window (for the shader patcher's compiled fragment programs) */
		Block AllocFragmentUsse(const char* name, std::uint32_t size);

		/** @brief Unmaps and releases a block obtained from any of the allocators above */
		void Free(Block& block);

		/** @brief Returns the total number of bytes currently reserved through this allocator (for the memory report) */
		std::uint32_t GetAllocatedBytes();

		struct SurfaceTelemetry
		{
			std::uint32_t RetainedSurfaces = 0;
			std::uint32_t InUseSurfaces = 0;
			std::uint32_t NewAcquisitions = 0;
			std::uint32_t ReusedAcquisitions = 0;
		};
		/** @brief Returns current render-target pool state and clears interval acquisition counters */
		SurfaceTelemetry GetAndResetSurfaceTelemetry();
	}
}
