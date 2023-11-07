#if defined(WITH_TRACY)
#include "tracy/Tracy.hpp"

#if !defined(OVERRIDE_NEW)
void* operator new(std::size_t count)
{
	auto* ptr = malloc(count);
	TracyAllocS(ptr, count, 5);
	return ptr;
}

void operator delete(void* ptr) noexcept
{
	TracyFreeS(ptr, 5);
	free(ptr);
}

void* operator new[](std::size_t count)
{
	auto* ptr = malloc(count);
	TracyAllocS(ptr, count, 5);
	return ptr;
}

void operator delete[](void* ptr) noexcept
{
	TracyFreeS(ptr, 5);
	free(ptr);
}
#endif
#endif