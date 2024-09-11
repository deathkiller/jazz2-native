#include "DeflateStream.h"
#include "../Asserts.h"

#if !defined(WITH_ZLIB)
#	pragma message("Death::IO::DeflateStream requires `zlib` library")
#else

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
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

namespace Death { namespace IO {
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
		if (other._state == State::Created || other._state == State::Initialized) {
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
		if (other._state == State::Created || other._state == State::Initialized) {
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
					std::int32_t size = (offset > sizeof(buffer) ? sizeof(buffer) : static_cast<std::int32_t>(offset));
					std::int32_t bytesRead = Read(buffer, size);
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

	std::int32_t DeflateStream::Read(void* buffer, std::int32_t bytes)
	{
		std::uint8_t* typedBuffer = static_cast<std::uint8_t*>(buffer);
		std::int32_t bytesRead = ReadInternal(typedBuffer, bytes);
		std::int32_t bytesReadTotal = bytesRead;
		while (bytesRead > 0 && bytesRead < bytes) {
			typedBuffer += bytesRead;
			bytes -= bytesRead;
			bytesRead = ReadInternal(typedBuffer, bytes);
			if (bytesRead > 0) {
				bytesReadTotal += bytesRead;
			}
		}
		return bytesReadTotal;
	}

	std::int32_t DeflateStream::Write(const void* buffer, std::int32_t bytes)
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
		return (_state == State::Initialized || _state == State::Finished);
	}

	std::int64_t DeflateStream::GetSize() const
	{
		return _size;
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
			LOGE("Failed to initialize compressed stream with error %i", error);
			return;
		}

		_state = State::Initialized;
	}

	std::int32_t DeflateStream::ReadInternal(void* ptr, std::int32_t size)
	{
		if (size <= 0) {
			return 0;
		}

		if (_state == State::Created) {
			InitializeInternal();
		}
		if (_state == State::Unknown || _state >= State::Finished) {
			return Stream::Invalid;
		}

		if (_strm.avail_in == 0) {
			std::int32_t bytesRead = _inputSize;
			if (bytesRead < 0 || bytesRead > sizeof(_buffer)) {
				bytesRead = sizeof(_buffer);
			}

			bytesRead = _inputStream->Read(_buffer, bytesRead);
			if (bytesRead <= 0) {
				return 0;
			}

			_inputSize -= bytesRead;

			_strm.next_in = _buffer;
			_strm.avail_in = static_cast<std::uint32_t>(bytesRead);
		}

		_strm.next_out = static_cast<unsigned char*>(ptr);
		_strm.avail_out = size;

		std::int32_t res = inflate(&_strm, Z_SYNC_FLUSH);
		if (res != Z_OK && res != Z_STREAM_END) {
			CeaseReading();
			_state = State::Failed;
			LOGE("Failed to inflate compressed stream with error %i", res);
			return -1;
		}
		size -= _strm.avail_out;

		if (res == Z_STREAM_END && !CeaseReading()) {
			return -1;
		}

		return size;
	}

	bool DeflateStream::CeaseReading()
	{
		if (_state != State::Initialized) {
			return true;
		}

		_inputStream->Seek(_inputSize >= 0 ? _inputSize : -static_cast<std::int32_t>(_strm.avail_in), SeekOrigin::Current);

		std::int32_t error = inflateEnd((z_stream*)&_strm);
		if (error != Z_OK) {
			LOGE("Failed to finalize compressed stream with error %i", error);
		}

		_state = State::Finished;
		_size = static_cast<std::int32_t>(_strm.total_out);
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
			LOGE("Failed to initialize compressed stream with error %i", error);
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
			LOGE("Failed to finalize compressed stream with error %i", error);
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

	std::int32_t DeflateWriter::Read(void* buffer, std::int32_t bytes)
	{
		// Not supported
		return Stream::Invalid;
	}

	std::int32_t DeflateWriter::Write(const void* buffer, std::int32_t bytes)
	{
		if (bytes <= 0) {
			return 0;
		}
		if (_state != State::Created && _state != State::Initialized) {
			return Stream::Invalid;
		}

		_state = State::Initialized;
		return WriteInternal(buffer, bytes, false);
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

	std::int32_t DeflateWriter::WriteInternal(const void* buffer, std::int32_t bytes, bool finish)
	{
		_strm.next_in = (unsigned char*)buffer;
		_strm.avail_in = bytes;

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
				LOGE("Failed to deflate uncompressed buffer with error %i", error);
				return Stream::Invalid;
			}
		}

		return bytes - (std::int32_t)_strm.avail_in;
	}

	std::int64_t DeflateWriter::GetMaxDeflatedSize(std::int64_t uncompressedSize)
	{
		constexpr std::int64_t MinBlockSize = 5000;
		std::int64_t maxBlocks = std::max((uncompressedSize + MinBlockSize - 1) / MinBlockSize, std::int64_t(1));
		return uncompressedSize + (5 * maxBlocks) + 1 + 8;
	}

}}

#endif