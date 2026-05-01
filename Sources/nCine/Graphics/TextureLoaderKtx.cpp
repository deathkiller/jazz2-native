#include "TextureLoaderKtx.h"

#include <Base/Memory.h>

using namespace Death::IO;
using namespace Death::Memory;

namespace nCine
{
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
		const std::uint32_t glInternalFormat = AsLE(header.glInternalFormat);

		// Map GL internal format to RHI::TextureFormat
		RHI::TextureFormat format;
		switch (glInternalFormat) {
			case 0x8229: format = RHI::TextureFormat::R8; break;          // GL_R8
			case 0x822B: format = RHI::TextureFormat::RG8; break;         // GL_RG8
			case 0x8051: format = RHI::TextureFormat::RGB8; break;        // GL_RGB8
			case 0x8058: format = RHI::TextureFormat::RGBA8; break;       // GL_RGBA8
			case 0x83F0: format = RHI::TextureFormat::RGB_DXT1; break;    // GL_COMPRESSED_RGB_S3TC_DXT1_EXT
			case 0x83F2: format = RHI::TextureFormat::RGBA_DXT3; break;   // GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
			case 0x83F4: format = RHI::TextureFormat::RGBA_DXT5; break;   // GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
			case 0x8D64: format = RHI::TextureFormat::RGB_ETC1; break;    // GL_ETC1_RGB8_OES
			case 0x9274: format = RHI::TextureFormat::RGB_ETC2; break;    // GL_COMPRESSED_RGB8_ETC2
			case 0x9278: format = RHI::TextureFormat::RGBA_ETC2; break;   // GL_COMPRESSED_RGBA8_ETC2_EAC
			case 0x822D: format = RHI::TextureFormat::R_Float16; break;   // GL_R16F
			case 0x881A: format = RHI::TextureFormat::RGBA_Float16; break; // GL_RGBA16F
			// ATC (Adreno)
			case 0x8C92: format = RHI::TextureFormat::RGB_ATC; break;             // GL_ATC_RGB_AMD
			case 0x8C93: format = RHI::TextureFormat::RGBA_ATC_Explicit; break;   // GL_ATC_RGBA_EXPLICIT_ALPHA_AMD
			case 0x87EE: format = RHI::TextureFormat::RGBA_ATC_Interpolated; break; // GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD
			// PVRTC
			case 0x8C00: format = RHI::TextureFormat::RGB_PVRTC_2BPP; break;   // GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG
			case 0x8C01: format = RHI::TextureFormat::RGB_PVRTC_4BPP; break;   // GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG
			case 0x8C02: format = RHI::TextureFormat::RGBA_PVRTC_2BPP; break;  // GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG
			case 0x8C03: format = RHI::TextureFormat::RGBA_PVRTC_4BPP; break;  // GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG
			// ASTC
			case 0x93B0: format = RHI::TextureFormat::RGBA_ASTC_4x4; break;    // GL_COMPRESSED_RGBA_ASTC_4x4_KHR
			case 0x93B1: format = RHI::TextureFormat::RGBA_ASTC_5x4; break;    // GL_COMPRESSED_RGBA_ASTC_5x4_KHR
			case 0x93B2: format = RHI::TextureFormat::RGBA_ASTC_5x5; break;    // GL_COMPRESSED_RGBA_ASTC_5x5_KHR
			case 0x93B3: format = RHI::TextureFormat::RGBA_ASTC_6x5; break;    // GL_COMPRESSED_RGBA_ASTC_6x5_KHR
			case 0x93B4: format = RHI::TextureFormat::RGBA_ASTC_6x6; break;    // GL_COMPRESSED_RGBA_ASTC_6x6_KHR
			case 0x93B5: format = RHI::TextureFormat::RGBA_ASTC_8x5; break;    // GL_COMPRESSED_RGBA_ASTC_8x5_KHR
			case 0x93B6: format = RHI::TextureFormat::RGBA_ASTC_8x6; break;    // GL_COMPRESSED_RGBA_ASTC_8x6_KHR
			case 0x93B7: format = RHI::TextureFormat::RGBA_ASTC_8x8; break;    // GL_COMPRESSED_RGBA_ASTC_8x8_KHR
			case 0x93B8: format = RHI::TextureFormat::RGBA_ASTC_10x5; break;   // GL_COMPRESSED_RGBA_ASTC_10x5_KHR
			case 0x93B9: format = RHI::TextureFormat::RGBA_ASTC_10x6; break;   // GL_COMPRESSED_RGBA_ASTC_10x6_KHR
			case 0x93BA: format = RHI::TextureFormat::RGBA_ASTC_10x8; break;   // GL_COMPRESSED_RGBA_ASTC_10x8_KHR
			case 0x93BB: format = RHI::TextureFormat::RGBA_ASTC_10x10; break;  // GL_COMPRESSED_RGBA_ASTC_10x10_KHR
			case 0x93BC: format = RHI::TextureFormat::RGBA_ASTC_12x10; break;  // GL_COMPRESSED_RGBA_ASTC_12x10_KHR
			case 0x93BD: format = RHI::TextureFormat::RGBA_ASTC_12x12; break;  // GL_COMPRESSED_RGBA_ASTC_12x12_KHR
			default:
				LOGE("Unsupported KTX internal format: 0x{:x}", glInternalFormat);
				return false;
		}

		loadPixels(format);

		if (mipMapCount_ > 1) {
			LOGI("MIP Maps: {}", mipMapCount_);
			mipDataOffsets_ = std::make_unique<std::uint32_t[]>(mipMapCount_);
			mipDataSizes_ = std::make_unique<std::uint32_t[]>(mipMapCount_);
			std::uint32_t dataSizesSum = RHI::CalculateMipSizes(format, width_, height_, mipMapCount_, mipDataOffsets_.get(), mipDataSizes_.get());

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
