#include "ZstdStream.h"
#include "../../Asserts.h"

#if !defined(WITH_ZSTD)
#	pragma message("Death::IO::ZstdStream requires `zstd` library")
#else

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#	if defined(_M_X64)
#		pragma comment(lib, "../Libs/Windows/x64/zstd.lib")
#	elif defined(_M_IX86)
#		pragma comment(lib, "../Libs/Windows/x86/zstd.lib")
#	else
#		error Unsupported architecture
#	endif
#endif

#include <algorithm>
#include <cstring>

#ifdef __HAS_LOCAL_ZSTD
#	include "zstd/zstd_errors.h"
#else
#	include <zstd_errors.h>
#endif

namespace Death { namespace IO { namespace Compression {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	ZstdStream::ZstdStream()
		: _inputStream(nullptr), _strm(nullptr), _size(Stream::Invalid), _inputSize(-1), _state(State::Unknown)
	{
	}

	ZstdStream::ZstdStream(Stream& inputStream, std::int32_t inputSize)
		: ZstdStream()
	{
		Open(inputStream, inputSize);
	}

	ZstdStream::~ZstdStream()
	{
		ZstdStream::Dispose();
	}

	ZstdStream::ZstdStream(ZstdStream&& other) noexcept
	{
		_inputStream = other._inputStream;
		_strm = other._strm;
		_inputSize = other._inputSize;
		_state = other._state;

		_buffer = Death::move(other._buffer);
		_inBufferPos = other._inBufferPos;
		_inBufferLength = other._inBufferLength;
		_inBufferCapacity = other._inBufferCapacity;
		_outBufferPos = other._outBufferPos;
		_outPosTotal = other._outPosTotal;
		_outBufferLength = other._outBufferLength;
		_outBufferCapacity = other._outBufferCapacity;

		// Original instance will be disabled
		if (other._state == State::Created || other._state == State::Initialized) {
			other._state = State::Failed;
		}
	}

	ZstdStream& ZstdStream::operator=(ZstdStream&& other) noexcept
	{
		Dispose();

		_inputStream = other._inputStream;
		_strm = other._strm;
		_inputSize = other._inputSize;
		_state = other._state;
		
		_buffer = Death::move(other._buffer);
		_inBufferPos = other._inBufferPos;
		_inBufferLength = other._inBufferLength;
		_inBufferCapacity = other._inBufferCapacity;
		_outBufferPos = other._outBufferPos;
		_outPosTotal = other._outPosTotal;
		_outBufferLength = other._outBufferLength;
		_outBufferCapacity = other._outBufferCapacity;

		// Original instance will be disabled
		if (other._state == State::Created || other._state == State::Initialized) {
			other._state = State::Failed;
		}

		return *this;
	}

	std::int64_t ZstdStream::Seek(std::int64_t offset, SeekOrigin origin)
	{
		switch (origin) {
			case SeekOrigin::Current: {
				DEATH_ASSERT(offset >= 0, "Cannot seek to negative values", Stream::OutOfRange);

				char buffer[4096];
				while (offset > 0) {
					std::int64_t size = (offset > sizeof(buffer) ? sizeof(buffer) : offset);
					std::int64_t bytesRead = Read(buffer, size);
					if (bytesRead <= 0) {
						return bytesRead;
					}
					offset -= bytesRead;
				}

				return static_cast<std::int64_t>(_outPosTotal);
			}
		}

		return Stream::OutOfRange;
	}

	std::int64_t ZstdStream::GetPosition() const
	{
		return static_cast<std::int64_t>(_outPosTotal);
	}

	std::int64_t ZstdStream::Read(void* destination, std::int64_t bytesToRead)
	{
		if (bytesToRead <= 0) {
			return 0;
		}

		DEATH_ASSERT(destination != nullptr, "destination is null", 0);
		std::uint8_t* typedBuffer = static_cast<std::uint8_t*>(destination);
		std::int64_t bytesReadTotal = 0;

		do {
			// ReadInternal() can read only up to ZSTD_DStreamInSize() bytes
			std::int32_t partialBytesToRead = (bytesToRead < INT32_MAX ? (std::int32_t)bytesToRead : INT32_MAX);
			std::int32_t bytesRead = ReadInternal(&typedBuffer[bytesReadTotal], partialBytesToRead);
			if DEATH_UNLIKELY(bytesRead < 0) {
				return bytesRead;
			} else if DEATH_UNLIKELY(bytesRead == 0) {
				break;
			}
			bytesReadTotal += bytesRead;
			bytesToRead -= bytesRead;
		} while (bytesToRead > 0);

		_outPosTotal += bytesReadTotal;
		return bytesReadTotal;
	}

