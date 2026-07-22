#include "GLTexture.h"
#include "GLTextureFormat.h"
#include "GLDebug.h"
#include "../../../ServiceLocator.h"
#include "../../../tracy_opengl.h"

#ifndef GL_TEXTURE_SWIZZLE_R
#	define GL_TEXTURE_SWIZZLE_R 0x8E42
#endif
#ifndef GL_TEXTURE_SWIZZLE_G
#	define GL_TEXTURE_SWIZZLE_G 0x8E43
#endif
#ifndef GL_TEXTURE_SWIZZLE_B
#	define GL_TEXTURE_SWIZZLE_B 0x8E44
#endif
#ifndef GL_TEXTURE_SWIZZLE_A
#	define GL_TEXTURE_SWIZZLE_A 0x8E45
#endif

namespace nCine::RHI::GL
{
	namespace
	{
		GLenum TextureTargetToGL(TextureTarget target)
		{
			switch (target) {
				default:
				case TextureTarget::Texture2D:
					return GL_TEXTURE_2D;
			}
		}

		GLenum SamplerFilterToGLMin(SamplerFilter filter)
		{
			// clang-format off
			switch (filter) {
				case SamplerFilter::Nearest:				return GL_NEAREST;
				case SamplerFilter::Linear:					return GL_LINEAR;
				case SamplerFilter::NearestMipmapNearest:	return GL_NEAREST_MIPMAP_NEAREST;
				case SamplerFilter::LinearMipmapNearest:	return GL_LINEAR_MIPMAP_NEAREST;
				case SamplerFilter::NearestMipmapLinear:	return GL_NEAREST_MIPMAP_LINEAR;
				case SamplerFilter::LinearMipmapLinear:		return GL_LINEAR_MIPMAP_LINEAR;
				default:									return GL_NEAREST;
			}
			// clang-format on
		}

		GLenum SamplerFilterToGLMag(SamplerFilter filter)
		{
			// clang-format off
			switch (filter) {
				case SamplerFilter::Nearest:	return GL_NEAREST;
				case SamplerFilter::Linear:		return GL_LINEAR;
				default:					return GL_NEAREST;
			}
			// clang-format on
		}

		GLenum SamplerWrappingToGL(SamplerWrapping wrap)
		{
			// clang-format off
			switch (wrap) {
				default:
				case SamplerWrapping::ClampToEdge:		return GL_CLAMP_TO_EDGE;
				case SamplerWrapping::MirroredRepeat:	return GL_MIRRORED_REPEAT;
				case SamplerWrapping::Repeat:			return GL_REPEAT;
			}
			// clang-format on
		}

#if !defined(RHI_GL_PROFILE_ES2)
		GLint SwizzleChannelToGL(SwizzleChannel channel)
		{
			switch (channel) {
				default:
				case SwizzleChannel::Red:	return GL_RED;
				case SwizzleChannel::Green:	return GL_GREEN;
				case SwizzleChannel::Blue:	return GL_BLUE;
				case SwizzleChannel::Alpha:	return GL_ALPHA;
				case SwizzleChannel::Zero:	return GL_ZERO;
				case SwizzleChannel::One:	return GL_ONE;
			}
		}
#endif
	}

	GLHashMap<GLTextureMappingFunc::Size, GLTextureMappingFunc> GLTexture::boundTextures_[MaxTextureUnits];
	std::uint32_t GLTexture::boundUnit_ = 0;

