#pragma once

#include <cstdint>

namespace nCine::RHI::GS
{
	/**
		@brief Pixel storage mode of a Graphics Synthesizer buffer (the `PSM` field of `TEX0`/`FRAME`)

		Only the modes this backend can actually place in video memory are listed. The enumerator values are
		the hardware ones, so they can be written into a register field directly.
	*/
	enum class GsPsm : std::uint8_t
	{
		Ct32 = 0x00,	/**< RGBA8888 */
		Ct24 = 0x01,	/**< RGB888 (stored in 32-bit cells, upper byte unused) */
		Ct16 = 0x02,	/**< RGBA5551 */
		Ct16S = 0x0A,	/**< RGBA5551, "scrambled" page layout */
		T8 = 0x13,		/**< 8-bit indexed, colours through a CLUT */
		T4 = 0x14		/**< 4-bit indexed, colours through a CLUT */
	};

	/**
		@brief Static layout of the Graphics Synthesizer's local memory, as requested by the device

		Passed to @ref GsVram::Initialize(), which places the display buffers first, then the render-target
		reserve, then the CLUT slab, and gives every page left over to the texture cache.
	*/
	struct GsVramLayout
	{
		/** @brief Width of a display buffer in pixels (must be a multiple of 64, the `FBW` unit) */
		std::int32_t DisplayWidth = 640;
		/** @brief Height of a display buffer in pixels */
		std::int32_t DisplayHeight = 448;
		/** @brief Pixel storage mode of the display buffers */
		GsPsm DisplayPsm = GsPsm::Ct16;
		/** @brief Number of display buffers (1 for single-buffered, 2 for the usual flip) */
		std::int32_t DisplayBufferCount = 2;
		/**
			@brief Pages statically reserved for render targets

			Render targets have no host copy to rebuild from, so they must never be evicted (the PVR
			backend spares them in its eviction walk for the same reason). Rather than let them compete
			with the texture cache for space and fail unpredictably - which is how the Dreamcast's
			256x256 `TexturedBackgroundPass` target used to fail to allocate - the ones the engine is
			known to want are reserved up front out of a fixed pool.
		*/
		std::uint32_t ReservedRttPages = 16;
		/** @brief Number of 1 KB CLUT slots in the slab (a 256-entry 32-bit CLUT is exactly one slot) */
		std::uint32_t ClutSlotCount = 32;
	};

	/**
		@brief Page allocator for the Graphics Synthesizer's 4 MB of local memory

		The GS has no memory allocator of its own: a buffer is simply a base address written into a register
		(`TEX0.TBP0` for a texture, `FRAME.FBP` for a render target), and it is the program's job to make sure
		two buffers never overlap. This class is that bookkeeping. It hands out **pages**, and only pages.

		@section GsVram-granularity Why the granularity is a whole page

		Local memory is organised as 512 pages of 8 KB, each page 32 blocks of 256 bytes. Within a page the
		mapping from a texel coordinate to a block is a swizzle that **depends on the pixel storage mode** -
		`PSMT8` and `PSMCT32` order the blocks of a page differently, and each mode covers a different number
		of texels per page (see @ref GetPageGeometry()). Two buffers of different modes that share a page
		therefore interleave unpredictably and corrupt each other, which is a failure mode the Dreamcast's
		byte-granular `pvr_mem_malloc()` simply does not have.

		Allocating whole pages removes the hazard by construction rather than by care: if no page is ever
		shared by two buffers, no page can ever mix two storage modes, and the swizzle stays a private matter
		inside each allocation. The cost is internal fragmentation of up to one page per texture - and for the
		sheets this engine actually loads it is usually zero, because a power-of-two sheet at least as large as
		one page in each direction covers an exact number of pages (a 256x256 `PSMT8` sheet is exactly 8).

		@section GsVram-fragmentation Placement

		Every request takes **exactly** the pages it needs, placed in the shortest free run long enough to
		hold it (best fit, lowest address first).

		It used to round the request up to a power of two and align it to its own size, which buys a buddy
		allocator's freedom from external fragmentation. That trade is wrong for a cache this small: the
		rounding costs up to *twice* the pages a buffer actually needs, and the page counts it was assumed
		to be free for - power-of-two sheets - are only half the content. The stores that are not
		power-of-two are exactly the large ones, where the waste is measured in hundreds of kilobytes: a
		640x448 `PSMCT32` cinematic frame is 140 pages and was rounded to 256, throwing away 928 KB of a
		3.3 MB cache, and a level's own mix of sheets lost about a fifth of the window to the same rounding.
		External fragmentation costs nothing like that here, because it is not fatal: a request that cannot
		be placed makes the caller evict and retry (see @ref GsVram-residency), and best fit keeps the long
		runs the big atlases need intact by consuming the short ones first.

		@section GsVram-residency Residency

		This class only places and frees; it does not decide what should be resident. That is the caller's
		job - `GsTexture` keeps the same most-recently-used list, per-frame stamp and host-store rebuild the
		PVR backend uses, and calls @ref AllocatePages() through it. A failed allocation is reported, not
		fatal: the caller evicts and retries, exactly as `PvrTexture::AllocateVram()` does.
	*/
	class GsVram
	{
	public:
		/** @brief Size of one block, the unit of `TEX0.TBP0` and `TEX0.CBP` */
		static constexpr std::uint32_t BlockBytes = 256;
		/** @brief Size of one page, the unit of `FRAME.FBP` */
		static constexpr std::uint32_t PageBytes = 8192;
		/** @brief Blocks per page */
		static constexpr std::uint32_t BlocksPerPage = PageBytes / BlockBytes;
		/** @brief Total local memory of the Graphics Synthesizer */
		static constexpr std::uint32_t TotalBytes = 4 * 1024 * 1024;
		/** @brief Total pages of local memory */
		static constexpr std::uint32_t TotalPages = TotalBytes / PageBytes;
		/** @brief Total blocks of local memory */
		static constexpr std::uint32_t TotalBlocks = TotalBytes / BlockBytes;
		/** @brief Texels of buffer width one unit of `TEX0.TBW`/`FRAME.FBW` stands for */
		static constexpr std::int32_t BufferWidthUnit = 64;
		/** @brief Bytes of one CLUT slot (256 entries of 32 bits) */
		static constexpr std::uint32_t ClutSlotBytes = 1024;