	std::int64_t ZstdStream::Write(const void* source, std::int64_t bytesToWrite)
	{
		// Not supported
		return Stream::Invalid;
	}

	bool ZstdStream::Flush()
	{
		// Not supported
		return true;
	}

	bool ZstdStream::IsValid()
	{
		if (_state == State::Created) {
			InitializeInternal();
		}
		return (_state == State::Initialized || _state == State::Finished);
	}

	std::int64_t ZstdStream::GetSize() const
	{
		return _size;
	}

	void ZstdStream::Open(Stream& inputStream, std::int32_t inputSize)
	{
		Dispose();

		_inputStream = &inputStream;
		_inputSize = inputSize;
		_state = State::Created;
		_size = Stream::NotSeekable;

		_strm = ZSTD_createDStream();
		DEATH_DEBUG_ASSERT(_strm != nullptr);

		_inBufferPos = 0;
		_inBufferLength = 0;
		_inBufferCapacity = static_cast<std::int32_t>(ZSTD_DStreamInSize());	// Recommended input buffer size
		_outBufferPos = 0;
		_outPosTotal = 0;
		_outBufferLength = 0;
		_outBufferCapacity = static_cast<std::int32_t>(ZSTD_DStreamOutSize());	// Recommended output buffer size
	}

	void ZstdStream::Dispose()
	{
		CeaseReading();

		ZSTD_freeDStream(_strm);
		_strm = nullptr;

		_inputStream = nullptr;
		_state = State::Unknown;
		_size = Stream::Invalid;
	}

	void ZstdStream::InitializeInternal()
	{
		std::size_t result = ZSTD_initDStream(_strm);
		if (ZSTD_isError(result)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("ZSTD_initDStream() failed with error 0x%zx (%s)", result, ZSTD_getErrorName(result));
#endif
			_state = State::Failed;
		}

		if (_buffer == nullptr) {
			_buffer = std::make_unique<char[]>(_inBufferCapacity + _outBufferCapacity);
		}

		_state = State::Initialized;
	}

	std::int32_t ZstdStream::ReadInternal(void* ptr, std::int32_t size)
	{
		if (_state == State::Created) {
			InitializeInternal();
		}
		if (size <= 0 || _state == State::Finished) {
			return 0;
		}
		if (_state == State::Unknown || _state >= State::Failed) {
			return Stream::Invalid;
		}

		std::int32_t n = (_outBufferLength - _outBufferPos);
		while (n == 0) {
			if (_inBufferPos >= _inBufferLength) {
				std::int32_t bytesRead = _inputSize;
				if (bytesRead < 0 || bytesRead > _inBufferCapacity) {
					bytesRead = _inBufferCapacity;
				}

				bytesRead = (std::int32_t)_inputStream->Read(&_buffer[0], bytesRead);
				if (bytesRead <= 0) {
					return Stream::Invalid;
				}

				if (_inputSize > 0) {
					_inputSize -= bytesRead;
				}

				_inBufferPos = 0;
			}

			ZSTD_inBuffer inBuffer = { &_buffer[0], _inBufferCapacity, _inBufferLength };
			ZSTD_outBuffer outBuffer = { &_buffer[_inBufferCapacity], _outBufferCapacity, _outBufferLength };
			std::size_t result = ZSTD_decompressStream(_strm, &outBuffer, &inBuffer);
			if (ZSTD_isError(result)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("ZSTD_decompressStream() failed with error 0x%zx (%s)", result, ZSTD_getErrorName(result));
#endif
				_state = State::Failed;
				return Stream::Invalid;
			}

			_inBufferLength = inBuffer.pos;
			_outBufferLength = outBuffer.pos;

			n = (_outBufferLength - _outBufferPos);
		}

		if (n > size) {
			n = size;
		}
		std::memcpy(ptr, &_buffer[_inBufferCapacity + _outBufferPos], n);
		_outBufferPos += n;
		return n;
	}

	bool ZstdStream::CeaseReading()
	{
		if (_state != State::Initialized) {
			return true;
		}

		std::int64_t seekToEnd = _inBufferLength - _inBufferPos;
		if (seekToEnd != 0) {
			_inputStream->Seek(seekToEnd, SeekOrigin::Current);
		}

		_state = State::Finished;
		_size = static_cast<std::int64_t>(_outPosTotal);
		return true;
	}

