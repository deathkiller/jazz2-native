#include "GsVram.h"

namespace nCine::RHI::GS
{
	std::uint64_t GsVram::_pageBitmap[GsVram::BitmapWords] = {};
	std::uint32_t GsVram::_clutBitmap = 0;
	std::uint32_t GsVram::_clutSlotCount = 0;
	std::uint32_t GsVram::_clutFirstBlock = 0;
	std::uint32_t GsVram::_displayBufferPage[2] = {};
	std::int32_t GsVram::_displayBufferCount = 0;
	std::int32_t GsVram::_displayBufferWidth = 0;
	GsPsm GsVram::_displayPsm = GsPsm::Ct16;
	std::uint32_t GsVram::_reserveFirstPage = 0;
	std::uint32_t GsVram::_reservePageCount = 0;
	std::uint32_t GsVram::_cacheFirstPage = 0;
	std::uint32_t GsVram::_cachePageCount = 0;
	std::uint32_t GsVram::_usedPageCount = 0;
	std::uint32_t GsVram::_peakUsedPageCount = 0;
	std::uint32_t GsVram::_failedAllocationCount = 0;
	bool GsVram::_initialized = false;

	namespace
	{
		/** @brief Number of CLUT slots the slab bitmap can track */
		constexpr std::uint32_t MaxClutSlots = 32;
		/** @brief Blocks of one CLUT slot */
		constexpr std::uint32_t ClutSlotBlocks = GsVram::ClutSlotBytes / GsVram::BlockBytes;

		constexpr std::uint32_t AlignUp(std::uint32_t value, std::uint32_t alignment)
		{
			return (value + alignment - 1) & ~(alignment - 1);
		}

		constexpr std::int32_t AlignUpInt(std::int32_t value, std::int32_t alignment)
		{
			return (value + alignment - 1) / alignment * alignment;
		}

		/** @brief Rounds up to a power of two (1 for 0) */
		std::uint32_t RoundUpPow2(std::uint32_t value)
		{
			if (value <= 1) {
				return 1;
			}
			std::uint32_t result = 1;
			while (result < value) {
				result <<= 1;
			}
			return result;
		}
	}

	void GsVram::GetPageGeometry(GsPsm psm, std::int32_t& width, std::int32_t& height)
	{
		// One page is always 8 KB, so the texels it covers follow from the bits per texel - but the split
		// between the two axes is fixed by the hardware's page layout, not chosen
		switch (psm) {
			case GsPsm::Ct32:
			case GsPsm::Ct24:
				width = 64;
				height = 32;
				break;
			case GsPsm::Ct16:
			case GsPsm::Ct16S:
				width = 64;
				height = 64;
				break;
			case GsPsm::T8:
				width = 128;
				height = 64;
				break;
			case GsPsm::T4:
				width = 128;
				height = 128;
				break;
			default:
				// Unknown mode: assume the densest page so the reservation cannot come out too small
				width = 64;
				height = 32;
				break;
		}
	}

	std::uint32_t GsVram::GetPageCount(GsPsm psm, std::int32_t width, std::int32_t height)
	{
		if (width <= 0 || height <= 0) {
			return 0;
		}

		std::int32_t pageWidth, pageHeight;
		GetPageGeometry(psm, pageWidth, pageHeight);

		const std::int32_t pagesX = AlignUpInt(width, pageWidth) / pageWidth;
		const std::int32_t pagesY = AlignUpInt(height, pageHeight) / pageHeight;
		return std::uint32_t(pagesX) * std::uint32_t(pagesY);
	}

	std::int32_t GsVram::GetBufferWidth(GsPsm psm, std::int32_t width)
	{
		if (width <= 0) {
			return 1;
		}

		std::int32_t pageWidth, pageHeight;
		GetPageGeometry(psm, pageWidth, pageHeight);

		// Padded to the page geometry, because that is the pitch the reserved pages were counted for.
		// `TBW`/`FBW` count 64 texels each, and every page geometry is a multiple of that, so the
		// division is exact
		return AlignUpInt(width, pageWidth) / BufferWidthUnit;
	}

	std::int32_t GsVram::GetPaddedWidth(GsPsm psm, std::int32_t width)
	{
		if (width <= 0) {
			return BufferWidthUnit;
		}

		std::int32_t pageWidth, pageHeight;
		GetPageGeometry(psm, pageWidth, pageHeight);
		return AlignUpInt(width, pageWidth);
	}

