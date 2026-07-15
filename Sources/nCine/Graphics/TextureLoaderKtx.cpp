#include "TextureLoaderKtx.h"

#include <Base/Memory.h>

using namespace Death::IO;
using namespace Death::Memory;

namespace nCine
{
	namespace
	{
		// KTX v1.1 stores the graphics-API internal-format enum values on disk; map the recognized codes to a
		// backend-neutral pixel format (mirrors the former internal-format resolver, including its platform gating).
		PixelFormat pixelFormatFromCode(std::uint32_t code)
		{
			switch (code) {
				case 1: case 0x8229: return PixelFormat::R8;
				case 2: case 0x822B: return PixelFormat::RG8;
				case 3: case 0x8051: return PixelFormat::RGB8;
				case 4: case 0x8058: return PixelFormat::RGBA8;
				case 0x8D62: return PixelFormat::RGB565;
				case 0x8057: return PixelFormat::RGB5A1;
				case 0x8056: return PixelFormat::RGBA4;
				case 0x881B: return PixelFormat::RGB16F;
				case 0x881A: return PixelFormat::RGBA16F;
				case 0x8815: return PixelFormat::RGB32F;
				case 0x8814: return PixelFormat::RGBA32F;
				case 0x81A5: return PixelFormat::Depth16;
				case 0x81A6: return PixelFormat::Depth24;
				case 0x8CAC: return PixelFormat::Depth32F;
				case 0x83F0: return PixelFormat::DXT1RGB;
				case 0x83F1: return PixelFormat::DXT1RGBA;
				case 0x83F2: return PixelFormat::DXT3;
				case 0x83F3: return PixelFormat::DXT5;
				case 0x8D64: return PixelFormat::ETC1;
				case 0x9274: return PixelFormat::ETC2RGB8;
				case 0x9276: return PixelFormat::ETC2RGB8A1;
				case 0x9278: return PixelFormat::ETC2RGBA8;
				case 0x9270: return PixelFormat::EAC_R11;
				case 0x9272: return PixelFormat::EAC_RG11;
				case 0x8C92: return PixelFormat::ATC_RGB;
				case 0x8C93: return PixelFormat::ATC_RGBA_Explicit;
				case 0x87EE: return PixelFormat::ATC_RGBA_Interpolated;
				case 0x8C01: return PixelFormat::PVRTC_2BPP_RGB;
				case 0x8C03: return PixelFormat::PVRTC_2BPP_RGBA;
				case 0x8C00: return PixelFormat::PVRTC_4BPP_RGB;
				case 0x8C02: return PixelFormat::PVRTC_4BPP_RGBA;
				case 0x93B0: return PixelFormat::ASTC_4x4;
				case 0x93B1: return PixelFormat::ASTC_5x4;
				case 0x93B2: return PixelFormat::ASTC_5x5;
				case 0x93B3: return PixelFormat::ASTC_6x5;
				case 0x93B4: return PixelFormat::ASTC_6x6;
				case 0x93B5: return PixelFormat::ASTC_8x5;
				case 0x93B6: return PixelFormat::ASTC_8x6;
				case 0x93B7: return PixelFormat::ASTC_8x8;
				case 0x93B8: return PixelFormat::ASTC_10x5;
				case 0x93B9: return PixelFormat::ASTC_10x6;
				case 0x93BA: return PixelFormat::ASTC_10x8;
				case 0x93BB: return PixelFormat::ASTC_10x10;
				case 0x93BC: return PixelFormat::ASTC_12x10;
				case 0x93BD: return PixelFormat::ASTC_12x12;
				default:
					FATAL_ASSERT_MSG(false, "Unknown internal format: 0x{:x}", code);
					return PixelFormat::Unknown;
			}
		}
	}

	std::uint8_t TextureLoaderKtx::fileIdentifier_[] = {
		0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
	}; // "«KTX 11»\r\n\x1A\n"};

	TextureLoaderKtx::TextureLoaderKtx(std::unique_ptr<Stream> fileHandle)
		: ITextureLoader(std::move(fileHandle))
	{
		KtxHeader header;

		if (!fileHandle_->IsValid()) {
			return;
		}

		const bool headerRead = readHeader(header);
		DEATH_ASSERT(headerRead, "KTX header cannot be read", );
		const bool formatParsed = parseFormat(header);
		DEATH_ASSERT(formatParsed, "KTX format cannot be parsed", );

		hasLoaded_ = true;
	}

	bool TextureLoaderKtx::readHeader(KtxHeader& header)
	{
		bool checkPassed = true;

		// KTX header is 64 bytes long
		fileHandle_->Read(&header, 64);

		for (int i = 0; i < KtxIdentifierLength; i++) {
			if (header.identifier[i] != fileIdentifier_[i]) {
				checkPassed = false;
			}
		}

		DEATH_ASSERT(checkPassed, "Invalid KTX signature", false);
		// Checking for the header identifier
		DEATH_ASSERT(header.endianess != 0x01020304, "File endianess doesn't match machine one", false);

		// Accounting for key-value data and `UInt32 imageSize` from first MIP level
		headerSize_ = 64 + AsLE(header.bytesOfKeyValueData) + 4;
		width_ = AsLE(header.pixelWidth);
		height_ = AsLE(header.pixelHeight);
		mipMapCount_ = AsLE(header.numberOfMipmapLevels);

		return true;
	}

	bool TextureLoaderKtx::parseFormat(const KtxHeader& header)
	{
		const PixelFormat format = pixelFormatFromCode(AsLE(header.internalFormat));

		loadPixels(format);

		if (mipMapCount_ > 1) {
			LOGI("MIP Maps: {}", mipMapCount_);
			mipDataOffsets_ = std::make_unique<std::uint32_t[]>(mipMapCount_);
			mipDataSizes_ = std::make_unique<std::uint32_t[]>(mipMapCount_);
			std::uint32_t dataSizesSum = TextureFormat::calculateMipSizes(format, width_, height_, mipMapCount_, mipDataOffsets_.get(), mipDataSizes_.get());

			// HACK: accounting for `UInt32 imageSize` on top of each MIP level
			// Excluding the first one, already taken into account in header size
			for (std::int32_t i = 1; i < mipMapCount_; i++) {
				mipDataOffsets_[i] += 4 * i;
			}
			dataSizesSum += 4 * mipMapCount_;

			if (dataSizesSum != dataSize_) {
				LOGW("The sum of MIP maps size ({}) is different than texture total data ({})", dataSizesSum, dataSize_);
			}
		}

		return true;
	}
}
