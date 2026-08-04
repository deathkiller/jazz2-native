//#include "return_macros.h"
//#include "TextureLoaderWebP.h"
//
//namespace nCine {
//
/////////////////////////////////////////////////////////////
//// CONSTRUCTORS and DESTRUCTOR
/////////////////////////////////////////////////////////////
//
//TextureLoaderWebP::TextureLoaderWebP(std::unique_ptr<IFile> fileHandle)
//    : ITextureLoader(std::move(fileHandle))
//{
//	LOGI("Loading \"{}\"", _fileHandle->filename());
//
//	// Loading the whole file in memory
//	DEATH_ASSERT(_fileHandle->IsValid(), ("File \"{}\" cannot be opened", _fileHandle->GetPath()), );
//	const long int fileSize = _fileHandle->size();
//	std::unique_ptr<unsigned char[]> fileBuffer = std::make_unique<unsigned char[]>(fileSize);
//	_fileHandle->read(fileBuffer.get(), fileSize);
//
//	if (WebPGetInfo(fileBuffer.get(), fileSize, &_width, &_height) == 0)
//	{
//		fileBuffer.reset(nullptr);
//		RETURN_MSG("Cannot read WebP header");
//	}
//
//	LOGI("Header found: w:{} h:{}", _width, _height);
//
//	WebPBitstreamFeatures features;
//	if (WebPGetFeatures(fileBuffer.get(), fileSize, &features) != VP8_STATUS_OK)
//	{
//		fileBuffer.reset(nullptr);
//		RETURN_MSG("Cannot retrieve WebP features from headers");
//	}
//
//	LOGI("Bitstream features found: alpha:{} animation:{} format:{}",
//	       features.has_alpha, features.has_animation, features.format);
//
//	_mipMapCount = 1; // No MIP Mapping
//	_texFormat = features.has_alpha ? TextureFormat(PixelFormat::RGBA8) : TextureFormat(PixelFormat::RGB8);
//	_dataSize = _width * _height * _texFormat.numChannels();
//	_pixels = std::make_unique<unsigned char[]>(_dataSize);
//
//	if (features.has_alpha)
//	{
//		if (WebPDecodeRGBAInto(fileBuffer.get(), fileSize, _pixels.get(), _dataSize, _width * 4) == nullptr)
//		{
//			fileBuffer.reset(nullptr);
//			_pixels.reset(nullptr);
//			RETURN_MSG("Cannot decode RGBA WebP image");
//		}
//	}
//	else
//	{
//		if (WebPDecodeRGBInto(fileBuffer.get(), fileSize, _pixels.get(), _dataSize, _width * 3) == nullptr)
//		{
//			fileBuffer.reset(nullptr);
//			_pixels.reset(nullptr);
//			RETURN_MSG("Cannot decode RGB WebP image");
//		}
//	}
//
//	_hasLoaded = true;
//}
//
//}
