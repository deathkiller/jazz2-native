#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../CommonHeaders.h"
#endif

#include "../../Main.h"

namespace nCine
{
	/**
		@brief OpenGL pixel format of a texture
		
		Resolves an OpenGL internal format into the matching external format and pixel data type,
		tracks whether the data is compressed and verifies that the GPU supports the format.
	*/
	class TextureFormat
	{
	public:
		TextureFormat() : internalFormat_(-1), format_(-1), type_(-1), isCompressed_(false) {}
		explicit TextureFormat(GLenum internalFormat);
		TextureFormat(GLenum internalFormat, GLenum type);

		/** @brief Returns the internal format */
		inline GLenum internalFormat() const {
			return internalFormat_;
		}
		/** @brief Returns the corresponding external format */
		inline GLenum format() const {
			return format_;
		}
		/** @brief Returns the corresponding pixel data type */
		inline GLenum type() const {
			return type_;
		}
		/** @brief Returns `true` if the format holds compressed data */
		inline bool isCompressed() const {
			return isCompressed_;
		}
		/** @brief Returns the number of color channels */
		std::uint32_t numChannels() const;

		/** @brief Converts the external format to the corresponding BGR one */
		void bgrFormat();

		/** @brief Calculates the pixel data offset and size for each MIP map level */
		static std::uint32_t calculateMipSizes(GLenum internalFormat, std::int32_t width, std::int32_t height, std::int32_t mipMapCount, std::uint32_t* mipDataOffsets, std::uint32_t* mipDataSizes);

	private:
		GLenum internalFormat_;
		GLenum format_;
		GLenum type_;
		bool isCompressed_;

		/** @brief Searches for a match between an integer internal format and an external one */
		bool integerFormat();
		/** @brief Searches for a match between a non-integer internal format and an external one */
		bool nonIntegerFormat();
		/** @brief Searches for a match between a floating point internal format and an external one */
		bool floatFormat();
		/** @brief Searches for a match between a compressed internal format and an external one */
		bool compressedFormat();
#if defined(WITH_OPENGLES)
		/** @brief Searches for a match between an OpenGL ES compressed internal format and an external one */
		bool oesCompressedFormat();
#endif

		/** @brief Determines the external format corresponding to the internal one */
		void findExternalFormat();
		/** @brief Checks whether the internal format is supported by the GPU */
		void checkFormatSupport() const;
	};

}

