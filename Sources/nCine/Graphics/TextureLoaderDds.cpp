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
		RHI::TextureFormat format = RHI::TextureFormat::RGB8; // default
		bool isBGR = false;

		const uint32_t flags = AsLE(header.ddspf.dwFlags);

		// Texture contains compressed RGB data, dwFourCC contains valid data
		if (flags & DDPF_FOURCC) {
			const uint32_t fourCC = AsLE(header.ddspf.dwFourCC);

			const char* fourCCchars = reinterpret_cast<const char*>(&fourCC);
			LOGI("FourCC: \"{:c}{:c}{:c}{:c}\" (0x{:x})", fourCCchars[0], fourCCchars[1], fourCCchars[2], fourCCchars[3], fourCC);

			// Parsing the FourCC format
			switch (fourCC) {
				case DDS_DXT1:
					format = RHI::TextureFormat::RGB_DXT1;
					break;
				case DDS_DXT3:
					format = RHI::TextureFormat::RGBA_DXT3;
					break;
				case DDS_DXT5:
					format = RHI::TextureFormat::RGBA_DXT5;
					break;
#if defined(WITH_OPENGLES)
				case DDS_ETC1:
					format = RHI::TextureFormat::RGB_ETC1;
					break;
				case DDS_ATC:
					format = RHI::TextureFormat::RGB_ATC;
					break;
				case DDS_ATCA:
					format = RHI::TextureFormat::RGBA_ATC_Explicit;
					break;
				case DDS_ATCI:
					format = RHI::TextureFormat::RGBA_ATC_Interpolated;
					break;
#endif
				default:
					LOGE("Unsupported FourCC compression code: {}", fourCC);
					return false;
			}

			loadPixels(format);
		} else {
			// Texture contains uncompressed data
			const uint32_t bitCount = AsLE(header.ddspf.dwRGBBitCount);
			const uint32_t redMask = AsLE(header.ddspf.dwRBitMask);
			const uint32_t greenMask = AsLE(header.ddspf.dwGBitMask);
			const uint32_t blueMask = AsLE(header.ddspf.dwBBitMask);
			const uint32_t alphaMask = AsLE(header.ddspf.dwABitMask);

			LOGI("Pixel masks ({}bit): R:0x{:x} G:0x{:x} B:0x{:x} A:0x{:x}", bitCount, redMask, greenMask, blueMask, alphaMask);

			// Texture contains uncompressed RGB data
			if (flags & DDPF_RGB || flags & (DDPF_RGB | DDPF_ALPHAPIXELS)) {
				if ((redMask == 0x00FF0000 && greenMask == 0x0000FF00 && blueMask == 0x000000FF && alphaMask == 0x0) ||
					(blueMask == 0x00FF0000 && greenMask == 0x0000FF00 && redMask == 0x000000FF && alphaMask == 0x0)) { // 888
					format = RHI::TextureFormat::RGB8;
				} else if ((alphaMask == 0xFF000000 && redMask == 0x00FF0000 && greenMask == 0x0000FF00 && blueMask == 0x000000FF) ||
						 (alphaMask == 0xFF000000 && blueMask == 0x00FF0000 && greenMask == 0x0000FF00 && redMask == 0x000000FF)) { // 8888
					format = RHI::TextureFormat::RGBA8;
				} else {
					LOGE("Unsupported DDPF_RGB pixel format");
					return false;
				}
			} else if (flags & (DDPF_LUMINANCE | DDPF_ALPHAPIXELS)) {
				format = RHI::TextureFormat::RG8;
			} else if (flags & DDPF_LUMINANCE) {
				format = RHI::TextureFormat::R8;
			} else if (flags & DDPF_ALPHA) {
				format = RHI::TextureFormat::R8;
			} else {
				LOGE("Unsupported DDS uncompressed pixel format: {}", flags);
				return false;
			}

			loadPixels(format);
			isBGR = (redMask > blueMask && bitCount > 16);
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