	bool GsVram::Initialize(const GsVramLayout& layout)
	{
		if (layout.DisplayWidth <= 0 || layout.DisplayHeight <= 0 ||
			layout.DisplayBufferCount < 1 || layout.DisplayBufferCount > 2) {
			return false;
		}

		const std::uint32_t displayPages = GetPageCount(layout.DisplayPsm, layout.DisplayWidth, layout.DisplayHeight);
		const std::uint32_t clutSlots = (layout.ClutSlotCount < MaxClutSlots ? layout.ClutSlotCount : MaxClutSlots);
		const std::uint32_t clutPages = AlignUp(clutSlots * ClutSlotBytes, PageBytes) / PageBytes;

		const std::uint32_t staticPages = displayPages * std::uint32_t(layout.DisplayBufferCount) + layout.ReservedRttPages + clutPages;
		if (staticPages >= TotalPages) {
			// Nothing is committed on failure, so the caller can retry with a smaller display mode
			return false;
		}

		for (std::uint32_t i = 0; i < BitmapWords; i++) {
			_pageBitmap[i] = 0;
		}
		_clutBitmap = 0;
		_usedPageCount = 0;
		_peakUsedPageCount = 0;
		_failedAllocationCount = 0;

		std::uint32_t nextPage = 0;

		_displayBufferCount = layout.DisplayBufferCount;
		_displayBufferWidth = GetBufferWidth(layout.DisplayPsm, layout.DisplayWidth);
		_displayPsm = layout.DisplayPsm;
		for (std::int32_t i = 0; i < layout.DisplayBufferCount; i++) {
			_displayBufferPage[i] = nextPage;
			nextPage += displayPages;
		}
		if (layout.DisplayBufferCount == 1) {
			_displayBufferPage[1] = _displayBufferPage[0];
		}

		_reserveFirstPage = nextPage;
		_reservePageCount = layout.ReservedRttPages;
		nextPage += layout.ReservedRttPages;

		_clutFirstBlock = nextPage * BlocksPerPage;
		_clutSlotCount = clutSlots;
		nextPage += clutPages;

		_cacheFirstPage = nextPage;
		_cachePageCount = TotalPages - nextPage;

		// The static regions are outside both allocation windows, so they are never handed out and do not
		// need to be marked; the bitmap only ever describes the reserve and the cache
		_initialized = true;
		return true;
	}

	bool GsVram::IsInitialized()
	{
		return _initialized;
	}

	std::uint32_t GsVram::GetDisplayBufferPage(std::int32_t index)
	{
		return _displayBufferPage[index & 1];
	}

	std::int32_t GsVram::GetDisplayBufferWidth()
	{
		return _displayBufferWidth;
	}

	GsPsm GsVram::GetDisplayPsm()
	{
		return _displayPsm;
	}

	bool GsVram::IsRangeFree(std::uint32_t firstPage, std::uint32_t pageCount)
	{
		for (std::uint32_t page = firstPage; page < firstPage + pageCount; page++) {
			if ((_pageBitmap[page >> 6] & (std::uint64_t(1) << (page & 63))) != 0) {
				return false;
			}
		}
		return true;
	}

	void GsVram::MarkRange(std::uint32_t firstPage, std::uint32_t pageCount, bool used)
	{
		for (std::uint32_t page = firstPage; page < firstPage + pageCount; page++) {
			const std::uint64_t mask = std::uint64_t(1) << (page & 63);
			if (used) {
				_pageBitmap[page >> 6] |= mask;
			} else {
				_pageBitmap[page >> 6] &= ~mask;
			}
		}
	}

	std::uint32_t GsVram::AllocateWithin(std::uint32_t windowFirstPage, std::uint32_t windowPageCount, std::uint32_t pageCount)
	{
		if (pageCount == 0 || pageCount > windowPageCount) {
			return InvalidPage;
		}

		// A power-of-two run placed at a multiple of its own size can only ever recombine with its sibling,
		// which is what keeps the window from fragmenting; past the cap the run just has to be page-aligned
		const std::uint32_t alignment = (pageCount < PagesPerAlignment ? pageCount : PagesPerAlignment);
		const std::uint32_t windowEndPage = windowFirstPage + windowPageCount;

		for (std::uint32_t page = AlignUp(windowFirstPage, alignment); page + pageCount <= windowEndPage; page += alignment) {
			if (IsRangeFree(page, pageCount)) {
				MarkRange(page, pageCount, true);
				return page;
			}
		}

		return InvalidPage;
	}

