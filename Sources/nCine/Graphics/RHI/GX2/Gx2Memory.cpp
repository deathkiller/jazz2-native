#if defined(WITH_RHI_GX2)

#include "Gx2Memory.h"

#include "../../../../Main.h"

#include <cstring>
#include <coreinit/memdefaultheap.h>
#include <gx2/mem.h>
#include <gx2/surface.h>
#include <gx2r/surface.h>

namespace nCine::RHI::GX2::Gx2Memory
{
	namespace
	{
		std::uint32_t _allocatedBytes = 0;
		std::uint32_t _mem1Bytes = 0;

		// Cafe OS maps MEM1 at this virtual range
		constexpr std::uintptr_t Mem1Begin = 0xF4000000u;
		constexpr std::uintptr_t Mem1End = 0xF6000000u;
	}

	Block Alloc(std::uint32_t size, std::uint32_t alignment)
	{
		Block block;
		if (size == 0) {
			return block;
		}
		if (alignment < 64) {
			alignment = 64;
		}
		block.Base = MEMAllocFromDefaultHeapEx(size, int(alignment));
		if (block.Base != nullptr) {
			block.Size = size;
			_allocatedBytes += size;
		}
		return block;
	}

	Block AllocColorSurfaceMem1(const GX2Surface& surface)
	{
		// SDL's GX2R allocator serves colour buffers from its MEM1 heap (and returns nothing when it is full, it
		// doesn't fall back to MEM2). A copy of the surface is passed, so the caller's keeps no GX2R state.
		GX2Surface copy = surface;
		copy.image = nullptr;
		copy.mipmaps = nullptr;
		const GX2RResourceFlags flags = GX2RResourceFlags(GX2R_RESOURCE_BIND_COLOR_BUFFER | GX2R_RESOURCE_BIND_TEXTURE |
			GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ | GX2R_RESOURCE_USAGE_GPU_WRITE);
		Block block;
		if (!GX2RCreateSurface(&copy, flags) || copy.image == nullptr) {
			LOGI("No room in MEM1 for a {}x{} surface ({} bytes, {} in use), using MEM2", surface.width, surface.height,
				surface.imageSize, _mem1Bytes);
			return block;
		}
		block.Base = copy.image;
		block.Size = copy.imageSize;
		// GX2RDestroySurfaceEx() needs the flags GX2R stored, which include internal bits
		block.Mem1Flags = std::uint32_t(copy.resourceFlags != 0 ? copy.resourceFlags : flags);
		_mem1Bytes += block.Size;

		const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(block.Base);
		if (address < Mem1Begin || address >= Mem1End) {
			LOGW("A {}x{} colour surface for MEM1 was allocated at 0x{:.8x}, outside MEM1", surface.width, surface.height, address);
		} else {
			LOGI("MEM1: {}x{} surface, {} bytes ({} in use)", surface.width, surface.height, block.Size, _mem1Bytes);
		}
		return block;
	}

	void Free(Block& block)
	{
		if (block.Base != nullptr && block.Mem1Flags != 0) {
			// GX2R frees through SDL's allocator, which only needs the image pointer and the flags
			GX2Surface surface;
			std::memset(&surface, 0, sizeof(surface));
			surface.image = block.Base;
			surface.imageSize = block.Size;
			surface.resourceFlags = GX2RResourceFlags(block.Mem1Flags);
			GX2RDestroySurfaceEx(&surface, GX2RResourceFlags(0));
			_mem1Bytes -= block.Size;
		} else if (block.Base != nullptr) {
			MEMFreeToDefaultHeap(block.Base);
			_allocatedBytes -= block.Size;
		}
		block = Block{};
	}

	void FlushCpuWrites(const void* base, std::uint32_t size, std::uint32_t mode)
	{
		if (base != nullptr && size != 0) {
			GX2Invalidate(GX2InvalidateMode(mode | GX2_INVALIDATE_MODE_CPU), const_cast<void*>(base), size);
		}
	}

	std::uint32_t GetAllocatedBytes()
	{
		return _allocatedBytes;
	}

	std::uint32_t GetMem1Bytes()
	{
		return _mem1Bytes;
	}
}

#endif
