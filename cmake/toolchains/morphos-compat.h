/** @file
	@brief Declarations the MorphOS SDK leaves to its inline headers, which this port does not use

	Force-included into every translation unit by `cmake/toolchains/morphos.cmake`, which builds with
	`_NO_PPCINLINE`: the SDK's `ppcinline/*.h` headers turn OS calls into function-like macros, and
	several of those names are ordinary C++ identifiers - `bind` is the fatal one, because `<unistd.h>`
	pulls in `proto/socket.h` and the macro then rewrites `std::bind` inside libstdc++'s `<functional>`.
	Skipping the inline headers costs this port nothing (it reaches the system through SDL2, never
	directly), but it also removes the declarations that some SDK headers rely on having seen -
	`<sys/select.h>`'s `FD_ZERO` expands to `bzero`, which lives in `<strings.h>`.
*/

#ifndef __JAZZ2_MORPHOS_COMPAT_H__
#define __JAZZ2_MORPHOS_COMPAT_H__

#include <strings.h>

/*
	exec's FindTask(), which `<unistd.h>` uses to define getpid(). It is declared here rather than by
	including the SDK's own prototypes, and deliberately with an opaque return type: the real one is
	`struct Task *`, and a global `Task` would make every unqualified `Task` in the engine ambiguous
	with its own coroutine type - which is why proto/exec.h is skipped in the first place. Only the
	pointer value is ever used, and it is cast to an integer at every call site.
*/
#ifdef __cplusplus
extern "C" void* FindTask(const char* name);
#else
void* FindTask(const char* name);
#endif

#endif