	GLTexture::GLTexture(TextureTarget target)
		: glHandle_(0), target_(TextureTargetToGL(target)), textureUnit_(0)
	{
		glGenTextures(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	GLTexture::~GLTexture()
	{
		// Remove this texture from the bound-texture tracking on every unit it may be bound to, not just the
		// currently active one - otherwise the freed handle lingers in the cache and a later texture that reuses
		// the same GL handle would be considered already bound (its bind skipped -> wrong texture sampled).
		for (std::uint32_t textureUnit = 0; textureUnit < MaxTextureUnits; textureUnit++) {
			if (boundTextures_[textureUnit][target_] == glHandle_) {
				BindHandle(target_, 0, textureUnit);
			}
		}

		glDeleteTextures(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	bool GLTexture::Bind(std::uint32_t textureUnit) const
	{
		const bool hasBound = BindHandle(target_, glHandle_, textureUnit);
		if (hasBound) {
			textureUnit_ = textureUnit;
		}
		return hasBound;
	}

	bool GLTexture::Unbind() const
	{
		return BindHandle(target_, 0, textureUnit_);
	}

	bool GLTexture::Unbind(GLenum target, std::uint32_t textureUnit)
	{
		return BindHandle(target, 0, textureUnit);
	}

	bool GLTexture::Unbind(std::uint32_t textureUnit)
	{
		return BindHandle(GL_TEXTURE_2D, 0, textureUnit);
	}

	void GLTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
	{
		TracyGpuZone("glTexImage2D");
		GLTextureFormat::CheckSupport(format);
		Bind();
		GLint internalFormat;
		GLenum externalFormat, dataType;
		GLTextureFormat::Resolve(format, bgr, internalFormat, externalFormat, dataType);
		glTexImage2D(target_, level, internalFormat, width, height, 0, externalFormat, dataType, data);
		GL_LOG_ERRORS();
	}

	void GLTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		TracyGpuZone("glTexSubImage2D");
		Bind();
		GLint internalFormat;
		GLenum externalFormat, dataType;
		GLTextureFormat::Resolve(format, bgr, internalFormat, externalFormat, dataType);
		glTexSubImage2D(target_, level, xoffset, yoffset, width, height, externalFormat, dataType, data);
		GL_LOG_ERRORS();
	}

	void GLTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		TracyGpuZone("glCompressedTexImage2D");
		GLTextureFormat::CheckSupport(format);
		Bind();
		GLint internalFormat;
		GLenum externalFormat, dataType;
		GLTextureFormat::Resolve(format, false, internalFormat, externalFormat, dataType);
		glCompressedTexImage2D(target_, level, internalFormat, width, height, 0, imageSize, data);
		GL_LOG_ERRORS();
	}

	void GLTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
	{
		TracyGpuZone("glCompressedTexSubImage2D");
		GLTextureFormat::CheckSupport(format);
		Bind();
#if !defined(DEATH_TARGET_VITA)
		// vitaGL provides no glCompressedTexSubImage2D(), compressed-texture sub-uploads are unsupported there
		GLint internalFormat;
		GLenum externalFormat, dataType;
		GLTextureFormat::Resolve(format, false, internalFormat, externalFormat, dataType);
		glCompressedTexSubImage2D(target_, level, xoffset, yoffset, width, height, internalFormat, imageSize, data);
#endif
		GL_LOG_ERRORS();
	}

	void GLTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		TracyGpuZone("glTexStorage2D");
		GLTextureFormat::CheckSupport(format);
		Bind();
#if !defined(RHI_GL_PROFILE_ES2)
		// glTexStorage2D() is ES 3.0 (and strict ES 2.0 headers such as vitaGL's declare none of it); the ES2
		// profile reports SupportsImmutableStorage()==false and allocates textures via glTexImage2D instead,
		// so this immutable-storage path is never reached here
		GLint internalFormat;
		GLenum externalFormat, dataType;
		GLTextureFormat::Resolve(format, false, internalFormat, externalFormat, dataType);
		glTexStorage2D(target_, levels, internalFormat, width, height);
#else
		static_cast<void>(levels);
		static_cast<void>(width);
		static_cast<void>(height);
#endif
		GL_LOG_ERRORS();
	}

	void GLTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
#if !defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN)
		TracyGpuZone("glGetTexImage");
		Bind();
		GLint internalFormat;
		GLenum externalFormat, dataType;
		GLTextureFormat::Resolve(format, bgr, internalFormat, externalFormat, dataType);
		glGetTexImage(target_, level, externalFormat, dataType, pixels);
		GL_LOG_ERRORS();
#endif
	}

	void GLTexture::SetMinFiltering(SamplerFilter filter)
	{
		TexParameteri(GL_TEXTURE_MIN_FILTER, SamplerFilterToGLMin(filter));
	}

	void GLTexture::SetMagFiltering(SamplerFilter filter)
	{
		TexParameteri(GL_TEXTURE_MAG_FILTER, SamplerFilterToGLMag(filter));
	}

	void GLTexture::SetWrap(SamplerWrapping wrap)
	{
		const GLenum glWrap = SamplerWrappingToGL(wrap);
		TexParameteri(GL_TEXTURE_WRAP_S, glWrap);
		TexParameteri(GL_TEXTURE_WRAP_T, glWrap);
	}

