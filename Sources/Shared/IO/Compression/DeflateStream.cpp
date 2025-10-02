#include "DeflateStream.h"
#include "../../Asserts.h"

#if !defined(WITH_ZLIB) && !defined(WITH_MINIZ)
#	pragma message("Death::IO::DeflateStream requires `zlib` or `miniz` library")
#else

#if defined(WITH_ZLIB) && defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#	if defined(_M_X64)
#		pragma comment(lib, "../Libs/Windows/x64/zlib.lib")
#	elif defined(_M_IX86)
#		pragma comment(lib, "../Libs/Windows/x86/zlib.lib")
#	else
#		error Unsupported architecture
#	endif
#endif

#include <algorithm>
#include <cstring>

namespace Death { namespace IO { namespace Compression {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	DeflateStream::DeflateStream()
		: _inputStream(nullptr), _size(Stream::Invalid), _inputSize(-1), _state(State::Unknown)
	{
	}

	DeflateStream::DeflateStream(Stream& inputStream, std::int32_t inputSize, bool rawInflate)
		: DeflateStream()
	{
		Open(inputStream, inputSize, rawInflate);
	}

	DeflateStream::~DeflateStream()
	{
		DeflateStream::Dispose();
	}

	DeflateStream::DeflateStream(DeflateStream&& other) noexcept
	{
		_inputStream = other._inputStream;
		_strm = other._strm;
		_inputSize = other._inputSize;
		_state = other._state;
		_rawInflate = other._rawInflate;
		std::memcpy(_buffer, other._buffer, sizeof(_buffer));

		// Original instance will be disabled
		if (other._state == State::Created || other._state == State::Initialized || other._state == State::Read) {
			other._state = State::Failed;
		}
	}

	DeflateStream& DeflateStream::operator=(DeflateStream&& other) noexcept
	{
		Dispose();

		_inputStream = other._inputStream;
		_strm = other._strm;
		_inputSize = other._inputSize;
		_state = other._state;
		_rawInflate = other._rawInflate;
		std::memcpy(_buffer, other._buffer, sizeof(_buffer));

		// Original instance will be disabled
		if (other._state == State::Created || other._state == State::Initialized || other._state == State::Read) {
			other._state = State::Failed;
		}

		return *this;
	}

	std::int64_t DeflateStream::Seek(std::int64_t offset, SeekOrigin origin)
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

				return static_cast<std::int64_t>(_strm.total_out);
			}
		}

