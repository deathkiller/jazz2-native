#include "TextureLoaderDds.h"

#include <Base/Memory.h>

using namespace Death::IO;
using namespace Death::Memory;

namespace nCine
{
	TextureLoaderDds::TextureLoaderDds(std::unique_ptr<Stream> fileHandle)
		: ITextureLoader(std::move(fileHandle))
	{
		DdsHeader header;

		if (!fileHandle_->IsValid()) {
			return;
		}

		const bool headerRead = readHeader(header);
		DEATH_ASSERT(headerRead, "DDS header cannot be read", );
		const bool formatParsed = parseFormat(header);
		DEATH_ASSERT(formatParsed, "DDS format cannot be parsed", );

		hasLoaded_ = true;
	}

	bool TextureLoaderDds::readHeader(DdsHeader& header)
	{
		// DDS header is 128 bytes long
		fileHandle_->Read(&header, 128);

		// Checking for the header presence
		DEATH_ASSERT(AsLE(header.dwMagic) == 0x20534444 /* "DDS " */, "Invalid DDS signature", false);

		headerSize_ = 128;
		width_ = AsLE(header.dwWidth);
		height_ = AsLE(header.dwHeight);
		mipMapCount_ = AsLE(header.dwMipMapCount);

		if (mipMapCount_ == 0) {
			mipMapCount_ = 1;
		}

		return true;
	}

