#pragma once

#include "../../Common.h"
#include "../Stream.h"

#if defined(WITH_ZSTD) || defined(DOXYGEN_GENERATING_OUTPUT)

#if !defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("zstd/zstd.h")
#		define __HAS_LOCAL_ZSTD
#	endif
#endif
#ifdef __HAS_LOCAL_ZSTD
#	include "zstd/zstd.h"
#else
#	include <zstd.h>
#endif

namespace Death { namespace IO { namespace Compression {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Provides read-only streaming of compressed data using the Zstandard compression algorithm
	*/
	class ZstdStream : public Stream
	{
	public:
		ZstdStream();
		ZstdStream(Stream& inputStream, std::int32_t inputSize = -1);
		~ZstdStream();

		ZstdStream(const ZstdStream&) = delete;
		ZstdStream(ZstdStream&& other) noexcept;
		ZstdStream& operator=(const ZstdStream&) = delete;
		ZstdStream& operator=(ZstdStream&& other) noexcept;

		void Open(Stream& inputStream, std::int32_t inputSize = -1);

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;
		std::int64_t GetSize() const override;

		bool CeaseReading();

	private:
		enum class State : std::uint8_t {
			Unknown,
			Created,
			Initialized,
			Finished,
			Failed
		};

		Stream* _inputStream;
		ZSTD_DStream* _strm;
		std::int64_t _size;
		std::int32_t _inputSize;
		State _state;

		std::unique_ptr<char[]> _buffer;
		std::int32_t _inBufferPos;
		std::int32_t _inBufferLength;
		std::int32_t _inBufferCapacity;
		std::int32_t _outBufferPos;
		std::int32_t _outPosTotal;
		std::int32_t _outBufferLength;
		std::int32_t _outBufferCapacity;

		void InitializeInternal();
		std::int32_t ReadInternal(void* ptr, std::int32_t size);
	};

	/**
		@brief Provides write-only streaming to compress written data by using the Zstandard compression algorithm
	*/
	class ZstdWriter : public Stream
	{
	public:
		ZstdWriter(Stream& outputStream, std::int32_t compressionLevel = 0);
		~ZstdWriter();

		ZstdWriter(const ZstdWriter&) = delete;
		ZstdWriter& operator=(const ZstdWriter&) = delete;

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;
		std::int64_t GetSize() const override;

	private:
		enum class State : std::uint8_t {
			Created,
			Initialized,
			Finished,
			Failed
		};

		Stream* _outputStream;
		ZSTD_CStream* _strm;
		State _state;

		std::unique_ptr<char[]> _buffer;
		std::int32_t _inBufferCapacity;
		std::int32_t _outBufferPos;
		std::int32_t _outBufferLength;
		std::int32_t _outBufferCapacity;

		std::int32_t WriteInternal(const void* buffer, std::int32_t bytesToWrite, bool finish);
	};

}}}

#endif