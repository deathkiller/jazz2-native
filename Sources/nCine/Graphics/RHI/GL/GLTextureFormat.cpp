#include "GLTextureFormat.h"
#include "../../../ServiceLocator.h"

#if defined(DEATH_TARGET_ANDROID)
#	include <android/api-level.h>
#endif

namespace nCine::RhiGL
{
	void GLTextureFormat::Resolve(PixelFormat format, bool bgr, GLint& internalFormat, GLenum& externalFormat, GLenum& dataType)
	{
		switch (format) {
#if defined(RHI_GL_PROFILE_ES2)
			// OpenGL|ES 2.0 core has no sized internal formats and no GL_RED/GL_RG - `internalformat` must equal
			// `format` and be one of ALPHA/LUMINANCE/LUMINANCE_ALPHA/RGB/RGBA (ES 2.0 spec table 3.4). The engine's
			// single/dual-channel data maps to LUMINANCE / LUMINANCE_ALPHA, which sample as (L,L,L,1) / (L,L,L,A) -
			// exactly what the palette pipeline's GL3.3 swizzle produces for the channels its shaders read (.r/.a),
			// so GLTexture::SetSwizzle() can be a no-op on this profile
			case PixelFormat::RGBA8:
				internalFormat = GL_RGBA; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::RGB8:
				internalFormat = GL_RGB; externalFormat = GL_RGB; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::RG8:
				internalFormat = GL_LUMINANCE_ALPHA; externalFormat = GL_LUMINANCE_ALPHA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::R8:
				internalFormat = GL_LUMINANCE; externalFormat = GL_LUMINANCE; dataType = GL_UNSIGNED_BYTE; break;
			// Depth *textures* need OES_depth_texture on ES2 and the engine only ever creates depth renderbuffers
			// (GLRenderTarget), so the depth cases intentionally fall through to the unsupported-format assert

			// Packed integer formats (already unsized in ES2 style; internal must equal external)
			case PixelFormat::RGB5A1:
				internalFormat = GL_RGBA; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_SHORT_5_5_5_1; break;
			case PixelFormat::RGBA4:
				internalFormat = GL_RGBA; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_SHORT_4_4_4_4; break;
			case PixelFormat::RGB565:
				internalFormat = GL_RGB; externalFormat = GL_RGB; dataType = GL_UNSIGNED_SHORT_5_6_5; break;
#else
			// Uncompressed integer formats (external pixel type is always GL_UNSIGNED_BYTE)
			case PixelFormat::RGBA8:
				internalFormat = GL_RGBA8; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::RGB8:
				internalFormat = GL_RGB8; externalFormat = GL_RGB; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::RG8:
				internalFormat = GL_RG8; externalFormat = GL_RG; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::R8:
				internalFormat = GL_R8; externalFormat = GL_RED; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::Depth16:
				internalFormat = GL_DEPTH_COMPONENT16; externalFormat = GL_DEPTH_COMPONENT; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::Depth24:
				internalFormat = GL_DEPTH_COMPONENT24; externalFormat = GL_DEPTH_COMPONENT; dataType = GL_UNSIGNED_BYTE; break;

			// Packed integer formats
			case PixelFormat::RGB5A1:
				internalFormat = GL_RGB5_A1; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_SHORT_5_5_5_1; break;
			case PixelFormat::RGBA4:
				internalFormat = GL_RGBA4; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_SHORT_4_4_4_4; break;
			case PixelFormat::RGB565:
				internalFormat = GL_RGB565; externalFormat = GL_RGB; dataType = GL_UNSIGNED_SHORT_5_6_5; break;
#endif

			// Floating-point formats (external pixel type is always GL_FLOAT)
			case PixelFormat::RGBA16F:
				internalFormat = GL_RGBA16F; externalFormat = GL_RGBA; dataType = GL_FLOAT; break;
#if !defined(DEATH_TARGET_VITA)	// vitaGL declares GL_RGBA16F but not the 32-bit-float / RGB-float internal formats
			case PixelFormat::RGBA32F:
				internalFormat = GL_RGBA32F; externalFormat = GL_RGBA; dataType = GL_FLOAT; break;
			case PixelFormat::RGB16F:
				internalFormat = GL_RGB16F; externalFormat = GL_RGB; dataType = GL_FLOAT; break;
			case PixelFormat::RGB32F:
				internalFormat = GL_RGB32F; externalFormat = GL_RGB; dataType = GL_FLOAT; break;
#endif
			case PixelFormat::Depth32F:
				internalFormat = GL_DEPTH_COMPONENT32F; externalFormat = GL_DEPTH_COMPONENT; dataType = GL_FLOAT; break;

			// Compressed S3TC / DXT (external pixel type is always GL_UNSIGNED_BYTE)
			case PixelFormat::DXT1RGBA:
				internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::DXT3:
				internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::DXT5:
				internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::DXT1RGB:
				internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT; externalFormat = GL_RGB; dataType = GL_UNSIGNED_BYTE; break;

#if defined(WITH_OPENGLES)
			// Compressed OpenGL ES formats (external pixel type is always GL_UNSIGNED_BYTE)
			case PixelFormat::ATC_RGBA_Explicit:
				internalFormat = GL_ATC_RGBA_EXPLICIT_ALPHA_AMD; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ATC_RGBA_Interpolated:
				internalFormat = GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::PVRTC_2BPP_RGBA:
				internalFormat = GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::PVRTC_4BPP_RGBA:
				internalFormat = GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ETC2RGBA8:
				internalFormat = GL_COMPRESSED_RGBA8_ETC2_EAC; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
#	if !defined(DEATH_TARGET_VITA)	// GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 is not declared by vitaGL
			case PixelFormat::ETC2RGB8A1:
				internalFormat = GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
#	endif
#	if ((!defined(DEATH_TARGET_ANDROID) && defined(WITH_OPENGLES)) || (defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ >= 21)) && !defined(DEATH_TARGET_VITA)
			case PixelFormat::ASTC_4x4:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_4x4_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_5x4:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_5x4_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_5x5:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_5x5_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_6x5:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_6x5_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_6x6:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_6x6_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_8x5:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_8x5_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_8x6:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_8x6_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_8x8:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_8x8_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_10x5:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_10x5_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_10x6:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_10x6_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_10x8:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_10x8_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_10x10:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_10x10_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_12x10:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_12x10_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::ASTC_12x12:
				internalFormat = GL_COMPRESSED_RGBA_ASTC_12x12_KHR; externalFormat = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
#	endif
			case PixelFormat::ETC1:
				internalFormat = GL_ETC1_RGB8_OES; externalFormat = GL_RGB; dataType = GL_UNSIGNED_BYTE; break;
#	if !defined(DEATH_TARGET_VITA)	// GL_COMPRESSED_RGB8_ETC2 is not declared by vitaGL
			case PixelFormat::ETC2RGB8:
				internalFormat = GL_COMPRESSED_RGB8_ETC2; externalFormat = GL_RGB; dataType = GL_UNSIGNED_BYTE; break;
#	endif
			case PixelFormat::ATC_RGB:
				internalFormat = GL_ATC_RGB_AMD; externalFormat = GL_RGB; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::PVRTC_2BPP_RGB:
				internalFormat = GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG; externalFormat = GL_RGB; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::PVRTC_4BPP_RGB:
				internalFormat = GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG; externalFormat = GL_RGB; dataType = GL_UNSIGNED_BYTE; break;
#	if !defined(DEATH_TARGET_VITA)	// GL_COMPRESSED_RG11_EAC / GL_COMPRESSED_R11_EAC are not declared by vitaGL
			case PixelFormat::EAC_RG11:
				internalFormat = GL_COMPRESSED_RG11_EAC; externalFormat = GL_RG; dataType = GL_UNSIGNED_BYTE; break;
			case PixelFormat::EAC_R11:
				internalFormat = GL_COMPRESSED_R11_EAC; externalFormat = GL_RED; dataType = GL_UNSIGNED_BYTE; break;
#	endif
#endif

			default:
				FATAL_ASSERT_MSG(false, "Unsupported pixel format: {}", std::uint32_t(format));
				break;
		}

		// Convert the external format to the corresponding BGR/BGRA one, matching the former `TextureFormat::bgrFormat()`
		if (bgr) {
			if (externalFormat == GL_RGBA) {
#if defined(DEATH_TARGET_VITA)
				// vitaGL declares no GL_BGRA_EXT, BGRA source data is left unswizzled (the bgr path is unused on Vita)
#elif defined(RHI_GL_PROFILE_ES2)
				// ES2's GL_EXT_texture_format_BGRA8888 requires `internalformat` to be GL_BGRA_EXT as well
				// (ES2 core mandates internal == external), unlike ES3 where only the external format changes
				externalFormat = GL_BGRA_EXT;
				internalFormat = GL_BGRA_EXT;
#elif !defined(WITH_OPENGLES)
				externalFormat = GL_BGRA;
#else
				externalFormat = GL_BGRA_EXT;
#endif
			}
#if !defined(WITH_OPENGLES)
			else if (externalFormat == GL_RGB) {
				externalFormat = GL_BGR;
			}
#endif
		}
	}

