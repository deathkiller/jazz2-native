//#ifndef CLASS_NCINE_TEXTURESAVERWEBP
//#define CLASS_NCINE_TEXTURESAVERWEBP
//
//#include "ITextureSaver.h"
//
//namespace nCine {
//
///// WebP texture saver
//class TextureSaverWebP : public ITextureSaver
//{
//  public:
//	struct WebPProperties
//	{
//		WebPProperties()
//		    : quality(75.0f), lossless(false) {}
//
//		float quality;
//		bool lossless;
//	};
//
//	bool saveToFile(const Properties &properties, const char *filename) override;
//	bool saveToFile(const Properties &properties, std::unique_ptr<IFile> fileHandle) override;
//
//	bool saveToFile(const Properties &properties, const WebPProperties &webpProperties, const char *filename);
//	bool saveToFile(const Properties &properties, const WebPProperties &webpProperties, std::unique_ptr<IFile> fileHandle);
//};
//
//}
//
//#endif