	void GLTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// ES2 has no GL_TEXTURE_SWIZZLE_* (ES 3.0 feature). The only non-identity swizzle the engine uses is the
		// palette pipeline's (R,G,B,G) on RG8 index textures, and GLTextureFormat maps RG8 to LUMINANCE_ALPHA on
		// this profile, which natively samples (L,L,L,A) - identical for the channels the shaders read (.r/.a)
		static_cast<void>(r); static_cast<void>(g); static_cast<void>(b); static_cast<void>(a);
#else
		// Channels are set individually because GL_TEXTURE_SWIZZLE_RGBA (a single glTexParameteriv) is desktop-only
		// and absent on GLES/WebGL. Requires GL 3.3+ / GLES 3.0+.
		TexParameteri(GL_TEXTURE_SWIZZLE_R, SwizzleChannelToGL(r));
		TexParameteri(GL_TEXTURE_SWIZZLE_G, SwizzleChannelToGL(g));
		TexParameteri(GL_TEXTURE_SWIZZLE_B, SwizzleChannelToGL(b));
		TexParameteri(GL_TEXTURE_SWIZZLE_A, SwizzleChannelToGL(a));
#endif
	}

	void GLTexture::SetMaxLevel(std::int32_t maxLevel)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// ES2 has no GL_TEXTURE_MAX_LEVEL (ES 3.0 feature); an incomplete MIP chain sampled with a mipmap filter
		// would still be incomplete, but the engine only sets this for multi-level textures it fully uploads
		static_cast<void>(maxLevel);
#else
		TexParameteri(GL_TEXTURE_MAX_LEVEL, maxLevel);
#endif
	}

	void GLTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
	}

	void GLTexture::TexParameterf(GLenum pname, GLfloat param)
	{
		Bind();
		glTexParameterf(target_, pname, param);
		GL_LOG_ERRORS();
	}

	void GLTexture::TexParameteri(GLenum pname, GLint param)
	{
		Bind();
		glTexParameteri(target_, pname, param);
		GL_LOG_ERRORS();
	}

	void GLTexture::SetObjectLabel(StringView label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::Texture, glHandle_, label);
	}

	bool GLTexture::SupportsImmutableStorage()
	{
#if defined(RHI_GL_PROFILE_ES2)
		// glTexStorage2D() is ES 3.0 (the GL_ES_VERSION_3_0 check below matches the GLES3 *headers* this build
		// compiles against, not the runtime context); EXT_texture_storage is not assumed - use the mutable
		// glTexImage2D() per-level fallback instead
		return false;
#elif (defined(WITH_OPENGLES) && GL_ES_VERSION_3_0) || defined(DEATH_TARGET_EMSCRIPTEN)
		return true;
#else
		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
		return gfxCaps.HasExtension(IGfxCapabilities::Extensions::ARB_TEXTURE_STORAGE);
#endif
	}

	bool GLTexture::SupportsTextureReadback()
	{
#if !defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN)
		return true;
#else
		return false;
#endif
	}

	void GLTexture::ClearErrors()
	{
		glGetError();
	}

	bool GLTexture::CheckErrors()
	{
		return (glGetError() != GL_NO_ERROR);
	}

	void GLTexture::CheckFormatSupport(PixelFormat format)
	{
		GLTextureFormat::CheckSupport(format);
	}

	bool GLTexture::BindHandle(GLenum target, GLuint glHandle, std::uint32_t textureUnit)
	{
		FATAL_ASSERT(textureUnit < MaxTextureUnits);

		if (boundTextures_[textureUnit][target] != glHandle) {
			if (boundUnit_ != textureUnit) {
				glActiveTexture(GL_TEXTURE0 + textureUnit);
				boundUnit_ = textureUnit;
			}

			glBindTexture(target, glHandle);
			GL_LOG_ERRORS();
			boundTextures_[textureUnit][target] = glHandle;
			return true;
		}
		return false;
	}

	GLuint GLTexture::GetBoundHandle(GLenum target, unsigned int textureUnit)
	{
		FATAL_ASSERT(textureUnit < MaxTextureUnits);
		return boundTextures_[textureUnit][target];
	}
}
