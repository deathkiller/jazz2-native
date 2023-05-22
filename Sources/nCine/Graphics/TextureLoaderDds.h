#pragma once

#include "ITextureLoader.h"

#include <memory>

namespace nCine
{
	/// DDS texture loader
	class TextureLoaderDds : public ITextureLoader
	{
	public:
		explicit TextureLoaderDds(std::unique_ptr<Death::IO::Stream> fileHandle);

	private:
		/// Header for the DDS pixel format
		struct DdsPixelformat
		{
			std::uint32_t dwSize;
			std::uint32_t dwFlags;
			std::uint32_t dwFourCC;
			std::uint32_t dwRGBBitCount;
			std::uint32_t dwRBitMask;
			std::uint32_t dwGBitMask;
			std::uint32_t dwBBitMask;
			std::uint32_t dwABitMask;
		};

		/// Header for the DDS format
		struct DdsHeader
		{
			std::uint32_t dwMagic;
			std::uint32_t dwSize;
			std::uint32_t dwFlags;
			std::uint32_t dwHeight;
			std::uint32_t dwWidth;
			std::uint32_t dwPitchOrLinearSize;
			std::uint32_t dwDepth;
			std::uint32_t dwMipMapCount;
			std::uint32_t dwReserved1[11];
			DdsPixelformat ddspf;
			std::uint32_t dwCaps;
			std::uint32_t dwCaps2;
			std::uint32_t dwCaps3;
			std::uint32_t dwCaps4;
			std::uint32_t dwReserved2;
		};

		static const std::uint32_t DDPF_ALPHAPIXELS = 0x1;
		static const std::uint32_t DDPF_ALPHA = 0x2;
		static const std::uint32_t DDPF_FOURCC = 0x4;
		static const std::uint32_t DDPF_RGB = 0x40;
		static const std::uint32_t DDPF_YUV = 0x200;
		static const std::uint32_t DDPF_LUMINANCE = 0x20000;

		static const std::uint32_t DDS_R8G8B8 = 20;
		static const std::uint32_t DDS_A8R8G8B8 = 21;
		static const std::uint32_t DDS_R5G6B5 = 23;
		static const std::uint32_t DDS_A1R5G5B5 = 25;
		static const std::uint32_t DDS_A4R4G4B4 = 26;
		static const std::uint32_t DDS_DXT1 = 0x31545844; // "DXT1"
		static const std::uint32_t DDS_DXT3 = 0x33545844; // "DXT3"
		static const std::uint32_t DDS_DXT5 = 0x35545844; // "DXT5"
		static const std::uint32_t DDS_ETC1 = 0x31435445; // "ETC1"
		static const std::uint32_t DDS_ATC = 0x20435441; // "ATC "
		static const std::uint32_t DDS_ATCA = 0x41435441; // "ATCA"
		static const std::uint32_t DDS_ATCI = 0x49435441; // "ATCI"

		/// Reads the DDS header and fills the corresponding structure
		bool readHeader(DdsHeader& header);
		/// Parses the DDS header to determine its format
		bool parseFormat(const DdsHeader& header);
	};

}
