#pragma once

#include "../../Main.h"

#include "RHI/RhiTypes.h"

namespace nCine
{
	/**
		@brief Backend-neutral pixel format descriptor of a texture

		Describes the pixel format of decoded or empty texture data as a backend-neutral @ref PixelFormat,
		tracks whether the data is compressed and whether the external channel order is BGR/BGRA. The
		backend translates the format into its own internal/external formats and pixel data types when the
		texture is uploaded.
	*/
	class TextureFormat
	{
	public:
		TextureFormat() : _pixelFormat(PixelFormat::Unknown), _bgr(false) {}
		explicit TextureFormat(PixelFormat format) : _pixelFormat(format), _bgr(false) {}

		/** @brief Returns the backend-neutral pixel format */
		inline PixelFormat pixelFormat() const {
			return _pixelFormat;
		}
		/** @brief Returns `true` if the format holds compressed data */
		bool isCompressed() const;
		/** @brief Returns `true` if the external channel order is BGR/BGRA instead of RGB/RGBA */
		inline bool isBgr() const {
			return _bgr;
		}
		/** @brief Returns the number of color channels */
		std::uint32_t numChannels() const;

		/** @brief Marks the external channel order as BGR/BGRA */
		void bgrFormat();

		/** @brief Calculates the pixel data offset and size for each MIP map level */
		static std::uint32_t calculateMipSizes(PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t mipMapCount, std::uint32_t* mipDataOffsets, std::uint32_t* mipDataSizes);

	private:
		PixelFormat _pixelFormat;
		bool _bgr;
	};

}
