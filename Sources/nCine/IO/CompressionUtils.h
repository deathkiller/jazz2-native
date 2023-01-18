#pragma once

#include "../../Common.h"

#if defined(WITH_ZLIB)
#	include <zlib.h>
#else
#	if !defined(CMAKE_BUILD) && defined(__has_include)
#		if __has_include("../../../Libs/Includes/libdeflate.h")
#			define __HAS_LOCAL_LIBDEFLATE
#		endif
#	endif
#	ifdef __HAS_LOCAL_LIBDEFLATE
#		include "../../../Libs/Includes/libdeflate.h"
#	else
#		include <libdeflate.h>
#	endif
#endif

namespace nCine
{
	enum class DecompressionResult {
		Unknown,
		Success,
		CorruptedData,
		BufferTooSmall
	};

	class CompressionUtils
	{
	public:
		static int32_t Deflate(const uint8_t* srcBuffer, int32_t srcSize, uint8_t* destBuffer, int32_t destSize);
		static DecompressionResult Inflate(const uint8_t* compressedBuffer, int32_t& compressedSize, uint8_t* destBuffer, int32_t& uncompressedSize);
		static int32_t GetMaxDeflatedSize(int32_t uncompressedSize);

	protected:
		/// Deleted copy constructor
		CompressionUtils(const CompressionUtils&) = delete;
		/// Deleted assignment operator
		CompressionUtils& operator=(const CompressionUtils&) = delete;

#if !defined(WITH_ZLIB)
		class LibdeflateStaticDecompressor
		{
		public:
			LibdeflateStaticDecompressor();
			~LibdeflateStaticDecompressor();

			libdeflate_decompressor* Instance;
		};

		static thread_local LibdeflateStaticDecompressor _staticDecompressor;
#endif
	};
}