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

	bool GsVram::IsPageFree(std::uint32_t page)
	{
		return ((_pageBitmap[page >> 6] & (std::uint64_t(1) << (page & 63))) == 0);
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

		// Best fit: the shortest free run that still holds the request, lowest address among equals. First
		// fit is the obvious alternative and is worse here for one specific reason - the working set mixes a
		// handful of large atlases with dozens of one-page sheets, and first fit spends the head of the
		// window (which after a drain is the only run long enough for an atlas) on whichever small sheet
		// asked first. Best fit spends the short runs on the short requests, which is exactly what keeps the
		// long ones available. The scan is over 512 bits and runs a few dozen times a frame.
		const std::uint32_t windowEndPage = windowFirstPage + windowPageCount;
		std::uint32_t bestPage = InvalidPage, bestLength = 0;
		std::uint32_t runStart = 0, runLength = 0;

		for (std::uint32_t page = windowFirstPage; page <= windowEndPage; page++) {
			if (page < windowEndPage && IsPageFree(page)) {
				if (runLength == 0) {
					runStart = page;
				}
				runLength++;
				continue;
			}
			// A used page (or the end of the window) closes the run that led up to it
			if (runLength >= pageCount && (bestLength == 0 || runLength < bestLength)) {
				bestPage = runStart;
				bestLength = runLength;
				if (runLength == pageCount) {
					break;		// An exact fit cannot be improved on
				}
			}
			runLength = 0;
		}

		if (bestPage == InvalidPage) {
			return InvalidPage;
		}
		MarkRange(bestPage, pageCount, true);
		return bestPage;
	}

	std::uint32_t GsVram::LargestFreeRunWithin(std::uint32_t windowFirstPage, std::uint32_t windowPageCount)
	{
		const std::uint32_t windowEndPage = windowFirstPage + windowPageCount;
		std::uint32_t longest = 0, runLength = 0;
		for (std::uint32_t page = windowFirstPage; page < windowEndPage; page++) {
			runLength = (IsPageFree(page) ? runLength + 1 : 0);
			if (runLength > longest) {
				longest = runLength;
			}
		}
		return longest;
	}

	std::uint32_t GsVram::AllocatePages(std::uint32_t pageCount)
	{
		if (!_initialized) {
			_failedAllocationCount++;
			return InvalidPage;
		}

		const std::uint32_t page = AllocateWithin(_cacheFirstPage, _cachePageCount, pageCount);
		if (page == InvalidPage) {
			_failedAllocationCount++;
			return InvalidPage;
		}

		_usedPageCount += pageCount;
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

		MarkRange(firstPage, pageCount, false);
		_usedPageCount -= (pageCount < _usedPageCount ? pageCount : _usedPageCount);
	}

	std::uint32_t GsVram::AllocateReservedPages(std::uint32_t pageCount)
	{
		if (!_initialized || pageCount == 0 || pageCount > _reservePageCount) {
			_failedAllocationCount++;
			return InvalidPage;
		}

		const std::uint32_t page = AllocateWithin(_reserveFirstPage, _reservePageCount, pageCount);
		if (page == InvalidPage) {
			_failedAllocationCount++;
		}
		return page;
	}

	void GsVram::FreeReservedPages(std::uint32_t firstPage, std::uint32_t pageCount)
	{
		if (firstPage == InvalidPage || pageCount == 0) {
			return;
		}
		// The reserve is outside the cache's usage accounting, so unlike FreePages() this only clears bits
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

	std::uint32_t GsVram::GetLargestFreeRun()
	{
		return (_initialized ? LargestFreeRunWithin(_cacheFirstPage, _cachePageCount) : 0);
	}

	std::uint32_t GsVram::GetFailedAllocationCount()
	{
		return _failedAllocationCount;
	}
}
