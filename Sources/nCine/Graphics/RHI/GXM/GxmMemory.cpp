#include "GxmMemory.h"

#include "../../../../Main.h"

#include <cstring>

namespace nCine::RHI::GXM
{
	namespace
	{
		// LPDDR blocks are handed out in 4 KB pages and CDRAM ones in 256 KB pages; the kernel rejects any
		// size that is not a multiple of the granularity of the requested type
		constexpr std::uint32_t LpddrPageSize = 4 * 1024;
		constexpr std::uint32_t CdramPageSize = 256 * 1024;

		std::uint32_t allocatedBytes_ = 0;

		// Render-target texels whose address is reserved for one geometry (see GxmMemory::AcquireSurface()). A
		// session uses a handful of sizes - the game's resolution, the levels of the blur chain, the display -
		// so a small fixed table covers every one of them, and the CDRAM a retired entry holds on to is the
		// price of the address staying meaningful.
		constexpr std::uint32_t MaxRetainedSurfaces = 16;

		struct RetainedSurface
		{
			std::uint32_t Stride = 0;
			std::uint32_t Height = 0;
			GxmMemory::Block Memory;
			bool InUse = false;
		};

		RetainedSurface retainedSurfaces_[MaxRetainedSurfaces];
		std::uint32_t retainedSurfaceCount_ = 0;

		inline std::uint32_t AlignUp(std::uint32_t value, std::uint32_t alignment)
		{
			return (value + alignment - 1) & ~(alignment - 1);
		}

		GxmMemory::Block AllocInternal(const char* name, std::uint32_t size, SceKernelMemBlockType type,
			std::uint32_t pageSize, SceGxmMemoryAttribFlags attribs)
		{
			GxmMemory::Block block;
			if (size == 0) {
				return block;
			}

			const std::uint32_t alignedSize = AlignUp(size, pageSize);
			SceUID uid = sceKernelAllocMemBlock(name, type, alignedSize, nullptr);
			if (uid < 0) {
				LOGE("sceKernelAllocMemBlock(\"{}\", {} bytes) failed with 0x{:.8x}", name, alignedSize, std::uint32_t(uid));
				return block;
			}

			void* base = nullptr;
			std::int32_t result = sceKernelGetMemBlockBase(uid, &base);
			if (result < 0 || base == nullptr) {
				LOGE("sceKernelGetMemBlockBase(\"{}\") failed with 0x{:.8x}", name, std::uint32_t(result));
				sceKernelFreeMemBlock(uid);
				return block;
			}

			result = sceGxmMapMemory(base, alignedSize, attribs);
			if (result < 0) {
				LOGE("sceGxmMapMemory(\"{}\", {} bytes) failed with 0x{:.8x}", name, alignedSize, std::uint32_t(result));
				sceKernelFreeMemBlock(uid);
				return block;
			}

			block.Uid = uid;
			block.Base = base;
			block.Size = alignedSize;
			allocatedBytes_ += alignedSize;
			return block;
		}
	}

	GxmMemory::Block GxmMemory::Alloc(const char* name, std::uint32_t size, SceGxmMemoryAttribFlags attribs)
	{
		return AllocInternal(name, size, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, LpddrPageSize, attribs);
	}

	GxmMemory::Block GxmMemory::AllocCdram(const char* name, std::uint32_t size, SceGxmMemoryAttribFlags attribs)
	{
		Block block = AllocInternal(name, size, SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, CdramPageSize, attribs);
		if (!block.IsValid()) {
			// CDRAM is only 128 MB and is not the only thing competing for it (the video decoder takes a share
			// on the real console), so a large off-screen target can legitimately fail to fit. LPDDR is slower
			// to render into but works, which is a much better outcome than refusing to create the target
			LOGW("CDRAM allocation of {} bytes for \"{}\" failed, falling back to main memory", size, name);
			block = Alloc(name, size, attribs);
		}
		return block;
	}

	GxmMemory::Block GxmMemory::AcquireSurface(const char* name, std::uint32_t stride, std::uint32_t height)
	{
		if (stride == 0 || height == 0) {
			return Block();
		}

		for (RetainedSurface& surface : retainedSurfaces_) {
			if (!surface.InUse && surface.Stride == stride && surface.Height == height) {
				surface.InUse = true;
				return surface.Memory;
			}
		}

		Block block = AllocCdram(name, stride * height, SCE_GXM_MEMORY_ATTRIB_RW);
		if (block.IsValid()) {
			if (retainedSurfaceCount_ < MaxRetainedSurfaces) {
				RetainedSurface& surface = retainedSurfaces_[retainedSurfaceCount_++];
				surface.Stride = stride;
				surface.Height = height;
				surface.Memory = block;
				surface.InUse = true;
			} else {
				// Out of slots to keep addresses stable in, so this one goes back to the allocator when it is
				// released - it may then be handed to a target of another size, with the consequences above
				LOGW("More than {} distinct render-target sizes were used; {}x{} is not address-stable",
					MaxRetainedSurfaces, stride / 4u, height);
			}
		}
		return block;
	}

