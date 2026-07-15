#include "TextureFormat.h"
#include "../../Main.h"

namespace nCine
{
	bool TextureFormat::isCompressed() const
	{
		switch (pixelFormat_) {
			case PixelFormat::DXT1RGB:
			case PixelFormat::DXT1RGBA:
			case PixelFormat::DXT3:
			case PixelFormat::DXT5:
			case PixelFormat::ETC1:
			case PixelFormat::ETC2RGB8:
			case PixelFormat::ETC2RGB8A1:
			case PixelFormat::ETC2RGBA8:
			case PixelFormat::EAC_R11:
			case PixelFormat::EAC_RG11:
			case PixelFormat::ATC_RGB:
			case PixelFormat::ATC_RGBA_Explicit:
			case PixelFormat::ATC_RGBA_Interpolated:
			case PixelFormat::PVRTC_2BPP_RGB:
			case PixelFormat::PVRTC_2BPP_RGBA:
			case PixelFormat::PVRTC_4BPP_RGB:
			case PixelFormat::PVRTC_4BPP_RGBA:
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
			case PixelFormat::ASTC_12x12:
				return true;
			default:
				return false;
		}
	}

	std::uint32_t TextureFormat::numChannels() const
	{
		switch (pixelFormat_) {
			case PixelFormat::R8:
			case PixelFormat::EAC_R11:
				return 1;
			case PixelFormat::RG8:
			case PixelFormat::EAC_RG11:
				return 2;
			case PixelFormat::RGB8:
			case PixelFormat::RGB565:
			case PixelFormat::RGB16F:
			case PixelFormat::RGB32F:
			case PixelFormat::DXT1RGB:
			case PixelFormat::ETC1:
			case PixelFormat::ETC2RGB8:
			case PixelFormat::ATC_RGB:
			case PixelFormat::PVRTC_2BPP_RGB:
			case PixelFormat::PVRTC_4BPP_RGB:
				return 3;
			default:
				return 4;
		}
	}

	void TextureFormat::bgrFormat()
	{
		bgr_ = true;
	}

	std::uint32_t TextureFormat::calculateMipSizes(PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t mipMapCount, std::uint32_t* mipDataOffsets, std::uint32_t* mipDataSizes)
	{
		std::uint32_t blockWidth = 1; // Compression block width in pixels
		std::uint32_t blockHeight = 1; // Compression block height in pixels
		std::uint32_t bpp = 1; // Bits per pixel
		std::uint32_t blockSize = 0; // Compression block size in byts
		std::uint32_t minDataSize = 1; // Minimum data size in bytes

		switch (format) {
			case PixelFormat::RGBA8:
				bpp = 32;
				break;
			case PixelFormat::RGB8:
				bpp = 24;
				break;
			case PixelFormat::RG8:
			case PixelFormat::RGB565:
			case PixelFormat::RGB5A1:
			case PixelFormat::RGBA4:
				bpp = 16;
				break;
			case PixelFormat::R8:
				bpp = 8;
				break;
			case PixelFormat::DXT1RGBA:
			case PixelFormat::DXT1RGB:
				// max(1, width / 4) x max(1, height / 4) x 8(DXT1)
				blockWidth = 4;
				blockHeight = 4;
				bpp = 4;
				minDataSize = 8;
				break;
			case PixelFormat::DXT3:
			case PixelFormat::DXT5:
				// max(1, width / 4) x max(1, height / 4) x 16(DXT2-5)
				blockWidth = 4;
				blockHeight = 4;
				bpp = 8;
				minDataSize = 16;
				break;
			case PixelFormat::ETC1:
			case PixelFormat::ETC2RGB8:
			case PixelFormat::ETC2RGB8A1:
			case PixelFormat::EAC_R11:
				blockWidth = 4;
				blockHeight = 4;
				bpp = 4;
				minDataSize = 8;
				break;
			case PixelFormat::ETC2RGBA8:
			case PixelFormat::EAC_RG11:
				blockWidth = 4;
				blockHeight = 4;
				bpp = 8;
				minDataSize = 16;
				break;
			case PixelFormat::ATC_RGBA_Explicit:
			case PixelFormat::ATC_RGBA_Interpolated:
				// ((width_in_texels+3)/4) * ((height_in_texels+3)/4) * 16
				blockWidth = 4;
				blockHeight = 4;
				bpp = 8;
				minDataSize = 16;
				break;
			case PixelFormat::ATC_RGB:
				// ((width_in_texels+3)/4) * ((height_in_texels+3)/4) * 8
				blockWidth = 4;
				blockHeight = 4;
				bpp = 4;
				minDataSize = 8;
				break;
			case PixelFormat::PVRTC_2BPP_RGB:
			case PixelFormat::PVRTC_2BPP_RGBA:
				blockWidth = 8;
				blockHeight = 4;
				bpp = 2;
				minDataSize = 2 * 2 * ((blockWidth * blockHeight * bpp) / 8);
				break;
			case PixelFormat::PVRTC_4BPP_RGB:
			case PixelFormat::PVRTC_4BPP_RGBA:
				blockWidth = 4;
				blockHeight = 4;
				bpp = 4;
				minDataSize = 2 * 2 * ((blockWidth * blockHeight * bpp) / 8);
				break;
			case PixelFormat::ASTC_4x4:
				blockWidth = 4;
				blockHeight = 4;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_5x4:
				blockWidth = 5;
				blockHeight = 4;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_5x5:
				blockWidth = 5;
				blockHeight = 5;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_6x5:
				blockWidth = 6;
				blockHeight = 5;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_6x6:
				blockWidth = 6;
				blockHeight = 6;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_8x5:
				blockWidth = 8;
				blockHeight = 5;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_8x6:
				blockWidth = 8;
				blockHeight = 6;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_8x8:
				blockWidth = 8;
				blockHeight = 8;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_10x5:
				blockWidth = 10;
				blockHeight = 5;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_10x6:
				blockWidth = 10;
				blockHeight = 6;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_10x8:
				blockWidth = 10;
				blockHeight = 8;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_10x10:
				blockWidth = 10;
				blockHeight = 10;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_12x10:
				blockWidth = 12;
				blockHeight = 10;
				blockSize = 16;
				minDataSize = 16;
				break;
			case PixelFormat::ASTC_12x12:
				blockWidth = 12;
				blockHeight = 12;
				blockSize = 16;
				minDataSize = 16;
				break;
			default:
				LOGF("MIP maps not supported for pixel format: {}", std::uint32_t(format));
				break;
		}

		std::int32_t levelWidth = width;
		std::int32_t levelHeight = height;
		std::uint32_t dataSizesSum = 0;

		DEATH_ASSERT(mipDataOffsets);
		DEATH_ASSERT(mipDataSizes);

		for (std::int32_t i = 0; i < mipMapCount; i++) {
			mipDataOffsets[i] = dataSizesSum;
			mipDataSizes[i] = (blockSize > 0)
				? (levelWidth / blockWidth) * (levelHeight / blockHeight) * blockSize
				: (levelWidth / blockWidth) * (levelHeight / blockHeight) * ((blockWidth * blockHeight * bpp) / 8);

			// Clamping to the minimum valid size
			if (mipDataSizes[i] < minDataSize) {
				mipDataSizes[i] = minDataSize;
			}

			levelWidth /= 2;
			levelHeight /= 2;
			dataSizesSum += mipDataSizes[i];
		}

		return dataSizesSum;
	}

}