		/** @brief Returned by the allocators when the request could not be placed */
		static constexpr std::uint32_t InvalidPage = ~std::uint32_t(0);
		/** @brief Returned by @ref AllocateClut() when the slab is full */
		static constexpr std::uint32_t InvalidBlock = ~std::uint32_t(0);

		/** @brief Returns the number of texels one page covers horizontally and vertically in @p psm */
		static void GetPageGeometry(GsPsm psm, std::int32_t& width, std::int32_t& height);
		/**
			@brief Returns the pages a @p width x @p height buffer of @p psm occupies

			Both dimensions are rounded up to the page geometry of the storage mode, so the result is the
			number of *whole* pages the buffer's addressing can reach - which is what has to be reserved.
		*/
		static std::uint32_t GetPageCount(GsPsm psm, std::int32_t width, std::int32_t height);
		/**
			@brief Returns the `TBW`/`FBW` value for a buffer @p width texels wide in @p psm

			The buffer pitch is padded to the page geometry like @ref GetPageCount() pads it, because the
			pages behind it were reserved for the padded pitch. Note that a padded pitch costs no texture
			coordinate compensation the way the PowerVR's power-of-two padding does: `TBW` is the pitch and
			`TW`/`TH` are the sampled extent, and the two are independent fields.
		*/
		static std::int32_t GetBufferWidth(GsPsm psm, std::int32_t width);
		/**
			@brief Returns the buffer pitch of a @p width texel wide buffer of @p psm, **in texels**

			The same padded pitch @ref GetBufferWidth() reports, but not divided into `TBW` units. Both forms
			are needed and they are easy to confuse: the raw register field wants `TBW`, while PS2SDK's
			`libdraw` takes `texbuffer_t::width`/`framebuffer_t::width` in texels and divides by 64 itself.
			Handing it a `TBW` value silently samples a 64-times-too-narrow buffer.
		*/
		static std::int32_t GetPaddedWidth(GsPsm psm, std::int32_t width);

		/**
			@brief Places the static regions of @p layout and gives the rest to the texture cache

			Resets every allocation, so it is also how a test starts from a known state. Returns `false` if
			the static regions alone do not fit in local memory, in which case nothing is initialised.
		*/
		static bool Initialize(const GsVramLayout& layout);
		/** @brief Returns `true` once @ref Initialize() has succeeded */
		static bool IsInitialized();

		/** @brief Returns the first page of display buffer @p index (`FRAME.FBP` is a page number) */
		static std::uint32_t GetDisplayBufferPage(std::int32_t index);
		/** @brief Returns the `FBW` of the display buffers */
		static std::int32_t GetDisplayBufferWidth();
		/** @brief Returns the pixel storage mode of the display buffers */
		static GsPsm GetDisplayPsm();

