#pragma once

#include "../../Common.h"
#include "../Stream.h"

#if defined(WITH_ZLIB) || defined(WITH_MINIZ) || defined(DOXYGEN_GENERATING_OUTPUT)

#if defined(WITH_MINIZ)
#	if !defined(CMAKE_BUILD) && defined(__has_include)
#		if __has_include("miniz/miniz.h")
#			define __HAS_LOCAL_MINIZ
#		endif
#	endif
#	ifdef __HAS_LOCAL_MINIZ
#		include "miniz/miniz.h"
#	else
#		include <miniz.h>
#	endif
#else
#	if !defined(CMAKE_BUILD) && defined(__has_include)
#		if __has_include("zlib/zlib.h")
#			define __HAS_LOCAL_ZLIB
#		endif
#	endif
#	ifdef __HAS_LOCAL_ZLIB
#		include "zlib/zlib.h"
#	else
#		include <zlib.h>
#	endif
#endif

namespace Death { namespace IO { namespace Compression {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Provides read-only streaming of compressed data using the Deflate algorithm
	*/
	class DeflateStream : public Stream
	{
	public:
		DeflateStream();
		DeflateStream(Stream& inputStream, std::int32_t inputSize = -1, bool rawInflate = true);
		~DeflateStream();

		DeflateStream(const DeflateStream&) = delete;
		DeflateStream(DeflateStream&& other) noexcept;
		DeflateStream& operator=(const DeflateStream&) = delete;
		DeflateStream& operator=(DeflateStream&& other) noexcept;

		void Open(Stream& inputStream, std::int32_t inputSize = -1, bool rawInflate = true);

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;
		std::int64_t GetSize() const override;
		std::int64_t SetSize(std::int64_t size) override;

		bool CeaseReading();

	private:
		enum class State : std::uint8_t {
			Unknown,
			Created,
			Initialized,
			Read,
			Finished,
			Failed
		};

#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_SWITCH)
		static constexpr std::int32_t BufferSize = 8192;
#else
		static constexpr std::int32_t BufferSize = 16384;
#endif

		Stream* _inputStream;
		z_stream _strm;
		std::int64_t _size;
		std::int32_t _inputSize;
		State _state;
		bool _rawInflate;
		unsigned char _buffer[BufferSize];

		void InitializeInternal();
		std::int32_t ReadInternal(void* ptr, std::int32_t size);
		bool FillInputBuffer();
	};

	/**
		@brief Provides write-only streaming to compress written data by using the Deflate algorithm
	*/
	class DeflateWriter : public Stream
	{
	public:
		DeflateWriter(Stream& outputStream, std::int32_t compressionLevel = 9, bool rawInflate = true);
		~DeflateWriter();

		DeflateWriter(const DeflateWriter&) = delete;
		DeflateWriter& operator=(const DeflateWriter&) = delete;

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;
		std::int64_t GetSize() const override;
		std::int64_t SetSize(std::int64_t size) override;

		static std::int64_t GetMaxDeflatedSize(std::int64_t uncompressedSize);

	private:
		enum class State : std::uint8_t {
			Created,
			Initialized,
			Finished,
			Failed
		};

#if defined(DEATH_TARGET_EMSCRIPTEN)
		static constexpr std::int32_t BufferSize = 8192;
#else
		static constexpr std::int32_t BufferSize = 16384;
#endif

		Stream* _outputStream;
		z_stream _strm;
		State _state;
		unsigned char _buffer[BufferSize];

		std::int32_t WriteInternal(const void* buffer, std::int32_t bytesToWrite, bool finish);
	};

}}}

#endif