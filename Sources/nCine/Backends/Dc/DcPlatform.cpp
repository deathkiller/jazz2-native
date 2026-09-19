#include "DcPlatform.h"

#if defined(DEATH_TARGET_DREAMCAST)

#include "../../../Main.h"

#include <malloc.h>

#include <arch/arch.h>
#include <arch/stack.h>
#include <dc/pvr.h>
#include <dc/sound/sound.h>

// The linker places this symbol right after the last loaded section, which is where KallistiOS starts
// handing out heap (see kernel/mm/mm.c), so the distance from it to the top of memory is the whole
// window the heap can ever grow into
extern "C" char end[];

namespace nCine::Backends
{
	void DcPlatform::LogMemoryStatus(const char* reason)
	{
		// `uordblks` is what the allocator has actually handed out, `arena` what it has taken from the
		// window with sbrk(): the gap between them is free-list it kept, which a large request can still
		// fail to fit into, and what is left of the window is the only part it can still grow into.
		// The kernel stack sits at the very top and sbrk() refuses to grow into it.
		const struct ::mallinfo info = ::mallinfo();
		const std::uintptr_t heapBase = reinterpret_cast<std::uintptr_t>(end);
		const std::uintptr_t heapTop = std::uintptr_t(_arch_mem_top) - THD_KERNEL_STACK_SIZE;
		const std::uint32_t heapWindow = std::uint32_t(heapTop > heapBase ? heapTop - heapBase : 0);
		const std::uint32_t arena = std::uint32_t(info.arena);

		LOGI("Memory ({}): heap {} KB used of {} KB ({} KB reserved by the allocator, {} KB never touched), "
			"VRAM {} KB free, sound RAM {} KB free", reason, std::uint32_t(info.uordblks) / 1024,
			heapWindow / 1024, arena / 1024, (heapWindow > arena ? heapWindow - arena : 0) / 1024,
			std::uint32_t(pvr_mem_available()) / 1024, snd_mem_available() / 1024);
	}
}

#endif
