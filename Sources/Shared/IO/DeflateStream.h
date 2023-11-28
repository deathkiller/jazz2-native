#pragma once

#include "../Common.h"
#include "Stream.h"

#if defined(WITH_ZLIB)

#if !defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("zlib/zlib.h")
#		define __HAS_LOCAL_ZLIB
#	endif
#endif
#ifdef __HAS_LOCAL_ZLIB
#	include "zlib/zlib.h"
#else
#	include <zlib.h>
#endif

namespace Death::IO
{
	class DeflateStream : public Stream
	{
	public:
		DeflateStream()
			: _inputStream(nullptr), _inputSize(-1), _state(State::Unknown)
		{
			_size = INT32_MAX;
		}

		DeflateStream(Stream& inputStream, std::int32_t inputSize = -1, bool rawInflate = true)
			: DeflateStream()
		{
			Open(inputStream, inputSize, rawInflate);
		}

		~DeflateStream()
		{
			Close();
		}

		void Open(Stream& inputStream, std::int32_t inputSize = -1, bool rawInflate = true);

		void Close() override;
		std::int32_t Seek(std::int32_t offset, SeekOrigin origin) override;
		std::int32_t GetPosition() const override;
		std::int32_t Read(void* buffer, std::int32_t bytes) override;
		std::int32_t Write(const void* buffer, std::int32_t bytes) override;
		bool IsValid() const override;

		bool CeaseReading();

	protected:
		enum class State : std::uint8_t {
			Unknown,
			Created,
			Initialized,
			Finished,
			Failed
		};

		static constexpr std::int32_t BufferSize = 16384;

		Stream* _inputStream;
		z_stream _strm;
		std::int32_t _inputSize;
		State _state;
		bool _rawInflate;
		unsigned char _buffer[BufferSize];

		std::int32_t ReadInternal(void* ptr, std::int32_t size);
	};

	class DeflateWriter : public Stream
	{
	public:
		DeflateWriter(Stream& outputStream, std::int32_t compressionLevel = 9, bool rawInflate = true);
		~DeflateWriter();

		void Close() override;
		std::int32_t Seek(std::int32_t offset, SeekOrigin origin) override;
		std::int32_t GetPosition() const override;
		std::int32_t Read(void* buffer, std::int32_t bytes) override;
		std::int32_t Write(const void* buffer, std::int32_t bytes) override;
		bool IsValid() const override;

		static std::int32_t GetMaxDeflatedSize(std::int32_t uncompressedSize);

	protected:
		enum class State : std::uint8_t {
			Created,
			Initialized,
			Finished,
			Failed
		};

		static constexpr std::int32_t BufferSize = 16384;

		Stream* _outputStream;
		z_stream _strm;
		State _state;
		unsigned char _buffer[BufferSize];

		std::int32_t WriteInternal(const void* buffer, std::int32_t bytes, bool finish);
	};
}

#endif