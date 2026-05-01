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
		RHI::TextureFormat format = RHI::TextureFormat::RGB8; // default

		uint64_t pixelFormat = AsLE(header.pixelFormat);

		// Texture contains compressed data, most significant 4 bytes have been set to zero
		if (pixelFormat < 0x0000000100000000ULL) {
			LOGI("Compressed format: {}", pixelFormat);

			// Parsing the pixel format
			switch (pixelFormat) {
				case FMT_DXT1:
					format = RHI::TextureFormat::RGB_DXT1;
					break;
				case FMT_DXT3:
					format = RHI::TextureFormat::RGBA_DXT3;
					break;
				case FMT_DXT5:
					format = RHI::TextureFormat::RGBA_DXT5;
					break;
#if defined(WITH_OPENGLES)
				case FMT_ETC1:
					format = RHI::TextureFormat::RGB_ETC1;
					break;
				case FMT_ETC2_RGB:
					format = RHI::TextureFormat::RGB_ETC2;
					break;
				case FMT_ETC2_RGBA:
					format = RHI::TextureFormat::RGBA_ETC2;
					break;
				case FMT_PVRTC_2BPP_RGB:
					format = RHI::TextureFormat::RGB_PVRTC_2BPP;
					break;
				case FMT_PVRTC_2BPP_RGBA:
					format = RHI::TextureFormat::RGBA_PVRTC_2BPP;
					break;
				case FMT_PVRTC_4BPP_RGB:
					format = RHI::TextureFormat::RGB_PVRTC_4BPP;
					break;
				case FMT_PVRTC_4BPP_RGBA:
					format = RHI::TextureFormat::RGBA_PVRTC_4BPP;
					break;
				case FMT_PVRTCII_2BPP:
				case FMT_PVRTCII_4BPP:
				case FMT_ETC2_RGB_A1:
				case FMT_EAC_R11:
				case FMT_EAC_RG11:
					LOGE("Unsupported PVR3 compressed format: 0x{:x}", pixelFormat);
					return false;
#	if (!defined(DEATH_TARGET_ANDROID) && defined(WITH_OPENGLES)) || (defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ >= 21)
				case FMT_ASTC_4x4:  format = RHI::TextureFormat::RGBA_ASTC_4x4;  break;
				case FMT_ASTC_5x4:  format = RHI::TextureFormat::RGBA_ASTC_5x4;  break;
				case FMT_ASTC_5x5:  format = RHI::TextureFormat::RGBA_ASTC_5x5;  break;
				case FMT_ASTC_6x5:  format = RHI::TextureFormat::RGBA_ASTC_6x5;  break;
				case FMT_ASTC_6x6:  format = RHI::TextureFormat::RGBA_ASTC_6x6;  break;
				case FMT_ASTC_8x5:  format = RHI::TextureFormat::RGBA_ASTC_8x5;  break;
				case FMT_ASTC_8x6:  format = RHI::TextureFormat::RGBA_ASTC_8x6;  break;
				case FMT_ASTC_8x8:  format = RHI::TextureFormat::RGBA_ASTC_8x8;  break;
				case FMT_ASTC_10x5: format = RHI::TextureFormat::RGBA_ASTC_10x5; break;
				case FMT_ASTC_10x6: format = RHI::TextureFormat::RGBA_ASTC_10x6; break;
				case FMT_ASTC_10x8: format = RHI::TextureFormat::RGBA_ASTC_10x8; break;
				case FMT_ASTC_10x10: format = RHI::TextureFormat::RGBA_ASTC_10x10; break;
				case FMT_ASTC_12x10: format = RHI::TextureFormat::RGBA_ASTC_12x10; break;
				case FMT_ASTC_12x12: format = RHI::TextureFormat::RGBA_ASTC_12x12; break;
#	endif
#endif
				default:
					LOGE("Unsupported PVR3 compressed format: 0x{:x}", pixelFormat);
					return false;
			}

			loadPixels(format);
		} else {
			LOGI("Uncompressed format: {:c}{:c}{:c}{:c} ({}, {}, {}, {})",
				   reinterpret_cast<char*>(&pixelFormat)[0], reinterpret_cast<char*>(&pixelFormat)[1],
				   reinterpret_cast<char*>(&pixelFormat)[2], reinterpret_cast<char*>(&pixelFormat)[3],
				   reinterpret_cast<unsigned char*>(&pixelFormat)[4], reinterpret_cast<unsigned char*>(&pixelFormat)[5],
				   reinterpret_cast<unsigned char*>(&pixelFormat)[6], reinterpret_cast<unsigned char*>(&pixelFormat)[7]);

			switch (pixelFormat) {
				case FMT_BGRA_8888:
				case FMT_RGBA_8888:
					format = RHI::TextureFormat::RGBA8;
					break;
				case FMT_RGB_888:
					format = RHI::TextureFormat::RGB8;
					break;
				case FMT_LA_88:
					format = RHI::TextureFormat::RG8;
					break;
				case FMT_L_8:
				case FMT_A_8:
					format = RHI::TextureFormat::R8;
					break;
				case FMT_RGB_565:
				case FMT_RGBA_5551:
				case FMT_RGBA_4444:
					LOGE("Unsupported PVR3 packed uncompressed format: 0x{:x}", pixelFormat);
					return false;
				default:
					LOGE("Unsupported PVR3 uncompressed format: 0x{:x}", pixelFormat);
					return false;
			}

			loadPixels(format);
		}

		if (mipMapCount_ > 1) {
			LOGI("MIP Maps: {}", mipMapCount_);
			mipDataOffsets_ = std::make_unique<std::uint32_t[]>(mipMapCount_);
			mipDataSizes_ = std::make_unique<std::uint32_t[]>(mipMapCount_);
			std::uint32_t dataSizesSum = RHI::CalculateMipSizes(format, width_, height_, mipMapCount_, mipDataOffsets_.get(), mipDataSizes_.get());
			if (dataSizesSum != dataSize_) {
				LOGW("The sum of MIP maps size ({}) is different than texture total data ({})", dataSizesSum, dataSize_);
			}
		}

		return true;
	}
}
