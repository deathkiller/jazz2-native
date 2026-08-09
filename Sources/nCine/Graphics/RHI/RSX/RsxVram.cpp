#if defined(WITH_RHI_RSX)

#include "RsxVram.h"
#include "../../../../Main.h"

#include <rsx/rsx.h>
#include <rsx/mm.h>
#include <rsx/gcm_sys.h>
#include <sys/heap.h>

namespace nCine::RHI::RSX
{
	namespace RsxVram
	{
		namespace
		{
			/**
				@brief Suballocator for the GPU's IO window into main memory

				`rsxMemalign()` covers local memory, but PSL1GHT has no counterpart for the main-memory region
				`rsxInit()` mapped - the samples simply carve it up by hand. The same `heapInit()` allocator
				librsx uses for the local heap is used here so both sides behave identically (aligned
				allocation, real frees, no fragmentation surprises that one and not the other would have).
			*/
			heap_cntrl _mainHeap;
			bool _mainHeapReady = false;

			std::uint32_t _localAllocated = 0;
			std::uint32_t _mainAllocated = 0;

			/** @brief Smallest alignment the RSX accepts for anything it fetches by offset */
			constexpr std::uint32_t MinAlignment = 64;
			/**
				@brief Row-stride granularity of a surface

				Required by the display controller for a scan-out buffer, and what lets the GPU write a tile
				at the edge of a render target without a read-modify-write.
			*/
			constexpr std::uint32_t SurfacePitchAlignment = 64;
		}

		std::uint8_t Block::GetGcmLocation() const
		{
			return (Where == Location::Local ? GCM_LOCATION_RSX : GCM_LOCATION_CELL);
		}

		bool Initialize(void* mainHeapBase, std::uint32_t mainHeapSize)
		{
			if (mainHeapBase == nullptr || mainHeapSize == 0) {
				LOGE("Cannot initialize the main memory heap: no IO region was reserved");
				return false;
			}

			// The local heap belongs to the context and rsxInit() has already set it up; only the main-memory
			// side needs an allocator of its own
			heapInit(&_mainHeap, mainHeapBase, mainHeapSize);
			_mainHeapReady = true;
			_localAllocated = 0;
			_mainAllocated = 0;

			LOGI("RSX memory initialized: {} KB of main memory mapped into the GPU's IO window", mainHeapSize / 1024);
			return true;
		}

		void Shutdown()
		{
			// Nothing to release: the main heap is a view over memory the device owns, and the local heap goes
			// away with the context. Only the bookkeeping is reset, so a session that comes back up reports
			// its own usage rather than the previous one's.
			_mainHeapReady = false;
			_localAllocated = 0;
			_mainAllocated = 0;
		}

		Block Alloc(std::uint32_t size, std::uint32_t alignment, Location where)
		{
			Block block;
			if (size == 0) {
				return block;
			}
			if (alignment < MinAlignment) {
				alignment = MinAlignment;
			}

			void* base;
			if (where == Location::Local) {
				base = rsxMemalign(alignment, size);
			} else {
				base = (_mainHeapReady ? heapAllocateAligned(&_mainHeap, size, alignment) : nullptr);
			}
			if (base == nullptr) {
				LOGE("Cannot allocate {} bytes of {} memory", size, where == Location::Local ? "local" : "main");
				return block;
			}

			// The offset is what every RSX command actually takes; a block that has a base but no offset is
			// unusable, so the translation failing is treated as the allocation failing
			std::uint32_t offset = 0;
			if (rsxAddressToOffset(base, &offset) != 0) {
				// The format library has no void* formatter, so the address is reported as an integer
				LOGE("Cannot translate {} memory at 0x{:.8x} into an RSX offset",
					where == Location::Local ? "local" : "main",
					std::uint32_t(reinterpret_cast<std::uintptr_t>(base)));
				if (where == Location::Local) {
					rsxFree(base);
				} else {
					heapFree(&_mainHeap, base);
				}
				return block;
			}

			block.Base = base;
			block.Offset = offset;
			block.Size = size;
			block.Where = where;

			if (where == Location::Local) {
				_localAllocated += size;
			} else {
				_mainAllocated += size;
			}
			return block;
		}

		Block AllocSurface(std::uint32_t width, std::uint32_t height, std::uint32_t bytesPerPixel, std::uint32_t& pitchOut)
		{
			const std::uint32_t pitch = (width * bytesPerPixel + SurfacePitchAlignment - 1) & ~(SurfacePitchAlignment - 1);
			pitchOut = pitch;
			return Alloc(pitch * height, SurfacePitchAlignment, Location::Local);
		}

		Block AllocFragmentProgram(std::uint32_t size)
		{
			// Local is mandatory here rather than preferred - see the header
			return Alloc(size, MinAlignment, Location::Local);
		}

		void Free(Block& block)
		{
			if (block.Base == nullptr) {
				return;
			}

			if (block.Where == Location::Local) {
				rsxFree(block.Base);
				_localAllocated -= block.Size;
			} else if (_mainHeapReady) {
				heapFree(&_mainHeap, block.Base);
				_mainAllocated -= block.Size;
			}

			block.Base = nullptr;
			block.Offset = 0;
			block.Size = 0;
		}

		std::uint32_t GetLocalAllocatedBytes()
		{
			return _localAllocated;
		}

		std::uint32_t GetMainAllocatedBytes()
		{
			return _mainAllocated;
		}
	}
}

#endif
