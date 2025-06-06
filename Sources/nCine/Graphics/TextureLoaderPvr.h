#pragma once

#include <cstdint> // for header

#include "ITextureLoader.h"

namespace nCine
{
	/// PVR texture loader
	class TextureLoaderPvr : public ITextureLoader
	{
	public:
		explicit TextureLoaderPvr(std::unique_ptr<Death::IO::Stream> fileHandle);

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		/// Header for the PVR3 format
		struct Pvr3Header
		{
			std::uint32_t version;
			std::uint32_t flags;
			std::uint64_t pixelFormat;
			std::uint32_t colourSpace;
			std::uint32_t channelType;
			std::uint32_t height;
			std::uint32_t width;
			std::uint32_t depth;
			std::uint32_t numSurfaces;
			std::uint32_t numFaces;
			std::uint32_t numMipmaps;
			std::uint32_t metaDataSize;
		};
#endif

		static const std::uint64_t FMT_PVRTC_2BPP_RGB = 0;
		static const std::uint64_t FMT_PVRTC_2BPP_RGBA = 1;
		static const std::uint64_t FMT_PVRTC_4BPP_RGB = 2;
		static const std::uint64_t FMT_PVRTC_4BPP_RGBA = 3;
		static const std::uint64_t FMT_PVRTCII_2BPP = 4;
		static const std::uint64_t FMT_PVRTCII_4BPP = 5;
		static const std::uint64_t FMT_ETC1 = 6;
		static const std::uint64_t FMT_DXT1 = 7;
		static const std::uint64_t FMT_DXT3 = 9;
		static const std::uint64_t FMT_DXT5 = 11;

		static const std::uint64_t FMT_ETC2_RGB = 22;
		static const std::uint64_t FMT_ETC2_RGBA = 23;
		static const std::uint64_t FMT_ETC2_RGB_A1 = 24;
		static const std::uint64_t FMT_EAC_R11 = 25;
		static const std::uint64_t FMT_EAC_RG11 = 26;
		static const std::uint64_t FMT_ASTC_4x4 = 27;
		static const std::uint64_t FMT_ASTC_5x4 = 28;
		static const std::uint64_t FMT_ASTC_5x5 = 29;
		static const std::uint64_t FMT_ASTC_6x5 = 30;
		static const std::uint64_t FMT_ASTC_6x6 = 31;
		static const std::uint64_t FMT_ASTC_8x5 = 32;
		static const std::uint64_t FMT_ASTC_8x6 = 33;
		static const std::uint64_t FMT_ASTC_8x8 = 34;
		static const std::uint64_t FMT_ASTC_10x5 = 35;
		static const std::uint64_t FMT_ASTC_10x6 = 36;
		static const std::uint64_t FMT_ASTC_10x8 = 37;
		static const std::uint64_t FMT_ASTC_10x10 = 38;
		static const std::uint64_t FMT_ASTC_12x10 = 39;
		static const std::uint64_t FMT_ASTC_12x12 = 40;

		static const std::uint64_t FMT_BGRA_8888 = 0x0808080861726762ULL;
		static const std::uint64_t FMT_RGB_888 = 0x0008080800626772ULL; // rgb08880
		static const std::uint64_t FMT_RGBA_8888 = 0x0808080861626772ULL; // rgba8888
		static const std::uint64_t FMT_RGB_565 = 0x0005060500626772ULL; // rgb05650
		static const std::uint64_t FMT_RGBA_5551 = 0x0105050561626772ULL; // rgba5551
		static const std::uint64_t FMT_RGBA_4444 = 0x0404040461626772ULL; // rgba4444
		static const std::uint64_t FMT_LA_88 = 0x000008080000616cULL;
		static const std::uint64_t FMT_L_8 = 0x000000080000006cULL;
		static const std::uint64_t FMT_A_8 = 0x0000000800000061ULL;

		/// Reads the PVR3 header and fills the corresponding structure
		bool readHeader(Pvr3Header& header);
		/// Parses the PVR3 header to determine its format
		bool parseFormat(const Pvr3Header& header);
	};

}