	void GxmMemory::ReleaseSurface(Block& block)
	{
		if (!block.IsValid()) {
			return;
		}

		for (RetainedSurface& surface : retainedSurfaces_) {
			if (surface.Memory.Uid == block.Uid) {
				// Kept mapped and kept out of the allocator, so the next target of this geometry lands on the
				// very same address; the caller's handle goes away either way
				surface.InUse = false;
				block = Block();
				return;
			}
		}

		Free(block);
	}

	GxmMemory::Block GxmMemory::AllocVertexUsse(const char* name, std::uint32_t size)
	{
		Block block;
		const std::uint32_t alignedSize = AlignUp(size, LpddrPageSize);
		SceUID uid = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, alignedSize, nullptr);
		if (uid < 0) {
			LOGE("sceKernelAllocMemBlock(\"{}\", {} bytes) failed with 0x{:.8x}", name, alignedSize, std::uint32_t(uid));
			return block;
		}

		void* base = nullptr;
		if (sceKernelGetMemBlockBase(uid, &base) < 0 || base == nullptr) {
			sceKernelFreeMemBlock(uid);
			return block;
		}

		unsigned int usseOffset = 0;
		std::int32_t result = sceGxmMapVertexUsseMemory(base, alignedSize, &usseOffset);
		if (result < 0) {
			LOGE("sceGxmMapVertexUsseMemory(\"{}\") failed with 0x{:.8x}", name, std::uint32_t(result));
			sceKernelFreeMemBlock(uid);
			return block;
		}

		block.Uid = uid;
		block.Base = base;
		block.Size = alignedSize;
		block.UsseOffset = usseOffset;
		block.MappedAs = Kind::VertexUsse;
		allocatedBytes_ += alignedSize;
		return block;
	}

	GxmMemory::Block GxmMemory::AllocFragmentUsse(const char* name, std::uint32_t size)
	{
		Block block;
		const std::uint32_t alignedSize = AlignUp(size, LpddrPageSize);
		SceUID uid = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, alignedSize, nullptr);
		if (uid < 0) {
			LOGE("sceKernelAllocMemBlock(\"{}\", {} bytes) failed with 0x{:.8x}", name, alignedSize, std::uint32_t(uid));
			return block;
		}

		void* base = nullptr;
		if (sceKernelGetMemBlockBase(uid, &base) < 0 || base == nullptr) {
			sceKernelFreeMemBlock(uid);
			return block;
		}

		unsigned int usseOffset = 0;
		std::int32_t result = sceGxmMapFragmentUsseMemory(base, alignedSize, &usseOffset);
		if (result < 0) {
			LOGE("sceGxmMapFragmentUsseMemory(\"{}\") failed with 0x{:.8x}", name, std::uint32_t(result));
			sceKernelFreeMemBlock(uid);
			return block;
		}

		block.Uid = uid;
		block.Base = base;
		block.Size = alignedSize;
		block.UsseOffset = usseOffset;
		block.MappedAs = Kind::FragmentUsse;
		allocatedBytes_ += alignedSize;
		return block;
	}

	void GxmMemory::Free(Block& block)
	{
		if (block.Uid == InvalidUid) {
			return;
		}

		if (block.Base != nullptr) {
			// The unmap call has to match the one the block was mapped with
			switch (block.MappedAs) {
				case Kind::VertexUsse: sceGxmUnmapVertexUsseMemory(block.Base); break;
				case Kind::FragmentUsse: sceGxmUnmapFragmentUsseMemory(block.Base); break;
				default: sceGxmUnmapMemory(block.Base); break;
			}
		}

		sceKernelFreeMemBlock(block.Uid);
		if (allocatedBytes_ >= block.Size) {
			allocatedBytes_ -= block.Size;
		}

		block.Uid = InvalidUid;
		block.Base = nullptr;
		block.Size = 0;
		block.UsseOffset = 0;
		block.MappedAs = Kind::Mapped;
	}

	std::uint32_t GxmMemory::GetAllocatedBytes()
	{
		return allocatedBytes_;
	}
}
