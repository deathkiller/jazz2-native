/*
	Binds the process to MorphOS' TinyGL, which is what the legacy GL backend draws with there.

	Two globals stand between an application and tinygl.library, and the SDK's `gl*` macros use both:
	`TinyGLBase` (the library the call vectors are read from) and `__tglContext` (the context every call
	is made against - the macros expand to `GLFoo(__tglContext, ...)`). Both live in the program rather
	than in a library, so filling them in is the program's job:

	- `TinyGLBase` is opened by the SDK's own initializer, which libGL.a carries. Nothing would normally
	  pull that object in here - SDL2's static glue declares the same global as a common symbol, which
	  satisfies the reference without it - so the build asks for its constructor by name instead (see the
	  MorphOS arm of cmake/ncine_extra_sources.cmake). It opens the library before `main()` and closes it
	  after, which is also where the auto-context below comes from.

	- `__tglContext` is set here, to the context SDL created for the window. That IS the TinyGL context on
	  this platform - it is why SDL's own `SDL_GL_GetProcAddress` reads this very global - and it is the
	  one attached to the window, unlike the context the initializer above makes on its own.
*/

#if defined(__MORPHOS__) && defined(WITH_RHI_LEGACYGL)

// The build suppresses the SDK's inline macros globally (its `bind` breaks std::bind, see
// cmake/toolchains/morphos.cmake); the `gl*` names ARE those macros, so this file undoes it for itself.
// Nothing of the engine is included here, which is what keeps that safe.
#undef _NO_PPCINLINE
#include <GL/gl.h>

extern "C"
{
	/** @brief The context every `gl*` call is made against; SDL's glue reads it too */
	GLContext* __tglContext;
}

namespace nCine::Backends
{
	bool MorphOsAttachTinyGl(void* glContext)
	{
		if (glContext != nullptr) {
			__tglContext = static_cast<GLContext*>(glContext);
		}
		// A null one leaves the initializer's auto-context in place rather than clearing it, so a caller
		// that has no context of its own still gets something that can be called
		return (TinyGLBase != nullptr && __tglContext != nullptr);
	}

	void MorphOsDetachTinyGl()
	{
		// The context belongs to SDL, which destroys it with the window; the library reference belongs to
		// the initializer, whose destructor releases it (together with the auto-context) after main()
		__tglContext = nullptr;
	}
}

#endif
