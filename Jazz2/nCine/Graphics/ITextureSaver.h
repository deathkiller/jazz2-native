//#pragma once
//
//#include <memory>
//
//namespace nCine
//{
//	class IFile;
//
//	/// Texture saver interface class
//	class ITextureSaver
//	{
//	public:
//		enum class Format
//		{
//			RGB8,
//			RGBA8,
//			RGB_FLOAT
//		};
//
//		struct Properties
//		{
//			Properties()
//				: width(0), height(0), format(Format::RGB8), verticalFlip(false), pixels(nullptr) {}
//
//			int width;
//			int height;
//			Format format;
//			bool verticalFlip;
//			void* pixels;
//		};
//
//		virtual ~ITextureSaver();
//
//		virtual bool saveToFile(const Properties& properties, const char* filename) = 0;
//		virtual bool saveToFile(const Properties& properties, std::unique_ptr<IFile> fileHandle) = 0;
//
//		unsigned int bpp(const Format format);
//		unsigned int dataSize(const Properties& properties);
//		void flipPixels(const Properties& properties, unsigned char* dest);
//	};
//
//}