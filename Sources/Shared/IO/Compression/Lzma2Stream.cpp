#include "Lzma2Stream.h"

#if !defined(WITH_LZMA2)
#	pragma message("Death::IO::Lzma2Stream requires `liblzma` library")
#else

#include "../../Asserts.h"

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#	if defined(_M_X64)
#		pragma comment(lib, "../Libs/Windows/x64/lzma.lib")
#	elif defined(_M_IX86)
#		pragma comment(lib, "../Libs/Windows/x86/lzma.lib")
#	else
#		error Unsupported architecture
#	endif
#endif

#include <algorithm>
#include <cstring>

namespace Death { namespace IO { namespace Compression {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	Lzma2Stream::Lzma2Stream()
		: _inputStream(nullptr), _strm(LZMA_STREAM_INIT), _size(Stream::Invalid), _inputSize(-1), _state(State::Unknown)
	{
	}

	Lzma2Stream::Lzma2Stream(Stream& inputStream, std::int32_t inputSize)
		: Lzma2Stream()
	{
		Open(inputStream, inputSize);
	}

	Lzma2Stream::~Lzma2Stream()
	{
		Lzma2Stream::Dispose();
	}

	Lzma2Stream::Lzma2Stream(Lzma2Stream&& other) noexcept
	{
		_inputStream = other._inputStream;
		_strm = other._strm;
		_inputSize = other._inputSize;
		_state = other._state;
		std::memcpy(_buffer, other._buffer, sizeof(_buffer));

		// Original instance will be disabled
		if (other._state == State::Created || other._state == State::Initialized || other._state == State::Read) {
			other._state = State::Failed;
		}
	}

	Lzma2Stream& Lzma2Stream::operator=(Lzma2Stream&& other) noexcept
	{
		Dispose();

		_inputStream = other._inputStream;
		_strm = other._strm;
		_inputSize = other._inputSize;
		_state = other._state;
		std::memcpy(_buffer, other._buffer, sizeof(_buffer));

		// Original instance will be disabled
		if (other._state == State::Created || other._state == State::Initialized || other._state == State::Read) {
			other._state = State::Failed;
		}

		return *this;
	}

	std::int64_t Lzma2Stream::Seek(std::int64_t offset, SeekOrigin origin)
	{
		switch (origin) {
			case SeekOrigin::Current: {
				DEATH_ASSERT(offset >= 0, "Cannot seek to negative values", Stream::OutOfRange);

				std::uint8_t buffer[4096];
				while (offset > 0) {
					std::int64_t size = (offset > sizeof(buffer) ? sizeof(buffer) : offset);
					std::int64_t bytesRead = Read(buffer, size);
					if (bytesRead <= 0) {
						return bytesRead;
					}
					offset -= bytesRead;
				}

				return static_cast<std::int64_t>(_strm.total_out);
			}
		}

		return Stream::OutOfRange;
	}

	std::int64_t Lzma2Stream::GetPosition() const
	{
		return static_cast<std::int64_t>(_strm.total_out);
	}

	std::int64_t Lzma2Stream::Read(void* destination, std::int64_t bytesToRead)
	{
		if (bytesToRead <= 0) {
			return 0;
		}

		DEATH_ASSERT(destination != nullptr, "destination is null", 0);
		std::uint8_t* typedBuffer = (std::uint8_t*)destination;
		std::int64_t bytesReadTotal = 0;

		do {
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

		return bytesReadTotal;
	}

	std::int64_t Lzma2Stream::Write(const void* source, std::int64_t bytesToWrite)
	{
		// Not supported
		return Stream::Invalid;
	}

	bool Lzma2Stream::Flush()
	{
		// Not supported
		return true;
	}

	bool Lzma2Stream::IsValid()
	{
		if (_state == State::Created) {
			InitializeInternal();
		}
		return (_state == State::Initialized || _state == State::Read || _state == State::Finished);
	}

	std::int64_t Lzma2Stream::GetSize() const
	{
		return _size;
	}

	std::int64_t Lzma2Stream::SetSize(std::int64_t size)
	{
		return Stream::Invalid;
	}

	void Lzma2Stream::Open(Stream& inputStream, std::int32_t inputSize)
	{
		Dispose();

		_inputStream = &inputStream;
		_inputSize = inputSize;
		_state = State::Created;
		_size = Stream::NotSeekable;

		_strm = LZMA_STREAM_INIT;
	}

	void Lzma2Stream::Dispose()
	{
		CeaseReading();
		_inputStream = nullptr;
		_state = State::Unknown;
		_size = Stream::Invalid;
	}

	void Lzma2Stream::InitializeInternal()
	{
		// Use lzma_alone_decoder() for raw LZMA streams, or lzma_auto_decoder() for .xz/.lzma
		// Using raw LZMA2 decoder with filter chain for PakFile usage
		lzma_ret error = lzma_alone_decoder(&_strm, UINT64_MAX);
		if (error != LZMA_OK) {
			_state = State::Failed;
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to initialize LZMA2 stream with error {}", (std::int32_t)error);
#endif
			return;
		}

		_state = State::Initialized;
	}

	std::int32_t Lzma2Stream::ReadInternal(void* ptr, std::int32_t size)
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
		if (_state == State::Initialized && !FillInputBuffer()) {
			return 0;
		}

		_strm.next_out = (std::uint8_t*)ptr;
		_strm.avail_out = size;

		lzma_ret res = lzma_code(&_strm, LZMA_RUN);

		// If input buffer is empty, fill it and try again
		if (res == LZMA_OK && _strm.avail_out == (std::size_t)size && _strm.avail_in == 0 && FillInputBuffer()) {
			res = lzma_code(&_strm, LZMA_RUN);
		}

		if (res != LZMA_OK && res != LZMA_STREAM_END) {
			CeaseReading();
			_state = State::Failed;
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to decompress LZMA2 stream with error {}", (std::int32_t)res);
#endif
			return Stream::Invalid;
		}
		size -= (std::int32_t)_strm.avail_out;

		if (res == LZMA_STREAM_END && !CeaseReading()) {
			return Stream::Invalid;
		}

		return size;
	}

