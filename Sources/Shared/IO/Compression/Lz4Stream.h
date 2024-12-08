#pragma once

#include "../../Common.h"
#include "../Stream.h"

#if defined(WITH_LZ4)

#if !defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("lz4/lz4frame.h")
#		define __HAS_LOCAL_LZ4
#	endif
#endif
#ifdef __HAS_LOCAL_LZ4
#	include "lz4/lz4frame.h"
#else
#	include <lz4frame.h>
#endif

namespace Death { namespace IO { namespace Compression {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Read-only streaming of compressed data using the LZ4 compression algorithm
	*/
	class Lz4Stream : public Stream
	{
	public:
		Lz4Stream();
		Lz4Stream(Stream& inputStream, std::int32_t inputSize = -1);
		~Lz4Stream();

		Lz4Stream(const Lz4Stream&) = delete;
		Lz4Stream(Lz4Stream&& other) noexcept;
		Lz4Stream& operator=(const Lz4Stream&) = delete;
		Lz4Stream& operator=(Lz4Stream&& other) noexcept;

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

	protected:
		enum class State : std::uint8_t {
			Unknown,
			Created,
			Initialized,
			Finished,
			Failed
		};

		static constexpr std::int32_t ChunkSize = 16384;

		Stream* _inputStream;
		LZ4F_dctx* _ctx;
		std::int64_t _size;
		std::int32_t _inputSize;
		State _state;

		char _inBuffer[ChunkSize];
		std::int32_t _inPos;
		std::int32_t _inLength;

		std::unique_ptr<char[]> _outBuffer;
		std::int32_t _outCapacity;
		std::int32_t _outPos;
		std::int32_t _outPosTotal;
		std::int32_t _outLength;

		void InitializeInternal();
		std::int32_t ReadInternal(void* ptr, std::int32_t size);
	};

	/**
		@brief Write-only streaming to compress written data by using the LZ4 compression algorithm
	*/
	class Lz4Writer : public Stream
	{
	public:
		Lz4Writer(Stream& outputStream, std::int32_t compressionLevel = 0);
		~Lz4Writer();

		Lz4Writer(const Lz4Writer&) = delete;
		Lz4Writer& operator=(const Lz4Writer&) = delete;

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;
		std::int64_t GetSize() const override;

	protected:
		enum class State : std::uint8_t {
			Created,
			Initialized,
			Finished,
			Failed
		};

		static constexpr std::int32_t ChunkSize = 16384;

		Stream* _outputStream;
		LZ4F_cctx* _ctx;
		State _state;

		char _buffer[ChunkSize];

		std::unique_ptr<char[]> _outBuffer;
		std::int32_t _outCapacity;
		std::int32_t _outLength;

		std::int32_t WriteInternal(const void* buffer, std::int32_t bytesToWrite, bool finish);
	};

}}}

#endif