	void GLTextureFormat::CheckSupport(PixelFormat format)
	{
		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();

		switch (format) {
			case PixelFormat::DXT1RGB:
			case PixelFormat::DXT3:
			case PixelFormat::DXT5: {
				const bool hasS3tc = gfxCaps.HasExtension(IGfxCapabilities::Extensions::EXT_TEXTURE_COMPRESSION_S3TC);
				FATAL_ASSERT_MSG(hasS3tc, "GL_EXT_texture_compression_s3tc not available");
				break;
			}
#if defined(WITH_OPENGLES)
			case PixelFormat::ETC1: {
				const bool hasEct1 = gfxCaps.HasExtension(IGfxCapabilities::Extensions::OES_COMPRESSED_ETC1_RGB8_TEXTURE);
				FATAL_ASSERT_MSG(hasEct1, "GL_OES_compressed_etc1_rgb8_texture not available");
				break;
			}
			case PixelFormat::ATC_RGB:
			case PixelFormat::ATC_RGBA_Explicit:
			case PixelFormat::ATC_RGBA_Interpolated: {
				const bool hasAtc = gfxCaps.HasExtension(IGfxCapabilities::Extensions::AMD_COMPRESSED_ATC_TEXTURE);
				FATAL_ASSERT_MSG(hasAtc, "GL_AMD_compressed_ATC_texture not available");
				break;
			}
			case PixelFormat::PVRTC_2BPP_RGB:
			case PixelFormat::PVRTC_2BPP_RGBA:
			case PixelFormat::PVRTC_4BPP_RGB:
			case PixelFormat::PVRTC_4BPP_RGBA: {
				const bool hasPvr = gfxCaps.HasExtension(IGfxCapabilities::Extensions::IMG_TEXTURE_COMPRESSION_PVRTC);
				FATAL_ASSERT_MSG(hasPvr, "GL_IMG_texture_compression_pvrtc not available");
				break;
			}
#	if (!defined(DEATH_TARGET_ANDROID) && defined(WITH_OPENGLES)) || (defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ >= 21)
			case PixelFormat::ASTC_4x4:
			case PixelFormat::ASTC_5x4:
			case PixelFormat::ASTC_5x5:
			case PixelFormat::ASTC_6x5:
			case PixelFormat::ASTC_6x6:
			case PixelFormat::ASTC_8x5:
			case PixelFormat::ASTC_8x6:
			case PixelFormat::ASTC_8x8:
			case PixelFormat::ASTC_10x5:
			case PixelFormat::ASTC_10x6:
			case PixelFormat::ASTC_10x8:
			case PixelFormat::ASTC_10x10:
			case PixelFormat::ASTC_12x10:
			case PixelFormat::ASTC_12x12: {
				const bool hasAstc = gfxCaps.HasExtension(IGfxCapabilities::Extensions::KHR_TEXTURE_COMPRESSION_ASTC_LDR);
				FATAL_ASSERT_MSG(hasAstc, "GL_KHR_texture_compression_astc_ldr not available");
				break;
			}
#	endif
#endif
			default:
				break;
		}
	}
}
