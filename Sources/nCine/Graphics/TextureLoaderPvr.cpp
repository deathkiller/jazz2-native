#include "TextureLoaderPvr.h"
#include "../../Main.h"

#include <Base/Memory.h>

using namespace Death::IO;
using namespace Death::Memory;

namespace nCine
{
	TextureLoaderPvr::TextureLoaderPvr(std::unique_ptr<Stream> fileHandle)
		: ITextureLoader(std::move(fileHandle))
	{
		Pvr3Header header;

		if (!fileHandle_->IsValid()) {
			return;
		}

		const bool headerRead = readHeader(header);
		DEATH_ASSERT(headerRead, "PVR header cannot be read", );
		const bool formatParsed = parseFormat(header);
		DEATH_ASSERT(formatParsed, "PVR format cannot be parsed", );

		hasLoaded_ = true;
	}

	bool TextureLoaderPvr::readHeader(Pvr3Header& header)
	{
		// PVR3 header is 52 bytes long
		fileHandle_->Read(&header, 52);

		// Checking for the header presence ("PVR"03)
		DEATH_ASSERT(AsLE(header.version) == 0x03525650, "Invalid PVR3 signature", false);

		headerSize_ = 52 + AsLE(header.metaDataSize);
		width_ = AsLE(header.width);
		height_ = AsLE(header.height);
		mipMapCount_ = header.numMipmaps;

		if (mipMapCount_ == 0) {
			mipMapCount_ = 1;
		}
		return true;
	}

	bool TextureLoaderPvr::parseFormat(const Pvr3Header& header)
	{
		PixelFormat internalFormat = PixelFormat::RGB8; // to suppress uninitialized variable warning

		uint64_t pixelFormat = AsLE(header.pixelFormat);

		// Texture contains compressed data, most significant 4 bytes have been set to zero
		if (pixelFormat < 0x0000000100000000ULL) {
			LOGI("Compressed format: {}", pixelFormat);

			// Parsing the pixel format
			switch (pixelFormat) {
				case FMT_DXT1:
					internalFormat = PixelFormat::DXT1RGB;
					break;
				case FMT_DXT3:
					internalFormat = PixelFormat::DXT3;
					break;
				case FMT_DXT5:
					internalFormat = PixelFormat::DXT5;
					break;
				case FMT_ETC1:
					internalFormat = PixelFormat::ETC1;
					break;
				case FMT_PVRTC_2BPP_RGB:
					internalFormat = PixelFormat::PVRTC_2BPP_RGB;
					break;
				case FMT_PVRTC_2BPP_RGBA:
					internalFormat = PixelFormat::PVRTC_2BPP_RGBA;
					break;
				case FMT_PVRTC_4BPP_RGB:
					internalFormat = PixelFormat::PVRTC_4BPP_RGB;
					break;
				case FMT_PVRTC_4BPP_RGBA:
					internalFormat = PixelFormat::PVRTC_4BPP_RGBA;
					break;
				case FMT_PVRTCII_2BPP:
				case FMT_PVRTCII_4BPP:
					LOGF("No support for PVRTC-II compression");
					break;
				case FMT_ETC2_RGB:
					internalFormat = PixelFormat::ETC2RGB8;
					break;
				case FMT_ETC2_RGBA:
					internalFormat = PixelFormat::ETC2RGBA8;
					break;
				case FMT_ETC2_RGB_A1:
					internalFormat = PixelFormat::ETC2RGB8A1;
					break;
				case FMT_EAC_R11:
					internalFormat = PixelFormat::EAC_R11;
					break;
				case FMT_EAC_RG11:
					internalFormat = PixelFormat::EAC_RG11;
					break;
				case FMT_ASTC_4x4:
					internalFormat = PixelFormat::ASTC_4x4;
					break;
				case FMT_ASTC_5x4:
					internalFormat = PixelFormat::ASTC_5x4;
					break;
				case FMT_ASTC_5x5:
					internalFormat = PixelFormat::ASTC_5x5;
					break;
				case FMT_ASTC_6x5:
					internalFormat = PixelFormat::ASTC_6x5;
					break;
				case FMT_ASTC_6x6:
					internalFormat = PixelFormat::ASTC_6x6;
					break;
				case FMT_ASTC_8x5:
					internalFormat = PixelFormat::ASTC_8x5;
					break;
				case FMT_ASTC_8x6:
					internalFormat = PixelFormat::ASTC_8x6;
					break;
				case FMT_ASTC_8x8:
					internalFormat = PixelFormat::ASTC_8x8;
					break;
				case FMT_ASTC_10x5:
					internalFormat = PixelFormat::ASTC_10x5;
					break;
				case FMT_ASTC_10x6:
					internalFormat = PixelFormat::ASTC_10x6;
					break;
				case FMT_ASTC_10x8:
					internalFormat = PixelFormat::ASTC_10x8;
					break;
				case FMT_ASTC_10x10:
					internalFormat = PixelFormat::ASTC_10x10;
					break;
				case FMT_ASTC_12x10:
					internalFormat = PixelFormat::ASTC_12x10;
					break;
				case FMT_ASTC_12x12:
					internalFormat = PixelFormat::ASTC_12x12;
					break;
				default:
					LOGE("Unsupported PVR3 compressed format: 0x{:x}", pixelFormat);
					return false;
			}

			loadPixels(internalFormat);
		} else {
			// Texture contains uncompressed data
			bool bgra = false;

			LOGI("Uncompressed format: {:c}{:c}{:c}{:c} ({}, {}, {}, {})",
				   reinterpret_cast<char*>(&pixelFormat)[0], reinterpret_cast<char*>(&pixelFormat)[1],
				   reinterpret_cast<char*>(&pixelFormat)[2], reinterpret_cast<char*>(&pixelFormat)[3],
				   reinterpret_cast<unsigned char*>(&pixelFormat)[4], reinterpret_cast<unsigned char*>(&pixelFormat)[5],
				   reinterpret_cast<unsigned char*>(&pixelFormat)[6], reinterpret_cast<unsigned char*>(&pixelFormat)[7]);

			switch (pixelFormat) {
				case FMT_BGRA_8888:
					internalFormat = PixelFormat::RGBA8;
					bgra = true;
					break;
				case FMT_RGBA_8888:
					internalFormat = PixelFormat::RGBA8;
					break;
				case FMT_RGB_888:
					internalFormat = PixelFormat::RGB8;
					break;
				case FMT_RGB_565:
					internalFormat = PixelFormat::RGB565;
					break;
				case FMT_RGBA_5551:
					internalFormat = PixelFormat::RGB5A1;
					break;
				case FMT_RGBA_4444:
					internalFormat = PixelFormat::RGBA4;
					break;
				case FMT_LA_88:
					internalFormat = PixelFormat::RG8;
					break;
				case FMT_L_8:
					internalFormat = PixelFormat::R8;
					break;
				case FMT_A_8:
					internalFormat = PixelFormat::R8;
					break;
				default:
					LOGE("Unsupported PVR3 uncompressed format: 0x{:x}", pixelFormat);
					return false;
			}

			loadPixels(internalFormat);
			if (bgra) {
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
