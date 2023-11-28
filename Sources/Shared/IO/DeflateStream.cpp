#include "DeflateStream.h"

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

namespace Death::IO
{
	std::int32_t DeflateStream::Seek(std::int32_t offset, SeekOrigin origin)
	{
		switch (origin) {
			case SeekOrigin::Current: {
				DEATH_ASSERT(offset >= 0, -1, "Cannot seek to negative values");

				char buf[4096];
				while (offset > 0) {
					std::int32_t size = (offset > sizeof(buf) ? sizeof(buf) : offset);
					Read(buf, size);
					offset -= size;
				}
				return static_cast<std::int32_t>(_strm.total_out);
			}
		}

		return Unseekable;
	}

	std::int32_t DeflateStream::GetPosition() const
	{
		return static_cast<std::int32_t>(_strm.total_out);
	}

	std::int32_t DeflateStream::Read(void* buffer, std::int32_t bytes)
	{
		std::uint8_t* typedBuffer = (std::uint8_t*)buffer;
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
		return -1;
	}

	bool DeflateStream::IsValid() const
	{
		return (_state == State::Initialized || _state == State::Finished);
	}

	void DeflateStream::Open(Stream& inputStream, std::int32_t inputSize, bool rawInflate)
	{
		Close();

		_inputStream = &inputStream;
		_inputSize = inputSize;
		_state = State::Created;
		_rawInflate = rawInflate;

		_strm.zalloc = Z_NULL;
		_strm.zfree = Z_NULL;
		_strm.opaque = Z_NULL;
		_strm.next_in = Z_NULL;
		_strm.avail_in = 0;
		_strm.total_out = 0;
	}

	void DeflateStream::Close()
	{
		CeaseReading();
		_inputStream = nullptr;
		_state = State::Unknown;
	}

	std::int32_t DeflateStream::ReadInternal(void* ptr, std::int32_t size)
	{
		if (size == 0 || _state == State::Unknown || _state >= State::Finished) {
			return 0;
		}

		if (_state == State::Created) {
			std::int32_t error = (_rawInflate ? inflateInit2(&_strm, -MAX_WBITS) : inflateInit(&_strm));
			if (error != Z_OK) {
				_state = State::Failed;
				LOGE("Failed to initialize compressed stream with error: %i", error);
				return -1;
			}
			_state = State::Initialized;
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

		_strm.next_out = (unsigned char*)ptr;
		_strm.avail_out = size;

		std::int32_t res = inflate(&_strm, Z_SYNC_FLUSH);
		if (res != Z_OK && res != Z_STREAM_END) {
			CeaseReading();
			_state = State::Failed;
			LOGE("Failed to inflate compressed stream with error: %i", res);
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

		_inputStream->Seek(_inputSize >= 0 ? _inputSize : -std::int32_t(_strm.avail_in), SeekOrigin::Current);

		std::int32_t error = inflateEnd((z_stream*)&_strm);
		if (error != Z_OK) {
			LOGE("Failed to finalize compressed stream with error: %i", error);
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
			LOGE("Failed to initialize compressed stream with error: %i", error);
			_state = State::Failed;
		}
	}

	DeflateWriter::~DeflateWriter()
	{
		Close();
	}

	void DeflateWriter::Close()
	{
		if (_state != State::Created && _state != State::Initialized) {
			return;
		}

		if (_state == State::Initialized) {
			WriteInternal(nullptr, 0, true);
		}

		std::int32_t error = deflateEnd(&_strm);
		if (error != Z_OK) {
			LOGE("Failed to finalize compressed stream with error: %i", error);
		}

		_state = State::Finished;
	}

	std::int32_t DeflateWriter::Seek(std::int32_t offset, SeekOrigin origin)
	{
		return Unseekable;
	}

	std::int32_t DeflateWriter::GetPosition() const
	{
		return Unseekable;
	}

	std::int32_t DeflateWriter::Read(void* buffer, std::int32_t bytes)
	{
		return 0;
	}

	std::int32_t DeflateWriter::Write(const void* buffer, std::int32_t bytes)
	{
		if (_state != State::Created && _state != State::Initialized) {
			return 0;
		}

		_state = State::Initialized;
		return WriteInternal(buffer, bytes, false);
	}

	bool DeflateWriter::IsValid() const
	{
		return(_state != State::Failed && _outputStream->IsValid());
	}

	std::int32_t DeflateWriter::WriteInternal(const void* buffer, std::int32_t bytes, bool finish)
	{
		_strm.next_in = (unsigned char*)buffer;
		_strm.avail_in = bytes;

		while (_strm.avail_in > 0 || finish) {
			std::int32_t error;
			do {
				_strm.next_out = (unsigned char*)_buffer;
				_strm.avail_out = sizeof(_buffer);
				error = deflate(&_strm, finish ? Z_FINISH : Z_NO_FLUSH);

				std::int32_t bytesWritten = sizeof(_buffer) - (std::int32_t)_strm.avail_out;
				if (bytesWritten > 0) {
					_outputStream->Write(_buffer, bytesWritten);
				}
			} while (error == Z_OK && _strm.avail_out == 0);

			if (error == Z_STREAM_END) {
				break;
			}
			if (error != Z_OK) {
				LOGE("Failed to deflate uncompressed buffer with error: %i", error);
				break;
			}
		}

		return bytes - (std::int32_t)_strm.avail_in;
	}

	std::int32_t DeflateWriter::GetMaxDeflatedSize(int32_t uncompressedSize)
	{
		constexpr std::int32_t MinBlockSize = 5000;
		std::int32_t max_blocks = std::max((uncompressedSize + MinBlockSize - 1) / MinBlockSize, 1);
		return uncompressedSize + (5 * max_blocks) + 1 + 8;
	}
}

#endif