		return Stream::OutOfRange;
	}

	std::int64_t DeflateStream::GetPosition() const
	{
		return static_cast<std::int64_t>(_strm.total_out);
	}

	std::int64_t DeflateStream::Read(void* destination, std::int64_t bytesToRead)
	{
		if (bytesToRead <= 0) {
			return 0;
		}

		DEATH_ASSERT(destination != nullptr, "destination is null", 0);
		std::uint8_t* typedBuffer = static_cast<std::uint8_t*>(destination);
		std::int64_t bytesReadTotal = 0;

		do {
			// ReadInternal() can read only up to DeflateStream::BufferSize bytes
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

	std::int64_t DeflateStream::Write(const void* source, std::int64_t bytesToWrite)
	{
		// Not supported
		return Stream::Invalid;
	}

	bool DeflateStream::Flush()
	{
		// Not supported
		return true;
	}

	bool DeflateStream::IsValid()
	{
		if (_state == State::Created) {
			InitializeInternal();
		}
		return (_state == State::Initialized || _state == State::Read || _state == State::Finished);
	}

	std::int64_t DeflateStream::GetSize() const
	{
		return _size;
	}

	std::int64_t DeflateStream::SetSize(std::int64_t size)
	{
		return Stream::Invalid;
	}

	void DeflateStream::Open(Stream& inputStream, std::int32_t inputSize, bool rawInflate)
	{
		Dispose();

		_inputStream = &inputStream;
		_inputSize = inputSize;
		_state = State::Created;
		_rawInflate = rawInflate;
		_size = Stream::NotSeekable;

		_strm.zalloc = Z_NULL;
		_strm.zfree = Z_NULL;
		_strm.opaque = Z_NULL;
		_strm.next_in = Z_NULL;
		_strm.avail_in = 0;
		_strm.total_out = 0;
	}

	void DeflateStream::Dispose()
	{
		CeaseReading();
		_inputStream = nullptr;
		_state = State::Unknown;
		_size = Stream::Invalid;
	}

	void DeflateStream::InitializeInternal()
	{
		std::int32_t error = (_rawInflate ? inflateInit2(&_strm, -MAX_WBITS) : inflateInit(&_strm));
		if (error != Z_OK) {
			_state = State::Failed;
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to initialize compressed stream with error {}", error);
#endif
			return;
		}

		_state = State::Initialized;
	}

	std::int32_t DeflateStream::ReadInternal(void* ptr, std::int32_t size)
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

		_strm.next_out = static_cast<unsigned char*>(ptr);
		_strm.avail_out = size;

		std::int32_t res = inflate(&_strm, Z_NO_FLUSH);

		// If input buffer is empty, fill it and try it again
		if (res == Z_BUF_ERROR && _strm.avail_in == 0 && FillInputBuffer()) {
			res = inflate(&_strm, Z_NO_FLUSH);
		}

		if (res != Z_OK && res != Z_STREAM_END) {
			CeaseReading();
			_state = State::Failed;
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to inflate compressed stream with error {}", res);
#endif
			return Stream::Invalid;
		}
		size -= _strm.avail_out;

		if (res == Z_STREAM_END && !CeaseReading()) {
			return Stream::Invalid;
		}

		return size;
	}

	bool DeflateStream::FillInputBuffer()
	{
		std::int32_t bytesRead = _inputSize;
		if (bytesRead < 0 || bytesRead > sizeof(_buffer)) {
			bytesRead = sizeof(_buffer);
		}

		bytesRead = (std::int32_t)_inputStream->Read(_buffer, bytesRead);
		if (bytesRead <= 0) {
			return false;
		}

		if (_inputSize > 0) {
			_inputSize -= bytesRead;
		}

		_strm.next_in = _buffer;
		_strm.avail_in = static_cast<std::uint32_t>(bytesRead);
		_state = State::Read;
		return true;
	}

	bool DeflateStream::CeaseReading()
	{
		if (_state != State::Initialized && _state != State::Read) {
			return true;
		}

		std::int64_t seekToEnd = (_inputSize >= 0 ? _inputSize : -static_cast<std::int64_t>(_strm.avail_in));
		if (seekToEnd != 0) {
			_inputStream->Seek(seekToEnd, SeekOrigin::Current);
		}

		std::int32_t error = inflateEnd((z_stream*)&_strm);
		if (error != Z_OK) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to finalize compressed stream with error {}", error);
#endif
		}

		_state = State::Finished;
		_size = static_cast<std::int64_t>(_strm.total_out);
		return (error == Z_OK);
	}

	DeflateWriter::DeflateWriter(Stream& outputStream, std::int32_t compressionLevel, bool rawInflate)
		: _outputStream(&outputStream), _state(State::Created)
	{
		_strm.zalloc = Z_NULL;
		_strm.zfree = Z_NULL;
		_strm.opaque = Z_NULL;

		std::int32_t error = (rawInflate
			? deflateInit2(&_strm, compressionLevel, Z_DEFLATED, -MAX_WBITS, MAX_MEM_LEVEL >= 8 ? 8 : MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY)
			: deflateInit(&_strm, compressionLevel));
		if (error != Z_OK) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to initialize compressed stream with error {}", error);
#endif
			_state = State::Failed;
		}
	}

	DeflateWriter::~DeflateWriter()
	{
		DeflateWriter::Dispose();
	}

	void DeflateWriter::Dispose()
	{
		if (_state != State::Created && _state != State::Initialized) {
			return;
		}

		if (_state == State::Initialized) {
			WriteInternal(nullptr, 0, true);
		}

		std::int32_t error = deflateEnd(&_strm);
		if (error != Z_OK) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to finalize compressed stream with error {}", error);
#endif
		}

		_state = State::Finished;
	}

	std::int64_t DeflateWriter::Seek(std::int64_t offset, SeekOrigin origin)
	{
		return Stream::NotSeekable;
	}

	std::int64_t DeflateWriter::GetPosition() const
	{
		return Stream::NotSeekable;
	}

	std::int64_t DeflateWriter::Read(void* destination, std::int64_t bytesToRead)
	{
		// Not supported
		return Stream::Invalid;
	}

	std::int64_t DeflateWriter::Write(const void* source, std::int64_t bytesToWrite)
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

	bool DeflateWriter::Flush()
	{
		return (WriteInternal(nullptr, 0, false) >= 0);
	}

	bool DeflateWriter::IsValid()
	{
		return(_state != State::Failed && _outputStream->IsValid());
	}

	std::int64_t DeflateWriter::GetSize() const
	{
		return Stream::NotSeekable;
	}

	std::int64_t DeflateWriter::SetSize(std::int64_t size)
	{
		return Stream::Invalid;
	}

	std::int32_t DeflateWriter::WriteInternal(const void* buffer, std::int32_t bytesToWrite, bool finish)
	{
		_strm.next_in = (unsigned char*)buffer;
		_strm.avail_in = bytesToWrite;

		while (_strm.avail_in > 0 || finish) {
			std::int32_t error;
			do {
				_strm.next_out = static_cast<unsigned char*>(_buffer);
				_strm.avail_out = sizeof(_buffer);
				error = deflate(&_strm, finish
					? Z_FINISH
					: (buffer != nullptr ? Z_NO_FLUSH : Z_FULL_FLUSH));

				std::int32_t bytesWritten = sizeof(_buffer) - static_cast<std::int32_t>(_strm.avail_out);
				if (bytesWritten > 0) {
					_outputStream->Write(_buffer, bytesWritten);
				}
			} while (error == Z_OK && _strm.avail_out == 0);

			if (error == Z_STREAM_END) {
				break;
			}
			if (error != Z_OK) {
#if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Failed to deflate uncompressed buffer with error {}", error);
#endif
				return Stream::Invalid;
			}
		}

		return bytesToWrite - (std::int32_t)_strm.avail_in;
	}

	std::int64_t DeflateWriter::GetMaxDeflatedSize(std::int64_t uncompressedSize)
	{
		constexpr std::int64_t MinBlockSize = 5000;
		std::int64_t maxBlocks = std::max((uncompressedSize + MinBlockSize - 1) / MinBlockSize, std::int64_t(1));
		return uncompressedSize + (5 * maxBlocks) + 1 + 8;
	}

}}}

#endif