	bool TextureLoaderDds::parseFormat(const DdsHeader& header)
	{
		GLenum internalFormat = GL_RGB; // to suppress uninitialized variable warning

		const uint32_t flags = AsLE(header.ddspf.dwFlags);

		// Texture contains compressed RGB data, dwFourCC contains valid data
		if (flags & DDPF_FOURCC) {
			const uint32_t fourCC = AsLE(header.ddspf.dwFourCC);

			const char* fourCCchars = reinterpret_cast<const char*>(&fourCC);
			LOGI("FourCC: \"{:c}{:c}{:c}{:c}\" (0x{:x})", fourCCchars[0], fourCCchars[1], fourCCchars[2], fourCCchars[3], fourCC);

			// Parsing the FourCC format
			switch (fourCC) {
				case DDS_DXT1:
					internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
					break;
				case DDS_DXT3:
					internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
					break;
				case DDS_DXT5:
					internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
					break;
#if defined(WITH_OPENGLES)
				case DDS_ETC1:
					internalFormat = GL_ETC1_RGB8_OES;
					break;
				case DDS_ATC:
					internalFormat = GL_ATC_RGB_AMD;
					break;
				case DDS_ATCA:
					internalFormat = GL_ATC_RGBA_EXPLICIT_ALPHA_AMD;
					break;
				case DDS_ATCI:
					internalFormat = GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD;
					break;
#endif
				default:
					LOGE("Unsupported FourCC compression code: {}", fourCC);
					return false;
			}

			loadPixels(internalFormat);
		} else {
			// Texture contains uncompressed data
			GLenum type = GL_UNSIGNED_BYTE;

			const uint32_t bitCount = AsLE(header.ddspf.dwRGBBitCount);
			const uint32_t redMask = AsLE(header.ddspf.dwRBitMask);
			const uint32_t greenMask = AsLE(header.ddspf.dwGBitMask);
			const uint32_t blueMask = AsLE(header.ddspf.dwBBitMask);
			const uint32_t alphaMask = AsLE(header.ddspf.dwABitMask);

			LOGI("Pixel masks ({}bit): R:0x{:x} G:0x{:x} B:0x{:x} A:0x{:x}", bitCount, redMask, greenMask, blueMask, alphaMask);

			// Texture contains uncompressed RGB data
			// dwRGBBitCount and the RGB masks (dwRBitMask, dwRBitMask, dwRBitMask) contain valid data
			if (flags & DDPF_RGB || flags & (DDPF_RGB | DDPF_ALPHAPIXELS)) {
				if ((redMask == 0x00FF0000 && greenMask == 0x0000FF00 && blueMask == 0x000000FF && alphaMask == 0x0) ||
					(blueMask == 0x00FF0000 && greenMask == 0x0000FF00 && redMask == 0x000000FF && alphaMask == 0x0)) { // 888
#if defined(WITH_OPENGL2)
					internalFormat = GL_RGB;
#else
					internalFormat = GL_RGB8;
#endif
				} else if ((alphaMask == 0xFF000000 && redMask == 0x00FF0000 && greenMask == 0x0000FF00 && blueMask == 0x000000FF) ||
						 (alphaMask == 0xFF000000 && blueMask == 0x00FF0000 && greenMask == 0x0000FF00 && redMask == 0x000000FF)) { // 8888
#if defined(WITH_OPENGL2)
					internalFormat = GL_RGBA;
#else
					internalFormat = GL_RGBA8;
#endif
				}
#if 0
				// 16 bits uncompressed DDS data is not compatbile with OpenGL color channels order
				else if (redMask == 0xF800 && greenMask == 0x07E0 && blueMask == 0x001F) { // 565
					internalFormat = GL_RGB565;
					type = GL_UNSIGNED_SHORT_5_6_5;
				} else if (alphaMask == 0x8000 && redMask == 0x7C00 && greenMask == 0x03E0 && blueMask == 0x001F) { // 5551
					internalFormat = GL_RGB5_A1;
					type = GL_UNSIGNED_SHORT_5_5_5_1;
				} else if (alphaMask == 0xF000 && redMask == 0x0F00 && greenMask == 0x00F0 && blueMask == 0x000F) { // 4444
					internalFormat = GL_RGBA4;
					type = GL_UNSIGNED_SHORT_4_4_4_4;
				}
#endif
				else {
					LOGE("Unsupported DDPF_RGB pixel format");
					return false;
				}
			} else if (flags & (DDPF_LUMINANCE | DDPF_ALPHAPIXELS)) {
				// Used in some older DDS files for single channel color uncompressed data
				// dwRGBBitCount contains the luminance channel bit count; dwRBitMask contains the channel mask
				// Can be combined with DDPF_ALPHAPIXELS for a two channel DDS file
#if defined(WITH_OPENGL2)
				internalFormat = GL_LUMINANCE_ALPHA;
#else
				internalFormat = GL_RG8;
#endif
			} else if (flags & DDPF_LUMINANCE) {
#if defined(WITH_OPENGL2)
				internalFormat = GL_LUMINANCE;
#else
				internalFormat = GL_R8;
#endif
			} else if (flags & DDPF_ALPHA) {
				// Used in some older DDS files for alpha channel only uncompressed data
				// dwRGBBitCount contains the alpha channel bitcount; dwABitMask contains valid data
#if defined(WITH_OPENGL2)
				internalFormat = GL_LUMINANCE;
#else
				internalFormat = GL_R8;
#endif
			} else {
				LOGE("Unsupported DDS uncompressed pixel format: {}", flags);
				return false;
			}

			loadPixels(internalFormat, type);

			if (redMask > blueMask && bitCount > 16) {
				texFormat_.bgrFormat();
			}
		}

		if (mipMapCount_ > 1) {
			LOGI("MIP Maps: {}", mipMapCount_);
			mipDataOffsets_ = std::make_unique<std::uint32_t[]>(mipMapCount_);
			mipDataSizes_ = std::make_unique<std::uint32_t[]>(mipMapCount_);
			std::uint32_t dataSizesSum = TextureFormat::calculateMipSizes(internalFormat, width_, height_, mipMapCount_, mipDataOffsets_.get(), mipDataSizes_.get());
			if (dataSizesSum != dataSize_) {
				LOGW("The sum of MIP maps size ({}) is different than texture total data ({})", dataSizesSum, dataSize_);
			}
		}

		return true;
	}
}
