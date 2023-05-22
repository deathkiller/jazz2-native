//#include <webp/encode.h>
//#include "common_macros.h"
//#include "TextureSaverWebP.h"
//#include "IFile.h"
//
//namespace nCine {
//
/////////////////////////////////////////////////////////////
//// PUBLIC FUNCTIONS
/////////////////////////////////////////////////////////////
//
//bool TextureSaverWebP::saveToFile(const Properties &properties, const char *filename)
//{
//	return saveToFile(properties, WebPProperties(), IFileStream::createFileHandle(filename));
//}
//
//bool TextureSaverWebP::saveToFile(const Properties &properties, std::unique_ptr<IFile> fileHandle)
//{
//	return saveToFile(properties, WebPProperties(), std::move(fileHandle));
//}
//
//bool TextureSaverWebP::saveToFile(const Properties &properties, const WebPProperties &webpProperties, const char *filename)
//{
//	return saveToFile(properties, webpProperties, IFileStream::createFileHandle(filename));
//}
//
//bool TextureSaverWebP::saveToFile(const Properties &properties, const WebPProperties &webpProperties, std::unique_ptr<IFile> fileHandle)
//{
//	//FATAL_ASSERT(properties.width > 0);
//	//FATAL_ASSERT(properties.height > 0);
//	//FATAL_ASSERT_MSG(properties.width <= 16383, "Image width exceeds WebP maximum pixel dimensions");
//	//FATAL_ASSERT_MSG(properties.height <= 16383, "Image height exceeds WebP maximum pixel dimensions");
//	//FATAL_ASSERT(properties.pixels != nullptr);
//	//ASSERT(properties.format == Format::RGB8 || properties.format == Format::RGBA8);
//
//	LOGI("Saving \"%s\"", fileHandle->filename());
//	if (fileHandle->IsValid() == false)
//		return false;
//
//	// Flip pixels data vertically if the corresponding flag is true
//	std::unique_ptr<unsigned char[]> flippedPixels;
//	if (properties.verticalFlip)
//	{
//		flippedPixels = std::make_unique<unsigned char[]>(dataSize(properties));
//		flipPixels(properties, flippedPixels.get());
//	}
//	void *texturePixels = properties.verticalFlip ? flippedPixels.get() : properties.pixels;
//
//	// Using the WebP Simple Encoding API
//	const uint8_t *pixels = static_cast<uint8_t *>(texturePixels);
//	uint8_t *output = nullptr;
//	size_t bytesWritten = 0;
//	switch (properties.format)
//	{
//		default:
//		case Format::RGB8:
//		{
//			if (webpProperties.lossless)
//				bytesWritten = WebPEncodeLosslessRGB(pixels, properties.width, properties.height, properties.width * bpp(properties.format), &output);
//			else
//				bytesWritten = WebPEncodeRGB(pixels, properties.width, properties.height, properties.width * bpp(properties.format), webpProperties.quality, &output);
//			break;
//		}
//		case Format::RGBA8:
//		{
//			if (webpProperties.lossless)
//				bytesWritten = WebPEncodeLosslessRGBA(pixels, properties.width, properties.height, properties.width * bpp(properties.format), &output);
//			else
//				bytesWritten = WebPEncodeRGBA(pixels, properties.width, properties.height, properties.width * bpp(properties.format), webpProperties.quality, &output);
//			break;
//		}
//	}
//
//	if (bytesWritten > 0)
//		fileHandle->write(output, bytesWritten);
//
//	fileHandle->close();
//	WebPFree(output);
//
//	return (bytesWritten > 0);
//}
//
//}
