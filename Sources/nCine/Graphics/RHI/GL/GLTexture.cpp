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

namespace nCine::RhiGL
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
		GLint internalFormat;
		GLenum externalFormat, dataType;
		GLTextureFormat::Resolve(format, false, internalFormat, externalFormat, dataType);
		glCompressedTexSubImage2D(target_, level, xoffset, yoffset, width, height, internalFormat, imageSize, data);
		GL_LOG_ERRORS();
	}

	void GLTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		TracyGpuZone("glTexStorage2D");
		GLTextureFormat::CheckSupport(format);
		Bind();
		GLint internalFormat;
		GLenum externalFormat, dataType;
		GLTextureFormat::Resolve(format, false, internalFormat, externalFormat, dataType);
		glTexStorage2D(target_, levels, internalFormat, width, height);
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
		// Channels are set individually because GL_TEXTURE_SWIZZLE_RGBA (a single glTexParameteriv) is desktop-only
		// and absent on GLES/WebGL. Requires GL 3.3+ / GLES 3.0+.
		TexParameteri(GL_TEXTURE_SWIZZLE_R, SwizzleChannelToGL(r));
		TexParameteri(GL_TEXTURE_SWIZZLE_G, SwizzleChannelToGL(g));
		TexParameteri(GL_TEXTURE_SWIZZLE_B, SwizzleChannelToGL(b));
		TexParameteri(GL_TEXTURE_SWIZZLE_A, SwizzleChannelToGL(a));
	}

	void GLTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		TexParameteri(GL_TEXTURE_MAX_LEVEL, maxLevel);
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
#if (defined(WITH_OPENGLES) && GL_ES_VERSION_3_0) || defined(DEATH_TARGET_EMSCRIPTEN)
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