	std::uint32_t GsVram::AllocatePages(std::uint32_t pageCount)
	{
		if (!_initialized) {
			_failedAllocationCount++;
			return InvalidPage;
		}

		const std::uint32_t rounded = RoundUpPow2(pageCount);
		const std::uint32_t page = AllocateWithin(_cacheFirstPage, _cachePageCount, rounded);
		if (page == InvalidPage) {
			_failedAllocationCount++;
			return InvalidPage;
		}

		_usedPageCount += rounded;
		if (_usedPageCount > _peakUsedPageCount) {
			_peakUsedPageCount = _usedPageCount;
		}
		return page;
	}

	void GsVram::FreePages(std::uint32_t firstPage, std::uint32_t pageCount)
	{
		if (firstPage == InvalidPage || pageCount == 0) {
			return;
		}

		// Freed with the same rounding it was allocated with, so the caller can pass the logical count back
		const std::uint32_t rounded = RoundUpPow2(pageCount);
		MarkRange(firstPage, rounded, false);
		_usedPageCount -= (rounded < _usedPageCount ? rounded : _usedPageCount);
	}

	std::uint32_t GsVram::AllocateReservedPages(std::uint32_t pageCount)
	{
		if (!_initialized || pageCount == 0 || pageCount > _reservePageCount) {
			_failedAllocationCount++;
			return InvalidPage;
		}

		// Plain first fit at page granularity, deliberately NOT the power-of-two aligned placement the
		// texture cache uses. The reserve is a small dedicated pool of non-evictable surfaces that are
		// allocated once and never churn, so it has nothing to gain from anti-fragmentation alignment - and
		// a great deal to lose: a 16-page target could not be placed in a 16-page reserve at all unless the
		// reserve happened to start on a 16-page boundary, which is what the static layout ahead of it
		// decides. That is exactly how a 256x256 render target failed to fit a reserve sized for it.
		const std::uint32_t reserveEndPage = _reserveFirstPage + _reservePageCount;
		for (std::uint32_t page = _reserveFirstPage; page + pageCount <= reserveEndPage; page++) {
			if (IsRangeFree(page, pageCount)) {
				MarkRange(page, pageCount, true);
				return page;
			}
		}

		_failedAllocationCount++;
		return InvalidPage;
	}

	void GsVram::FreeReservedPages(std::uint32_t firstPage, std::uint32_t pageCount)
	{
		if (firstPage == InvalidPage || pageCount == 0) {
			return;
		}
		// Freed exactly as allocated - the reserve does not round up (see AllocateReservedPages)
		MarkRange(firstPage, pageCount, false);
	}

	std::uint32_t GsVram::AllocateClut()
	{
		if (!_initialized) {
			_failedAllocationCount++;
			return InvalidBlock;
		}

		for (std::uint32_t slot = 0; slot < _clutSlotCount; slot++) {
			const std::uint32_t mask = std::uint32_t(1) << slot;
			if ((_clutBitmap & mask) == 0) {
				_clutBitmap |= mask;
				return _clutFirstBlock + slot * ClutSlotBlocks;
			}
		}

		_failedAllocationCount++;
		return InvalidBlock;
	}

	void GsVram::FreeClut(std::uint32_t block)
	{
		if (block == InvalidBlock || block < _clutFirstBlock) {
			return;
		}

		const std::uint32_t slot = (block - _clutFirstBlock) / ClutSlotBlocks;
		if (slot < _clutSlotCount) {
			_clutBitmap &= ~(std::uint32_t(1) << slot);
		}
	}

	std::uint32_t GsVram::GetCacheFirstPage()
	{
		return _cacheFirstPage;
	}

	std::uint32_t GsVram::GetCachePageCount()
	{
		return _cachePageCount;
	}

	std::uint32_t GsVram::GetUsedPageCount()
	{
		return _usedPageCount;
	}

	std::uint32_t GsVram::GetPeakUsedPageCount()
	{
		return _peakUsedPageCount;
	}

	std::uint32_t GsVram::GetFailedAllocationCount()
	{
		return _failedAllocationCount;
	}
}