	ZstdWriter::ZstdWriter(Stream& outputStream, std::int32_t compressionLevel)
		: _outputStream(&outputStream), _state(State::Created), _outBufferPos(0), _outBufferLength(0)
	{
		_strm = ZSTD_createCStream();
		DEATH_DEBUG_ASSERT(_strm != nullptr);

		std::size_t result = ZSTD_initCStream(_strm, compressionLevel);
		if (ZSTD_isError(result)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("ZSTD_initCStream() failed with error 0x%zx (%s)", result, ZSTD_getErrorName(result));
#endif
			_state = State::Failed;
		}

		_inBufferCapacity = static_cast<std::int32_t>(ZSTD_CStreamInSize());	// Recommended input buffer size
		_outBufferCapacity = static_cast<std::int32_t>(ZSTD_CStreamOutSize());	// Recommended output buffer size

		_buffer = std::make_unique<char[]>(_outBufferCapacity);
	}

	ZstdWriter::~ZstdWriter()
	{
		ZstdWriter::Dispose();
	}

	void ZstdWriter::Dispose()
	{
		if (_state != State::Created && _state != State::Initialized) {
			return;
		}

		if (_state == State::Initialized) {
			WriteInternal(nullptr, 0, true);
		}

		ZSTD_freeCStream(_strm);
		_strm = nullptr;

		_state = State::Finished;
	}

	std::int64_t ZstdWriter::Seek(std::int64_t offset, SeekOrigin origin)
	{
		return Stream::NotSeekable;
	}

	std::int64_t ZstdWriter::GetPosition() const
	{
		return Stream::NotSeekable;
	}

	std::int64_t ZstdWriter::Read(void* destination, std::int64_t bytesToRead)
	{
		// Not supported
		return Stream::Invalid;
	}

	std::int64_t ZstdWriter::Write(const void* source, std::int64_t bytesToWrite)
	{
		if (bytesToWrite <= 0) {
			return 0;
		}
		if (_state != State::Created && _state != State::Initialized) {
			return Stream::Invalid;
		}

		DEATH_ASSERT(source != nullptr, "source is null", 0);
		const std::uint8_t* typedBuffer = static_cast<const std::uint8_t*>(source);
		std::int64_t bytesWrittenTotal = 0;
		_state = State::Initialized;

		do {
			std::int32_t partialBytesToWrite = (bytesToWrite < INT32_MAX ? (std::int32_t)bytesToWrite : INT32_MAX);
			std::int32_t bytesWritten = WriteInternal(&typedBuffer[bytesWrittenTotal], partialBytesToWrite, false);
			if DEATH_UNLIKELY(bytesWritten < 0) {
				return bytesWritten;
			} else if DEATH_UNLIKELY(bytesWritten == 0) {
				break;
			}
			bytesWrittenTotal += bytesWritten;
			bytesToWrite -= bytesWritten;
		} while (bytesToWrite > 0);

		return bytesWrittenTotal;
	}

	bool ZstdWriter::Flush()
	{
		return (WriteInternal(nullptr, 0, false) >= 0);
	}

	bool ZstdWriter::IsValid()
	{
		return(_state != State::Failed && _outputStream->IsValid());
	}

	std::int64_t ZstdWriter::GetSize() const
	{
		return Stream::NotSeekable;
	}

	std::int32_t ZstdWriter::WriteInternal(const void* buffer, std::int32_t bytesToWrite, bool finish)
	{
		if (bytesToWrite > 0) {
			if (bytesToWrite > _inBufferCapacity) {
				bytesToWrite = _inBufferCapacity;
			}

			ZSTD_inBuffer inBuffer = { buffer, bytesToWrite, 0 };
			ZSTD_outBuffer outBuffer = { &_buffer[0], _outBufferCapacity, 0 };
			std::size_t result = ZSTD_compressStream(_strm, &outBuffer, &inBuffer);
			if (ZSTD_isError(result)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("ZSTD_compressStream() failed with error %zx (%s)", result, ZSTD_getErrorName(result));
#endif
				_state = State::Failed;
				return Stream::Invalid;
			}

			if (outBuffer.pos > 0) {
				_outputStream->Write(&_buffer[0], outBuffer.pos);
			}

			bytesToWrite = inBuffer.pos;
		}

		if (finish) {
			ZSTD_outBuffer outBuffer = { &_buffer[0], _outBufferCapacity, 0 };
			std::size_t result = ZSTD_endStream(_strm, &outBuffer);
			if (ZSTD_isError(result)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("ZSTD_endStream() failed with error %zx (%s)", result, ZSTD_getErrorName(result));
#endif
				_state = State::Failed;
				return Stream::Invalid;
			}

			_outputStream->Write(&_buffer[0], outBuffer.pos);
		}

		return bytesToWrite;
	}

}}}

#endif