#pragma once

#if defined(WITH_RHI_LEGACYGL) || defined(DOXYGEN_GENERATING_OUTPUT)

/** @file
	@brief The OpenGL 1.x entry points the fixed-function GL backend draws with

	Separate from `nCine/CommonHeaders.h`, which sets up the *shader* GL backend: this one wants the
	legacy headers and nothing else. Three hosts matter today - MorphOS, where OpenGL is TinyGL and the
	entry points are the SDK's inline macros; AmigaOS 4, where it is MiniGL on top of Warp3D; and a
	desktop GL, which is where both of those are developed and looked at.

	They do not agree on how much of OpenGL exists, so what is optional is stated here rather than
	assumed: `RHI_LEGACYGL_HAS_FBO` says whether the framebuffer-object entry points can even be
	CALLED (MiniGL exports none, so a call would not link), and where they can, the backend still
	probes at run time whether they work.
*/

// The rest of the file is the include dance and a set of enumerant fallbacks - build plumbing with no
// API of its own, which is why the documentation does not list it
#if !defined(DOXYGEN_GENERATING_OUTPUT)

#if defined(DEATH_TARGET_AMIGAOS4)
	// AmigaOS 4 reaches Warp3D through MiniGL, whose headers are the system ones - which is also what
	// the SDK's own SDL_opengl.h includes on this platform. `<GL/glext.h>` is deliberately NOT included:
	// it declares entry points MiniGL does not export (the framebuffer objects among them), and a
	// declaration that cannot link is worse than the enumerant fallbacks below.
#	include <GL/gl.h>
#elif defined(DEATH_TARGET_MORPHOS)
	// MorphOS reaches tinygl.library through the SDK's ppcinline macros, which the port suppresses
	// globally with _NO_PPCINLINE (its `bind` macro rewrites std::bind inside libstdc++, see
	// cmake/toolchains/morphos.cmake). Undoing it for this one header is safe: nothing else in the
	// TinyGL proto chain declares a name the engine uses.
#	undef _NO_PPCINLINE
#	include <GL/gl.h>
	// Put it back: the call macros this header just defined stay defined (which is the point - every
	// `glXxx` below is one of them, dispatching through TinyGL's `__tglContext`, and libGL.a exports
	// nothing under the plain names), while any SDK header included after this one is again read
	// without its own inline macros.
#	define _NO_PPCINLINE 1
#elif defined(WITH_GLEW)
#	define GLEW_NO_GLU
#	include <GL/glew.h>
#elif defined(DEATH_TARGET_APPLE)
#	define GL_SILENCE_DEPRECATION
#	include <OpenGL/gl.h>
#	include <OpenGL/glext.h>
#else
#	define GL_GLEXT_PROTOTYPES
#	include <GL/gl.h>
#	include <GL/glext.h>
#endif

// Names this backend uses that a strictly-1.1 header may not carry. Every one of them is a plain
// enumerant of the 1.2/1.3 texture-combiner and clamping vocabulary, so defining the value where the
// header is older is exactly equivalent to having a newer header - the driver is what has to support
// them, and both hosts here do.
#if !defined(GL_CLAMP_TO_EDGE)
#	define GL_CLAMP_TO_EDGE 0x812F
#endif
#if !defined(GL_COMBINE)
#	define GL_COMBINE 0x8570
#	define GL_COMBINE_RGB 0x8571
#	define GL_COMBINE_ALPHA 0x8572
#	define GL_SOURCE0_RGB 0x8580
#	define GL_SOURCE1_RGB 0x8581
#	define GL_SOURCE2_RGB 0x8582
#	define GL_SOURCE0_ALPHA 0x8588
#	define GL_SOURCE1_ALPHA 0x8589
#	define GL_SOURCE2_ALPHA 0x858A
#	define GL_OPERAND0_RGB 0x8590
#	define GL_OPERAND1_RGB 0x8591
#	define GL_OPERAND2_RGB 0x8592
#	define GL_OPERAND0_ALPHA 0x8598
#	define GL_OPERAND1_ALPHA 0x8599
#	define GL_OPERAND2_ALPHA 0x859A
#	define GL_RGB_SCALE 0x8573
#	define GL_ALPHA_SCALE 0x0D1C
#	define GL_CONSTANT 0x8576
#	define GL_PRIMARY_COLOR 0x8577
#	define GL_PREVIOUS 0x8578
#	define GL_INTERPOLATE 0x8575
#	define GL_SUBTRACT 0x84E7
#endif
#if !defined(GL_TEXTURE0)
#	define GL_TEXTURE0 0x84C0
#endif

// Mirrored repeat is GL 1.4, and a driver that has neither it nor the ARB extension answers a wrap of
// 0x8370 with GL_INVALID_ENUM - which leaves the sampler set to whatever it was, a silent wrong wrap.
// So this is NOT filled in with the enumerant where the header lacks it: it degrades to plain repeat,
// which is what a mirrored one looks like for the content this backend draws (MiniGL is the case -
// its own headers carry the value, its README claims no support for it).
#if defined(GL_MIRRORED_REPEAT) && !defined(DEATH_TARGET_AMIGAOS4)
#	define RHI_LEGACYGL_MIRRORED_REPEAT GL_MIRRORED_REPEAT
#else
#	define RHI_LEGACYGL_MIRRORED_REPEAT GL_REPEAT
#endif
// The values above are the ARB/EXT ones unchanged - MiniGL's header, for instance, carries the whole
// set under its `_EXT` names only (GL_COMBINE_EXT is 0x8570 like GL_COMBINE), and MiniGL advertises
// GL_EXT_texture_env_combine, so defining the unsuffixed spellings here is exactly equivalent

// Framebuffer objects, which are how a render target is drawn into where they exist at all. MiniGL
// exports none of the entry points, so on AmigaOS 4 the whole path is compiled out and a render target
// is always drawn into the back buffer and copied into its texture (a GL 1.1 operation - see
// LegacyGlRenderTarget). Everywhere else they are declared, but still not assumed to WORK: TinyGL
// declares them unconditionally and the running library may answer nothing, so the backend probes them
// at run time as well (`LegacyGlDevice::SupportsFramebufferObjects()`).
#if !defined(DEATH_TARGET_AMIGAOS4)
#	define RHI_LEGACYGL_HAS_FBO
#	if !defined(GL_FRAMEBUFFER)
#		define GL_FRAMEBUFFER 0x8D40
#		define GL_COLOR_ATTACHMENT0 0x8CE0
#		define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#	endif
#endif

#endif	// !DOXYGEN_GENERATING_OUTPUT

#endif