	bool Lzma2Stream::FillInputBuffer()
	{
		std::int32_t bytesRead = _inputSize;
		if (bytesRead < 0 || bytesRead > sizeof(_buffer)) {
			bytesRead = sizeof(_buffer);
		}

		bytesRead = std::int32_t(_inputStream->Read(_buffer, bytesRead));
		if (bytesRead <= 0) {
			return false;
		}

		if (_inputSize > 0) {
			_inputSize -= bytesRead;
		}

		_strm.next_in = _buffer;
		_strm.avail_in = std::uint32_t(bytesRead);
		_state = State::Read;
		return true;
	}

	bool Lzma2Stream::CeaseReading()
	{
		if (_state != State::Initialized && _state != State::Read) {
			return true;
		}

		std::int64_t seekToEnd = (_inputSize >= 0 ? _inputSize : -(std::int64_t)(_strm.avail_in));
		if (seekToEnd != 0) {
			_inputStream->Seek(seekToEnd, SeekOrigin::Current);
		}

		lzma_end(&_strm);

		_state = State::Finished;
		_size = std::int64_t(_strm.total_out);
		return true;
	}

	Lzma2Writer::Lzma2Writer(Stream& outputStream, std::int32_t compressionLevel)
		: _outputStream(&outputStream), _strm(LZMA_STREAM_INIT), _state(State::Created)
	{
		// Clamp compression level to valid LZMA range (0-9)
		if (compressionLevel < 0) compressionLevel = 0;
		if (compressionLevel > 9) compressionLevel = 9;

		lzma_options_lzma opt;
		if (lzma_lzma_preset(&opt, compressionLevel)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to set LZMA2 preset {}", compressionLevel);
#endif
			_state = State::Failed;
			return;
		}

		lzma_ret error = lzma_alone_encoder(&_strm, &opt);
		if (error != LZMA_OK) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to initialize LZMA2 encoder with error {}", (std::int32_t)error);
#endif
			_state = State::Failed;
		}
	}

	Lzma2Writer::~Lzma2Writer()
	{
		Lzma2Writer::Dispose();
	}

	void Lzma2Writer::Dispose()
	{
		if (_state != State::Created && _state != State::Initialized) {
			return;
		}

		if (_state == State::Initialized) {
			WriteInternal(nullptr, 0, true);
		}

		lzma_end(&_strm);

		_state = State::Finished;
	}

	std::int64_t Lzma2Writer::Seek(std::int64_t offset, SeekOrigin origin)
	{
		return Stream::NotSeekable;
	}

	std::int64_t Lzma2Writer::GetPosition() const
	{
		return Stream::NotSeekable;
	}

	std::int64_t Lzma2Writer::Read(void* destination, std::int64_t bytesToRead)
	{
		// Not supported
		return Stream::Invalid;
	}

	std::int64_t Lzma2Writer::Write(const void* source, std::int64_t bytesToWrite)
	{
		if (bytesToWrite <= 0) {
			return 0;
		}
		if (_state != State::Created && _state != State::Initialized) {
			return Stream::Invalid;
		}

		DEATH_ASSERT(source != nullptr, "source is null", 0);
		const std::uint8_t* typedBuffer = (const std::uint8_t*)source;
		std::int64_t bytesWrittenTotal = 0;
		_state = State::Initialized;

		do {
			std::int32_t partialBytesToWrite = (bytesToWrite < INT32_MAX ? std::int32_t(bytesToWrite) : INT32_MAX);
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

	bool Lzma2Writer::Flush()
	{
		return (WriteInternal(nullptr, 0, false) >= 0);
	}

	bool Lzma2Writer::IsValid()
	{
		return (_state != State::Failed && _outputStream->IsValid());
	}

	std::int64_t Lzma2Writer::GetSize() const
	{
		return Stream::NotSeekable;
	}

	std::int64_t Lzma2Writer::SetSize(std::int64_t size)
	{
		return Stream::Invalid;
	}

	std::int32_t Lzma2Writer::WriteInternal(const void* buffer, std::int32_t bytesToWrite, bool finish)
	{
		_strm.next_in = (const std::uint8_t*)buffer;
		_strm.avail_in = bytesToWrite;

		while (_strm.avail_in > 0 || finish) {
			lzma_ret error;
			do {
				_strm.next_out = _buffer;
				_strm.avail_out = sizeof(_buffer);
				error = lzma_code(&_strm, finish ? LZMA_FINISH : LZMA_RUN);

				std::int32_t bytesWritten = sizeof(_buffer) - std::int32_t(_strm.avail_out);
				if (bytesWritten > 0) {
					_outputStream->Write(_buffer, bytesWritten);
				}
			} while (error == LZMA_OK && _strm.avail_out == 0);

			if (error == LZMA_STREAM_END) {
				break;
			}
			if (error != LZMA_OK) {
#if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Failed to compress LZMA2 buffer with error {}", (std::int32_t)error);
#endif
				return Stream::Invalid;
			}
		}

		return bytesToWrite - (std::int32_t)_strm.avail_in;
	}

}}}

#endif
