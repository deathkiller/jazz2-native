#pragma once

#include "../../Common.h"
#include "../Stream.h"

#if defined(WITH_LZMA2) || defined(DOXYGEN_GENERATING_OUTPUT)

#if !defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("lzma/lzma.h")
#		define __HAS_LOCAL_LZMA
#	endif
#endif
#ifdef __HAS_LOCAL_LZMA
#	include "lzma/lzma.h"
#else
#	include <lzma.h>
#endif

namespace Death { namespace IO { namespace Compression {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Provides read-only streaming of compressed data using the LZMA2 compression algorithm
	*/
	class Lzma2Stream : public Stream
	{
	public:
		Lzma2Stream();
		Lzma2Stream(Stream& inputStream, std::int32_t inputSize = -1);
		~Lzma2Stream();

		Lzma2Stream(const Lzma2Stream&) = delete;
		Lzma2Stream(Lzma2Stream&& other) noexcept;
		Lzma2Stream& operator=(const Lzma2Stream&) = delete;
		Lzma2Stream& operator=(Lzma2Stream&& other) noexcept;

		void Open(Stream& inputStream, std::int32_t inputSize = -1);

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
		lzma_stream _strm;
		std::int64_t _size;
		std::int32_t _inputSize;
		State _state;
		std::uint8_t _buffer[BufferSize];

		void InitializeInternal();
		std::int32_t ReadInternal(void* ptr, std::int32_t size);
		bool FillInputBuffer();
	};

	/**
		@brief Provides write-only streaming to compress written data by using the LZMA2 compression algorithm
	*/
	class Lzma2Writer : public Stream
	{
	public:
		Lzma2Writer(Stream& outputStream, std::int32_t compressionLevel = 6);
		~Lzma2Writer();

		Lzma2Writer(const Lzma2Writer&) = delete;
		Lzma2Writer& operator=(const Lzma2Writer&) = delete;

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;
		std::int64_t GetSize() const override;
		std::int64_t SetSize(std::int64_t size) override;

	private:
		enum class State : std::uint8_t {
			Created,
			Initialized,
			Finished,
			Failed
		};

#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_SWITCH)
		static constexpr std::int32_t BufferSize = 8192;
#else
		static constexpr std::int32_t BufferSize = 16384;
#endif

		Stream* _outputStream;
		lzma_stream _strm;
		State _state;
		std::uint8_t _buffer[BufferSize];

		std::int32_t WriteInternal(const void* buffer, std::int32_t bytesToWrite, bool finish);
	};

}}}

#endif
