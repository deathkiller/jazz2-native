#pragma once

// Compile-compatibility shims for building the GL backend against real OpenGL ES 2.0 headers
// (`GLES2/gl2.h` + `GLES2/gl2ext.h`), the way a PlayStation Vita (vitaGL) or any strict ES 2.0
// target would. Included from CommonHeaders.h only on the strict-ES2 header path, i.e. when
// `RHI_GL_PROFILE_ES2` is active AND the build compiles against gl2.h rather than GLES3/gl3.h.
// It is NOT included on the vitaGL path (vitaGL declares its own supersets natively), so these
// definitions never collide there.
//
// Every macro below is the canonical Khronos numeric value of an enum that strict ES 2.0 does not
// provide but that vitaGL / GLES3 / desktop GL do. They exist ONLY so that code paths which are
// statically compiled under the profile yet never executed on it - shared reflection switch tables
// (uint-vector / 3D-sampler GL types that ES2 never reports), `static_assert`s that pin the engine's
// enum values, dead uniform-buffer / read-draw-framebuffer branches, and unreachable format cases -
// keep compiling. None of these values is ever passed to a strict ES 2.0 driver while the profile is
// active. Prefer eliminating a reference with `#if` in the referencing code over adding one here;
// entries remain only where the reference genuinely has to stay compiled.

#if !defined(RHI_GL_PROFILE_ES2)
#	error GLEs2HeaderShims.h must only be included when RHI_GL_PROFILE_ES2 is defined
#endif

// Uniform buffer objects (ES 3.0): the target enum plus the invalid-block-index sentinel and the
// glMapBufferRange access bits, referenced by static_asserts (GLDevice), the buffer-target hash map
// (GLHashMap) and the reflection block index (GLShaderProgram). No UBO is ever bound on this profile.
#if !defined(GL_UNIFORM_BUFFER)
#	define GL_UNIFORM_BUFFER 0x8A11
#endif
#if !defined(GL_INVALID_INDEX)
#	define GL_INVALID_INDEX 0xFFFFFFFFu
#endif
#if !defined(GL_MAP_INVALIDATE_RANGE_BIT)
#	define GL_MAP_INVALIDATE_RANGE_BIT 0x0004
#endif
#if !defined(GL_MAP_INVALIDATE_BUFFER_BIT)
#	define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif
#if !defined(GL_MAP_FLUSH_EXPLICIT_BIT)
#	define GL_MAP_FLUSH_EXPLICIT_BIT 0x0010
#endif
#if !defined(GL_MAP_UNSYNCHRONIZED_BIT)
#	define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
#endif

// Reflection GL type enums that ES 2.0 never reports (unsigned-integer vectors and 3D samplers are
// ES 3.0). They only appear as `case` labels in the GL-type -> engine-type switch tables shared by
// every profile (GLAttribute / GLUniform / GLUniformCache / GLShaderProgram).
#if !defined(GL_UNSIGNED_INT_VEC2)
#	define GL_UNSIGNED_INT_VEC2 0x8DC6
#endif
#if !defined(GL_UNSIGNED_INT_VEC3)
#	define GL_UNSIGNED_INT_VEC3 0x8DC7
#endif
#if !defined(GL_UNSIGNED_INT_VEC4)
#	define GL_UNSIGNED_INT_VEC4 0x8DC8
#endif
#if !defined(GL_SAMPLER_3D)
#	define GL_SAMPLER_3D 0x8B5F
#endif

// Separate read/draw framebuffer bind points (ES 3.0). GLFramebuffer::BindHandle remaps every request
// to the single ES2 GL_FRAMEBUFFER target, so these only appear in the always-false branches and the
// FATAL_ASSERTs that validate the caller-supplied target enum.
#if !defined(GL_READ_FRAMEBUFFER)
#	define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#if !defined(GL_DRAW_FRAMEBUFFER)
#	define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif

// Sized floating-point internal formats (ES 3.0 / EXT_color_buffer_(half_)float). Referenced by the
// shared format-resolution switch (GLTextureFormat); the ES2 profile only ever resolves the unsized
// ES2-core formats in the profile-specific branch above these cases.
#if !defined(GL_RGBA16F)
#	define GL_RGBA16F 0x881A
#endif
#if !defined(GL_RGBA32F)
#	define GL_RGBA32F 0x8814
#endif
#if !defined(GL_RGB16F)
#	define GL_RGB16F 0x881B
#endif
#if !defined(GL_RGB32F)
#	define GL_RGB32F 0x8815
#endif
#if !defined(GL_DEPTH_COMPONENT32F)
#	define GL_DEPTH_COMPONENT32F 0x8CAC
#endif

// ETC2 / EAC compressed internal formats and the GL_RED / GL_RG channel enums used by their EAC cases
// (all ES 3.0). Unreachable on the ES2 profile (the engine uploads none of these), but present as
// `case` labels in the shared compressed-format switch (GLTextureFormat).
#if !defined(GL_COMPRESSED_RGBA8_ETC2_EAC)
#	define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#endif
#if !defined(GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2)
#	define GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 0x9276
#endif
#if !defined(GL_COMPRESSED_RGB8_ETC2)
#	define GL_COMPRESSED_RGB8_ETC2 0x9274
#endif
#if !defined(GL_COMPRESSED_RG11_EAC)
#	define GL_COMPRESSED_RG11_EAC 0x9272
#endif
#if !defined(GL_COMPRESSED_R11_EAC)
#	define GL_COMPRESSED_R11_EAC 0x9270
#endif
#if !defined(GL_RED)
#	define GL_RED 0x1903
#endif
#if !defined(GL_RG)
#	define GL_RG 0x8227
#endif

// Core program-binary format queries (ES 3.0 / ARB_get_program_binary). The ES2 profile uses the OES
// spelling; these appear only in the ARB fallback branch of GfxCapabilities, never taken on ES2.
#if !defined(GL_NUM_PROGRAM_BINARY_FORMATS)
#	define GL_NUM_PROGRAM_BINARY_FORMATS 0x87FE
#endif
#if !defined(GL_PROGRAM_BINARY_FORMATS)
#	define GL_PROGRAM_BINARY_FORMATS 0x87FF
#endif
