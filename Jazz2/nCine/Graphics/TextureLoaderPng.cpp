#include "TextureLoaderPng.h"

#if defined(__EMSCRIPTEN__)

#include <png.h>

namespace nCine
{
	static void readFromFileHandle(png_structp pngPtr, png_bytep outBytes, png_size_t byteCountToRead)
	{
		IFileStream* fileHandle = reinterpret_cast<IFileStream*>(png_get_io_ptr(pngPtr));

		unsigned long int bytesRead = fileHandle->Read(outBytes, static_cast<unsigned long int>(byteCountToRead));
		if (bytesRead > 0 && bytesRead != byteCountToRead) {
			//LOGW_X("Read %ul bytes instead of %ul", bytesRead, byteCountToRead);
		}
	}
}

#else

#if defined(_MSC_VER) && defined(__has_include)
#	if __has_include("../../../Libs/Deflate/libdeflate.h")
#		define __HAS_LOCAL_LIBDEFLATE
#	endif
#endif
#ifdef __HAS_LOCAL_LIBDEFLATE
#	include "../../../Libs/Deflate/libdeflate.h"
#else
#	include <libdeflate.h>
#endif

#include <SmallVector.h>

using namespace Death;

#endif

namespace nCine
{
	TextureLoaderPng::TextureLoaderPng(std::unique_ptr<IFileStream> fileHandle)
		: ITextureLoader(std::move(fileHandle))
	{
#if defined(__EMSCRIPTEN__)
		//LOGI_X("Loading \"%s\"", fileHandle_->filename());

		const int SignatureLength = 8;
		unsigned char signature[SignatureLength];
		fileHandle_->Open(FileAccessMode::Read);
		if (!fileHandle_->isOpened()) {
			return;
		}
		//RETURN_ASSERT_MSG_X(fileHandle_->isOpened(), "File \"%s\" cannot be opened", fileHandle_->filename());
		fileHandle_->Read(signature, SignatureLength);

		// Checking PNG signature
		const int isValid = png_check_sig(signature, SignatureLength);
		if (!isValid) {
			return;
		}
		//RETURN_ASSERT_MSG(isValid, "PNG signature check failed");

		// Get PNG file info struct (memory is allocated by libpng)
		png_structp pngPtr = nullptr;
		pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
		//RETURN_ASSERT_MSG(pngPtr, "Cannot create png read structure");
		if (!pngPtr) {
			return;
		}

		// Get PNG image data info struct (memory is allocated by libpng)
		png_infop infoPtr = nullptr;
		infoPtr = png_create_info_struct(pngPtr);

		if (infoPtr == nullptr) {
			png_destroy_read_struct(&pngPtr, nullptr, nullptr);
			//RETURN_MSG("Cannot create png info structure");
			return;
		}

		// Setting custom read function that uses an IFile as input
		png_set_read_fn(pngPtr, fileHandle_.get(), readFromFileHandle);
		// Telling libpng the signature has already be read
		png_set_sig_bytes(pngPtr, SignatureLength);

		png_read_info(pngPtr, infoPtr);

		png_uint_32 width = 0;
		png_uint_32 height = 0;
		int bitDepth = 0;
		int colorType = -1;
		png_uint_32 retVal = png_get_IHDR(pngPtr, infoPtr, &width, &height, &bitDepth, &colorType, nullptr, nullptr, nullptr);

		if (retVal != 1) {
			png_destroy_read_struct(&pngPtr, &infoPtr, nullptr);
			//RETURN_MSG("Cannot create png info structure");
			return;
		}

		width_ = width;
		height_ = height;
		mipMapCount_ = 1; // No MIP Mapping
		//LOGI_X("Header found: w:%d, h:%d, bitDepth:%d, colorType:%s", width_, height_, bitDepth, colorTypeString(colorType));

		switch (colorType) {
			case PNG_COLOR_TYPE_RGB_ALPHA:
				texFormat_ = TextureFormat(GL_RGBA8);
				break;
			case PNG_COLOR_TYPE_RGB:
				texFormat_ = TextureFormat(GL_RGB8);
				break;
			case PNG_COLOR_TYPE_PALETTE:
				png_set_palette_to_rgb(pngPtr);
				texFormat_ = TextureFormat(GL_RGB8);
				break;
			case PNG_COLOR_TYPE_GRAY_ALPHA:
				if (bitDepth < 8)
					png_set_expand_gray_1_2_4_to_8(pngPtr);
				texFormat_ = TextureFormat(GL_RG8);
				break;
			case PNG_COLOR_TYPE_GRAY:
				if (bitDepth < 8)
					png_set_expand_gray_1_2_4_to_8(pngPtr);
				texFormat_ = TextureFormat(GL_R8);
				break;
			default:
				png_destroy_read_struct(&pngPtr, &infoPtr, nullptr);
				//RETURN_MSG_X("Color type not supported: %d", colorType);
				return;
		}

		if (png_get_valid(pngPtr, infoPtr, PNG_INFO_tRNS)) {
			png_set_tRNS_to_alpha(pngPtr);
			texFormat_ = TextureFormat(GL_RGBA8);
		}
		if (bitDepth == 16)
			png_set_strip_16(pngPtr);

		png_read_update_info(pngPtr, infoPtr);
		// Row size in bytes
		const png_size_t bytesPerRow = png_get_rowbytes(pngPtr, infoPtr);

		dataSize_ = bytesPerRow * height_;
		pixels_ = std::make_unique<unsigned char[]>(static_cast<unsigned long>(dataSize_));

		std::unique_ptr<png_bytep[]> rowPointers = std::make_unique<png_bytep[]>(height_);
		for (int i = 0; i < height_; i++) {
			rowPointers[i] = pixels_.get() + i * bytesPerRow;
		}

		// Decoding png data through row pointers
		png_read_image(pngPtr, rowPointers.get());

		png_destroy_read_struct(&pngPtr, &infoPtr, nullptr);
		hasLoaded_ = true;
#else
		constexpr uint8_t PngSignature[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };
		constexpr uint8_t PngTypeIndexed = 1;
		constexpr uint8_t PngTypeColor = 2;

		// Lightweight PNG reader using libdeflate
		fileHandle_->Open(FileAccessMode::Read);

		// Check header signature
		uint8_t internalBuffer[sizeof(PngSignature)];
		fileHandle_->Read(internalBuffer, sizeof(PngSignature));
		if (memcmp(internalBuffer, PngSignature, sizeof(PngSignature)) != 0) {
			return;
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
				return;
			}

			int blockEndPosition = (int)fileHandle_->GetPosition() + length;

			switch (type) {
				case 'IHDR': {
					if (headerParsed) {
						// Duplicate header
						return;
					}

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
						return;
					}
					if (interlace != 0) {
						// Interlacing is not supported
						return;
					}

					headerParsed = true;
					break;
				}

				case 'IDAT': {
					int prevlength = (int)data.size();
					int newLength = length + length;
					data.resize_for_overwrite(newLength);
					fileHandle_->Read(data.data() + prevlength, length);
					break;
				}

				case 'IEND': {
					auto decompressor = libdeflate_alloc_decompressor();

					size_t dataLength = 16 + (width_ * height_ * 5);
					auto buffer = std::make_unique<GLubyte[]>(dataLength);

					size_t bytesRead;
					auto result = libdeflate_deflate_decompress(decompressor, data.data() + 2, data.size() - 2, buffer.get(), dataLength, &bytesRead);
					libdeflate_free_decompressor(decompressor);
					if (result != LIBDEFLATE_SUCCESS) {
						return;
					}

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
								memcpy(&pixels_[y * dstStride + 4 * i], &bufferRow[3 * i], 3);
								pixels_[y * dstStride + 4 * i + 3] = 255;
							}
						} else {
							memcpy(&pixels_[y * dstStride], bufferRow, srcStride);
						}

						memcpy(bufferPrev.get(), bufferRow, srcStride);
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
				return;
			}

			// Skip CRC
			fileHandle_->Seek(4, SeekOrigin::Current);
		}
#endif
	}

#if !defined(__EMSCRIPTEN__)
	int TextureLoaderPng::ReadInt32BigEndian(const std::unique_ptr<IFileStream>& s)
	{
		uint32_t value;
		s->Read(&value, sizeof(value));
		return IFileStream::int32FromBE(value);
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
#endif
}
