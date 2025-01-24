#include "Lz4Stream.h"
#include "../../Asserts.h"

#if !defined(WITH_LZ4)
#	pragma message("Death::IO::Lz4Stream requires `lz4` library")
#else

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#	if defined(_M_X64)
#		pragma comment(lib, "../Libs/Windows/x64/lz4.lib")
#	elif defined(_M_IX86)
#		pragma comment(lib, "../Libs/Windows/x86/lz4.lib")
#	else
#		error Unsupported architecture
#	endif
#endif

#include <algorithm>
#include <cstring>

namespace Death { namespace IO { namespace Compression {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	static std::int32_t GetLz4BlockSize(const LZ4F_frameInfo_t* info)
	{
		switch (info->blockSizeID) {
			case LZ4F_default:
			case LZ4F_max64KB:  return 1 << 16;
			case LZ4F_max256KB: return 1 << 18;
			case LZ4F_max1MB:   return 1 << 20;
			case LZ4F_max4MB:   return 1 << 22;
			default: return 0;
		}
	}

	Lz4Stream::Lz4Stream()
		: _inputStream(nullptr), _ctx(nullptr), _size(Stream::Invalid), _state(State::Unknown)
	{
	}

	Lz4Stream::Lz4Stream(Stream& inputStream, std::int32_t inputSize)
		: Lz4Stream()
	{
		Open(inputStream, inputSize);
	}

	Lz4Stream::~Lz4Stream()
	{
		Lz4Stream::Dispose();
	}

	Lz4Stream::Lz4Stream(Lz4Stream&& other) noexcept
	{
		_inputStream = other._inputStream;
		_ctx = other._ctx;
		_size = other._size;
		_inputSize = other._inputSize;
		_state = other._state;
		std::memcpy(_inBuffer, other._inBuffer, sizeof(_inBuffer));
		_inPos = other._inPos;
		_inLength = other._inLength;
		_outBuffer = std::move(other._outBuffer);
		_outCapacity = other._outCapacity;
		_outPos = other._outPos;
		_outPosTotal = other._outPosTotal;
		_outLength = other._outLength;

		other._ctx = nullptr;

		// Original instance will be disabled
		if (other._state == State::Created || other._state == State::Initialized) {
			other._state = State::Failed;
		}
	}

	Lz4Stream& Lz4Stream::operator=(Lz4Stream&& other) noexcept
	{
		Dispose();

		_inputStream = other._inputStream;
		_ctx = other._ctx;
		_size = other._size;
		_inputSize = other._inputSize;
		_state = other._state;
		std::memcpy(_inBuffer, other._inBuffer, sizeof(_inBuffer));
		_inPos = other._inPos;
		_inLength = other._inLength;
		_outBuffer = std::move(other._outBuffer);
		_outCapacity = other._outCapacity;
		_outPos = other._outPos;
		_outPosTotal = other._outPosTotal;
		_outLength = other._outLength;

		other._ctx = nullptr;

		// Original instance will be disabled
		if (other._state == State::Created || other._state == State::Initialized) {
			other._state = State::Failed;
		}

		return *this;
	}