		/**
			@brief Allocates exactly @p pageCount pages from the texture cache

			Returns the first page, or @ref InvalidPage when no run that long is free - the caller is expected
			to evict and retry rather than treat it as fatal.
		*/
		static std::uint32_t AllocatePages(std::uint32_t pageCount);
		/** @brief Returns a run obtained from @ref AllocatePages() to the cache */
		static void FreePages(std::uint32_t firstPage, std::uint32_t pageCount);

		/**
			@brief Allocates pages from the render-target reserve

			Kept apart from the texture cache so that a render target - which has no host copy and so cannot
			be evicted and rebuilt - can never be crowded out by streaming textures. A target that does not
			fit here is not fatal either: @ref GsTexture::SetRenderTarget() falls back to the cache, where it
			is still exempt from eviction. The reserve is what keeps the common case off the streaming
			window, not a hard ceiling on render targets.
		*/
		static std::uint32_t AllocateReservedPages(std::uint32_t pageCount);
		/** @brief Returns a run obtained from @ref AllocateReservedPages() */
		static void FreeReservedPages(std::uint32_t firstPage, std::uint32_t pageCount);

		/**
			@brief Allocates one CLUT slot, returning its block address (the unit of `TEX0.CBP`)

			Every slot holds a 256-entry 32-bit CLUT, so all of them share the one storage mode and may
			share pages - the slab is the single place this class sub-allocates a page, and it is safe for
			exactly that reason. Returns @ref InvalidBlock when the slab is full.
		*/
		static std::uint32_t AllocateClut();
		/** @brief Returns a CLUT slot obtained from @ref AllocateClut() */
		static void FreeClut(std::uint32_t block);

		/** @brief Returns the first page of the texture cache */
		static std::uint32_t GetCacheFirstPage();
		/** @brief Returns the number of pages in the texture cache */
		static std::uint32_t GetCachePageCount();
		/** @brief Returns the number of texture-cache pages currently allocated */
		static std::uint32_t GetUsedPageCount();
		/** @brief Returns the high-water mark of @ref GetUsedPageCount() */
		static std::uint32_t GetPeakUsedPageCount();
		/**
			@brief Returns the length of the longest free run in the texture cache

			The difference between this and `GetCachePageCount() - GetUsedPageCount()` is the fragmentation,
			which is the number to look at when an allocation fails while the cache is not full.
		*/
		static std::uint32_t GetLargestFreeRun();
		/**
			@brief Returns how many allocations have failed since @ref Initialize()

			The Dreamcast work used exactly this counter to tell a working set that fits from one that
			thrashes (a level went from 2972 failed allocations to none), and it is the first number to look
			at when bringing a level up here.
		*/
		static std::uint32_t GetFailedAllocationCount();

	private:
		GsVram() = delete;

		/** @brief Returns `true` when the page is free */
		static bool IsPageFree(std::uint32_t page);
		/** @brief Marks every page of the run allocated or free */
		static void MarkRange(std::uint32_t firstPage, std::uint32_t pageCount, bool used);
		/** @brief Places a run of exactly @p pageCount pages in the shortest free run of a page window that holds it */
		static std::uint32_t AllocateWithin(std::uint32_t windowFirstPage, std::uint32_t windowPageCount, std::uint32_t pageCount);
		/** @brief Returns the longest free run of a page window */
		static std::uint32_t LargestFreeRunWithin(std::uint32_t windowFirstPage, std::uint32_t windowPageCount);

		static constexpr std::uint32_t BitmapWords = TotalPages / 64;

		static std::uint64_t _pageBitmap[BitmapWords];
		static std::uint32_t _clutBitmap;
		static std::uint32_t _clutSlotCount;
		static std::uint32_t _clutFirstBlock;
		static std::uint32_t _displayBufferPage[2];
		static std::int32_t _displayBufferCount;
		static std::int32_t _displayBufferWidth;
		static GsPsm _displayPsm;
		static std::uint32_t _reserveFirstPage;
		static std::uint32_t _reservePageCount;
		static std::uint32_t _cacheFirstPage;
		static std::uint32_t _cachePageCount;
		static std::uint32_t _usedPageCount;
		static std::uint32_t _peakUsedPageCount;
		static std::uint32_t _failedAllocationCount;
		static bool _initialized;
	};
}
