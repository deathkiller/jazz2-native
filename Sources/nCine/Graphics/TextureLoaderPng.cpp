#include "TextureLoaderPng.h"

#include <Containers/SmallVector.h>
#include <IO/DeflateStream.h>
#include <IO/MemoryStream.h>

using namespace Death::Containers;
using namespace Death::IO;

namespace nCine
{
	TextureLoaderPng::TextureLoaderPng(std::unique_ptr<Stream> fileHandle)
		: ITextureLoader(std::move(fileHandle))
	{
		constexpr uint8_t PngSignature[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };
		constexpr uint8_t PngTypeIndexed = 1;
		constexpr uint8_t PngTypeColor = 2;

		if (!fileHandle_->IsValid()) {
			return;
		}

		// Check header signature
		uint8_t internalBuffer[sizeof(PngSignature)];
		fileHandle_->Read(internalBuffer, sizeof(PngSignature));
		if (std::memcmp(internalBuffer, PngSignature, sizeof(PngSignature)) != 0) {
			RETURN_MSG("Invalid PNG signature");
		}

		// Load image
		bool headerParsed = false;
		bool isPaletted = false;
		bool is24Bit = false;

		SmallVector<uint8_t, 0> data;

		while (true) {
			int length = ReadInt32BigEndian(fileHandle_);
			uint32_t type = ReadInt32BigEndian(fileHandle_);

			if (!headerParsed && type != 'IHDR') {
				// Header does not appear first
				RETURN_MSG("Invalid IHDR signature");
			}

			int blockEndPosition = (int)fileHandle_->GetPosition() + length;

			switch (type) {
				case 'IHDR': {
					RETURN_ASSERT_MSG(!headerParsed, "PNG file is corrupted");

					width_ = ReadInt32BigEndian(fileHandle_);
					height_ = ReadInt32BigEndian(fileHandle_);

					uint8_t bitDepth;
					fileHandle_->Read(&bitDepth, sizeof(bitDepth));
					uint8_t colorType;
					fileHandle_->Read(&colorType, sizeof(colorType));

					if (bitDepth == 8 && colorType == (PngTypeIndexed | PngTypeColor)) {
						isPaletted = true;
					}
					is24Bit = (colorType == PngTypeColor);

					int dataLength = width_ * height_;
					if (!isPaletted) {
						dataLength *= 4;
					}

					pixels_ = std::make_unique<GLubyte[]>(dataLength);

					uint8_t compression;
					fileHandle_->Read(&compression, sizeof(compression));
					uint8_t filter;
					fileHandle_->Read(&filter, sizeof(filter));
					uint8_t interlace;
					fileHandle_->Read(&interlace, sizeof(interlace));

					if (compression != 0) {
						// Compression method is not supported
						RETURN_MSG("PNG file is not supported");
					}
					if (interlace != 0) {
						// Interlacing is not supported
						RETURN_MSG("PNG file is not supported");
					}

					headerParsed = true;
					break;
				}

				case 'IDAT': {
					int prevlength = (int)data.size();
					int newLength = prevlength + length;
					data.resize_for_overwrite(newLength);
					fileHandle_->Read(data.data() + prevlength, length);
					break;
				}

				case 'IEND': {
					size_t dataLength = 16 + (width_ * height_ * 5);
					auto buffer = std::make_unique<GLubyte[]>(dataLength);

					MemoryStream ms(data.data() + 2, data.size() - 2);
					DeflateStream uc(ms, dataLength);
					uc.Read(buffer.get(), dataLength);
					RETURN_ASSERT_MSG(uc.IsValid(), "PNG file cannot be decompressed");

					int o = 0;
					int pxStride = (isPaletted ? 1 : (is24Bit ? 3 : 4));
					int srcStride = width_ * pxStride;
					int dstStride = width_ * (isPaletted ? 1 : 4);

					auto bufferPrev = std::make_unique<GLubyte[]>(srcStride);

					for (int y = 0; y < height_; y++) {
						// Read filter
						uint8_t filter = buffer[o++];

						// Read data
						GLubyte* bufferRow = &buffer[o];
						o += srcStride;

						for (int i = 0; i < srcStride; i++) {
							if (i < pxStride) {
								bufferRow[i] = UnapplyFilter(filter, bufferRow[i], 0, bufferPrev[i], 0);
							} else {
								bufferRow[i] = UnapplyFilter(filter, bufferRow[i], bufferRow[i - pxStride], bufferPrev[i], bufferPrev[i - pxStride]);
							}
						}

						if (is24Bit) {
							for (int i = 0; i < srcStride; i++) {
								std::memcpy(&pixels_[y * dstStride + 4 * i], &bufferRow[3 * i], 3);
								pixels_[y * dstStride + 4 * i + 3] = 255;
							}
						} else {
							std::memcpy(&pixels_[y * dstStride], bufferRow, srcStride);
						}

						std::memcpy(bufferPrev.get(), bufferRow, srcStride);
					}

					mipMapCount_ = 1;
					texFormat_ = TextureFormat(GL_RGBA8);
					hasLoaded_ = true;
					return;
				}

				default: {
					fileHandle_->Seek(length, SeekOrigin::Current);
					break;
				}
			}

			if (fileHandle_->GetPosition() != blockEndPosition) {
				// Block has incorrect length
				RETURN_MSG("PNG file is corrupted");
			}

			// Skip CRC
			fileHandle_->Seek(4, SeekOrigin::Current);
		}

		RETURN_MSG("PNG file is corrupted");
	}

	int TextureLoaderPng::ReadInt32BigEndian(const std::unique_ptr<Stream>& s)
	{
		uint32_t value;
		s->Read(&value, sizeof(value));
		return Stream::Uint32FromBE(value);
	}

	uint8_t TextureLoaderPng::UnapplyFilter(uint8_t filter, uint8_t x, uint8_t a, uint8_t b, uint8_t c)
	{
		constexpr uint8_t PngFilterNone = 0;
		constexpr uint8_t PngFilterSub = 1;
		constexpr uint8_t PngFilterUp = 2;
		constexpr uint8_t PngFilterAverage = 3;
		constexpr uint8_t PngFilterPaeth = 4;

		switch (filter) {
			case PngFilterNone: return x;
			case PngFilterSub: return (uint8_t)(x + a);
			case PngFilterUp: return (uint8_t)(x + b);
			case PngFilterAverage: return (uint8_t)(x + (a + b) / 2);
			case PngFilterPaeth: {
				int p = a + b - c;
				int pa = std::abs(p - a);
				int pb = std::abs(p - b);
				int pc = std::abs(p - c);
				return (uint8_t)(x + ((pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c));
			}

			// Unsupported filter specified
			default: return 0;
		}
	}
}
