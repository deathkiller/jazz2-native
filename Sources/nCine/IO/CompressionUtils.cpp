#include "CompressionUtils.h"

#include <algorithm>

namespace nCine
{
#if !defined(WITH_ZLIB)
	thread_local CompressionUtils::LibdeflateStaticDecompressor CompressionUtils::_staticDecompressor;
#endif

	int32_t CompressionUtils::Deflate(const uint8_t* srcBuffer, int32_t srcSize, uint8_t* destBuffer, int32_t destSize)
	{
#if defined(WITH_ZLIB)
		z_stream strm;
		strm.zalloc = Z_NULL;
		strm.zfree = Z_NULL;
		strm.opaque = Z_NULL;
		strm.avail_in = srcSize;
		strm.next_in = (unsigned char*)srcBuffer;
		strm.avail_out = destSize;
		strm.next_out = (unsigned char*)destBuffer;
		int result = deflateInit2(&strm, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
		if (result != Z_OK) {
			return 0;
		}

		result = deflate(&strm, Z_FINISH);
		deflateEnd(&strm);
		if (result == Z_STREAM_ERROR) {
			return 0;
		}

		return (int32_t)(destSize - strm.avail_out);
#else
		libdeflate_compressor* compressor = libdeflate_alloc_compressor(9);
		size_t bytesWritten = libdeflate_deflate_compress(compressor, srcBuffer, srcSize, destBuffer, destSize);
		libdeflate_free_compressor(compressor);

		return (int32_t)bytesWritten;
#endif
	}

	DecompressionResult CompressionUtils::Inflate(const uint8_t* compressedBuffer, int32_t& compressedSize, uint8_t* destBuffer, int32_t& uncompressedSize)
	{
#if defined(WITH_ZLIB)
		z_stream strm;
		strm.zalloc = Z_NULL;
		strm.zfree = Z_NULL;
		strm.opaque = Z_NULL;
		strm.avail_in = compressedSize;
		strm.next_in = (unsigned char*)compressedBuffer;
		strm.avail_out = uncompressedSize;
		strm.next_out = (unsigned char*)destBuffer;
		int result = inflateInit2(&strm, -15);
		if (result != Z_OK) {
			return DecompressionResult::CorruptedData;
		}

		// Z_FINISH doesn't work sometimes, use Z_NO_FLUSH instead
		result = inflate(&strm, /*Z_FINISH*/Z_NO_FLUSH);
		inflateEnd(&strm);
		if (result != Z_OK && result != Z_STREAM_END) {
			return (result == Z_BUF_ERROR ? DecompressionResult::BufferTooSmall : DecompressionResult::CorruptedData);
		}

		compressedSize -= strm.avail_in;
		uncompressedSize -= strm.avail_out;
#else
		size_t inBytesRead, outBytesRead;
		libdeflate_result result = libdeflate_deflate_decompress_ex(_staticDecompressor.Instance, compressedBuffer, compressedSize, destBuffer, uncompressedSize, &inBytesRead, &outBytesRead);
		if (result != LIBDEFLATE_SUCCESS) {
			return (result == LIBDEFLATE_INSUFFICIENT_SPACE ? DecompressionResult::BufferTooSmall : DecompressionResult::CorruptedData);
		}

		compressedSize = (int32_t)inBytesRead;
		uncompressedSize = (int32_t)outBytesRead;
#endif
		return DecompressionResult::Success;
	}

	int32_t CompressionUtils::GetMaxDeflatedSize(int32_t uncompressedSize)
	{
		constexpr int32_t MinBlockSize = 5000;
		int32_t max_blocks = std::max((uncompressedSize + MinBlockSize - 1) / MinBlockSize, 1);
		return uncompressedSize + 5 * max_blocks + 1 + 8;
	}

#if !defined(WITH_ZLIB)
	CompressionUtils::LibdeflateStaticDecompressor::LibdeflateStaticDecompressor()
	{
		Instance = libdeflate_alloc_decompressor();
	}

	CompressionUtils::LibdeflateStaticDecompressor::~LibdeflateStaticDecompressor()
	{
		if (Instance != nullptr) {
			libdeflate_free_decompressor(Instance);
			Instance = nullptr;
		}
	}
#endif
}