	std::int64_t Lz4Stream::Seek(std::int64_t offset, SeekOrigin origin)
	{
		switch (origin) {
			case SeekOrigin::Current: {
				DEATH_ASSERT(offset >= 0, "Can't seek to negative values", Stream::OutOfRange);

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

	std::int64_t Lz4Stream::GetPosition() const
	{
		return static_cast<std::int64_t>(_outPosTotal);
	}

	std::int64_t Lz4Stream::Read(void* destination, std::int64_t bytesToRead)
	{
		if (bytesToRead <= 0) {
			return 0;
		}

		DEATH_ASSERT(destination != nullptr, "destination is null", 0);
		std::uint8_t* typedBuffer = static_cast<std::uint8_t*>(destination);
		std::int64_t bytesReadTotal = 0;

		do {
			// ReadInternal() can read only up to Lz4Stream::BufferSize bytes
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

	std::int64_t Lz4Stream::Write(const void* source, std::int64_t bytesToWrite)
	{
		// Not supported
		return Stream::Invalid;
	}

	bool Lz4Stream::Flush()
	{
		// Not supported
		return true;
	}

	bool Lz4Stream::IsValid()
	{
		if (_state == State::Created) {
			InitializeInternal();
		}
		return (_state == State::Initialized || _state == State::Finished);
	}

	std::int64_t Lz4Stream::GetSize() const
	{
		return _size;
	}

	void Lz4Stream::Open(Stream& inputStream, std::int32_t inputSize)
	{
		Dispose();

		_inputStream = &inputStream;
		_inputSize = inputSize;
		_state = State::Created;
		_size = Stream::NotSeekable;

		_inPos = 0;
		_inLength = 0;
		_outPos = 0;
		_outPosTotal = 0;
		_outLength = 0;

		auto result = LZ4F_createDecompressionContext(&_ctx, LZ4F_VERSION);
		if (LZ4F_isError(result)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("LZ4F_createDecompressionContext() failed with error 0x%zx (%s)", result, LZ4F_getErrorName(result));
#endif
			_state = State::Failed;
		}
	}

	void Lz4Stream::Dispose()
	{
		CeaseReading();

		LZ4F_freeDecompressionContext(_ctx);
		_ctx = nullptr;

		_inputStream = nullptr;
		_state = State::Unknown;
		_size = Stream::Invalid;
	}

	void Lz4Stream::InitializeInternal()
	{
		std::int64_t bytesRead = _inputSize;
		if (bytesRead < 0 || bytesRead > sizeof(_inBuffer)) {
			bytesRead = sizeof(_inBuffer);
		}

		bytesRead = _inputStream->Read(_inBuffer, bytesRead);
		if (bytesRead <= 0) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to read frame info from input stream");
#endif
			_state = State::Failed;
			return;
		}

		if (_inputSize > 0) {
			_inputSize -= bytesRead;
		}

		LZ4F_frameInfo_t info;
		std::size_t consumedSize = bytesRead;
		std::size_t result = LZ4F_getFrameInfo(_ctx, &info, _inBuffer, &consumedSize);
		if (LZ4F_isError(result)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("LZ4F_getFrameInfo() failed with error 0x%zx (%s)", result, LZ4F_getErrorName(result));
#endif
			_state = State::Failed;
			return;
		}

		_inPos = consumedSize;
		_inLength = bytesRead;

		std::int32_t blockSize = GetLz4BlockSize(&info);
		if (_outCapacity != blockSize) {
			_outBuffer = std::make_unique<char[]>(blockSize);
			_outCapacity = blockSize;
		}

		_state = State::Initialized;
	}

	std::int32_t Lz4Stream::ReadInternal(void* ptr, std::int32_t size)
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

		bool inBufferTooSmall = false;
		std::int32_t n = (_outLength - _outPos);
		while (n == 0) {
			if (_inPos >= _inLength || inBufferTooSmall) {
				std::int32_t bytesRead = _inputSize;
				if (bytesRead < 0 || bytesRead > sizeof(_inBuffer)) {
					bytesRead = sizeof(_inBuffer);
				}

				_inLength = _inputStream->Read(_inBuffer, bytesRead);
				if (_inLength <= 0) {
					return Stream::Invalid;
				}

				if (_inputSize > 0) {
					_inputSize -= _inLength;
				}

				_inPos = 0;
				inBufferTooSmall = false;
			}

			std::size_t dstSize = _outCapacity;
			std::size_t srcSize = _inLength - _inPos;
			auto result = LZ4F_decompress(_ctx, &_outBuffer[0], &dstSize, &_inBuffer[_inPos], &srcSize, nullptr);
			if (LZ4F_isError(result)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("LZ4F_decompress() failed with error 0x%zx (%s)", result, LZ4F_getErrorName(result));
#endif
				_state = State::Failed;
				return Stream::Invalid;
			}

			if (dstSize == 0) {
				inBufferTooSmall = true;
			}

			_inPos += srcSize;
			_outPos = 0;
			_outLength = dstSize;
			n = (_outLength - _outPos);
		}

		if (n > size) {
			n = size;
		}
		std::memcpy(ptr, &_outBuffer[_outPos], n);
		_outPos += n;
		return n;
	}

	bool Lz4Stream::CeaseReading()
	{
		if (_state != State::Initialized) {
			return true;
		}

		std::int64_t seekToEnd = _inLength - _inPos;
		if (seekToEnd != 0) {
			_inputStream->Seek(seekToEnd, SeekOrigin::Current);
		}

		_state = State::Finished;
		_size = static_cast<std::int32_t>(_outPosTotal);
		return true;
	}

	Lz4Writer::Lz4Writer(Stream& outputStream, std::int32_t compressionLevel)
		: _outputStream(&outputStream), _ctx(nullptr), _state(State::Created)
	{
		auto result = LZ4F_createCompressionContext(&_ctx, LZ4F_VERSION);
		if (LZ4F_isError(result)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("LZ4F_createCompressionContext() failed with error 0x%zx (%s)", result, LZ4F_getErrorName(result));
#endif
			_state = State::Failed;
		}

		const LZ4F_preferences_t kPrefs = {
			{ LZ4F_max256KB, LZ4F_blockLinked, LZ4F_noContentChecksum, LZ4F_frame, 0 /* Unknown content size */, 0 /* No DictID */ , LZ4F_noBlockChecksum },
			compressionLevel,	/* Compression level (0 == default) */
			0,					/* Auto-flush */
			0,					/* Favor decompression speed */
			{ 0, 0, 0 },		/* Reserved, must be set to 0 */
		};

		_outCapacity = LZ4F_compressBound(ChunkSize, &kPrefs);
		_outBuffer = std::make_unique<char[]>(_outCapacity);

		std::size_t headerSize = LZ4F_compressBegin(_ctx, &_outBuffer[0], _outCapacity, &kPrefs);
		if (LZ4F_isError(result)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("LZ4F_compressBegin() failed with error 0x%zx (%s)", headerSize, LZ4F_getErrorName(headerSize));
#endif
			_state = State::Failed;
		}

		// Header needs to be written before the first frame
		_outLength = static_cast<std::int32_t>(headerSize);
	}

	Lz4Writer::~Lz4Writer()
	{
		Lz4Writer::Dispose();
	}

	void Lz4Writer::Dispose()
	{
		if (_state != State::Created && _state != State::Initialized) {
			return;
		}

		if (_state == State::Initialized) {
			WriteInternal(nullptr, 0, true);
		}

		LZ4F_freeCompressionContext(_ctx);
		_ctx = nullptr;

		_state = State::Finished;
	}

	std::int64_t Lz4Writer::Seek(std::int64_t offset, SeekOrigin origin)
	{
		return Stream::NotSeekable;
	}

	std::int64_t Lz4Writer::GetPosition() const
	{
		return Stream::NotSeekable;
	}

	std::int64_t Lz4Writer::Read(void* destination, std::int64_t bytesToRead)
	{
		// Not supported
		return Stream::Invalid;
	}

	std::int64_t Lz4Writer::Write(const void* source, std::int64_t bytesToWrite)
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

	bool Lz4Writer::Flush()
	{
		std::size_t compressedSize = LZ4F_flush(_ctx, &_outBuffer[0], _outCapacity, nullptr);
		if (LZ4F_isError(compressedSize)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("LZ4F_flush() failed with error 0x%zx (%s)", compressedSize, LZ4F_getErrorName(compressedSize));
#endif
			_state = State::Failed;
			return false;
		}

		if (compressedSize > 0 && _outputStream->Write(&_outBuffer[0], compressedSize) <= 0) {
			return false;
		}

		return true;
	}

	bool Lz4Writer::IsValid()
	{
		return(_state != State::Failed && _outputStream->IsValid());
	}

	std::int64_t Lz4Writer::GetSize() const
	{
		return Stream::NotSeekable;
	}

	std::int32_t Lz4Writer::WriteInternal(const void* buffer, std::int32_t bytesToWrite, bool finish)
	{
		if (_outLength > 0) {
			if (_outputStream->Write(&_outBuffer[0], _outLength) <= 0) {
#if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Failed to write frame info to output stream");
#endif
				return Stream::Invalid;
			}
			_outLength = 0;
		}

		if (bytesToWrite > 0) {
			if (bytesToWrite > ChunkSize) {
				bytesToWrite = ChunkSize;
			}

			std::size_t compressedSize = LZ4F_compressUpdate(_ctx, &_outBuffer[0], _outCapacity,
				buffer, bytesToWrite, nullptr);
			if (LZ4F_isError(compressedSize)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("LZ4F_compressUpdate() failed with error 0x%zx (%s)", compressedSize, LZ4F_getErrorName(compressedSize));
#endif
				_state = State::Failed;
				return Stream::Invalid;
			}

			if (compressedSize > 0) {
				_outputStream->Write(_buffer, compressedSize);
			}
		}

		if (finish) {
			std::size_t compressedSize = LZ4F_compressEnd(_ctx, &_outBuffer[0], _outCapacity, nullptr);
			if (LZ4F_isError(compressedSize)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("LZ4F_compressEnd() failed with error 0x%zx (%s)", compressedSize, LZ4F_getErrorName(compressedSize));
#endif
				_state = State::Failed;
				return Stream::Invalid;
			}

			_outputStream->Write(&_outBuffer[0], compressedSize);
		}

		return bytesToWrite;
	}

}}}

#endif