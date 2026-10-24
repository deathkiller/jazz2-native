#include "Lz4Stream.h"

#if !defined(WITH_LZ4)
#	pragma message("Death::IO::Lz4Stream requires `lz4` library")
#else

#include "../../Asserts.h"
#include "../../Base/Move.h"

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
		: _inputStream(nullptr), _ctx(nullptr), _size(Stream::Invalid), _inputSize(-1), _state(State::Unknown),
			_inPos(0), _inLength(0), _outCapacity(0), _outPos(0), _outPosTotal(0), _outLength(0)
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
		: Lz4Stream()
	{
		*this = Death::move(other);
	}

	Lz4Stream& Lz4Stream::operator=(Lz4Stream&& other) noexcept
	{
		if (this == &other) {
			return *this;
		}

		Dispose();

		_inputStream = other._inputStream;
		_ctx = other._ctx;
		_size = other._size;
		_inputSize = other._inputSize;
		_state = other._state;
		std::memcpy(_inBuffer, other._inBuffer, sizeof(_inBuffer));
		_inPos = other._inPos;
		_inLength = other._inLength;
		_outBuffer = Death::move(other._outBuffer);
		_outCapacity = other._outCapacity;
		_outPos = other._outPos;
		_outPosTotal = other._outPosTotal;
		_outLength = other._outLength;

		other._ctx = nullptr;
		other._outCapacity = 0;
		other._outPos = 0;
		other._outLength = 0;

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
		std::uint8_t* typedBuffer = (std::uint8_t*)destination;
		std::int64_t bytesReadTotal = 0;

		do {
			// ReadInternal() can read only up to Lz4Stream::BufferSize bytes
			std::int32_t partialBytesToRead = (bytesToRead < INT32_MAX ? std::int32_t(bytesToRead) : INT32_MAX);
			std::int32_t bytesRead = ReadInternal(&typedBuffer[bytesReadTotal], partialBytesToRead);
			if DEATH_UNLIKELY(bytesRead < 0) {
				// Whatever was already decompressed into the caller's buffer is returned rather than thrown
				// away with the error - a stream that ends without its end marker fails on the very call that
				// gathers its final bytes. The state stays Failed, so the error surfaces on the next call.
				if (bytesReadTotal > 0) {
					break;
				}
				return bytesRead;
			} else if DEATH_UNLIKELY(bytesRead == 0) {
				break;
			}
			bytesReadTotal += bytesRead;
			bytesToRead -= bytesRead;
		} while (bytesToRead > 0);

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

	std::int64_t Lz4Stream::SetSize(std::int64_t size)
	{
		return Stream::Invalid;
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
			LOGE("LZ4F_createDecompressionContext() failed with error 0x{:x} ({})", result, LZ4F_getErrorName(result));
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
			_inputSize -= std::int32_t(bytesRead);
		}

		LZ4F_frameInfo_t info;
		std::size_t consumedSize = bytesRead;
		std::size_t result = LZ4F_getFrameInfo(_ctx, &info, _inBuffer, &consumedSize);
		if (LZ4F_isError(result)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("LZ4F_getFrameInfo() failed with error 0x{:x} ({})", result, LZ4F_getErrorName(result));
#endif
			_state = State::Failed;
			return;
		}

		_inPos = std::int32_t(consumedSize);
		_inLength = std::int32_t(bytesRead);

		std::int32_t blockSize = GetLz4BlockSize(&info);
		if (blockSize <= 0) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Unsupported LZ4 block size");
#endif
			_state = State::Failed;
			return;
		}
		if (_outCapacity != blockSize || _outBuffer == nullptr) {
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
		if (size <= 0) {
			return 0;
		}
		if (_state == State::Unknown || _state >= State::Failed) {
			return Stream::Invalid;
		}

		std::int32_t n = (_outLength - _outPos);
		while (n == 0) {
			if (_state == State::Finished) {
				return 0;
			}

			if (_inPos >= _inLength) {
				std::int32_t bytesToRead = _inputSize;
				if (bytesToRead < 0 || bytesToRead > std::int32_t(sizeof(_inBuffer))) {
					bytesToRead = std::int32_t(sizeof(_inBuffer));
				}

				std::int64_t bytesRead = (bytesToRead > 0 ? _inputStream->Read(_inBuffer, bytesToRead) : 0);
				if (bytesRead <= 0) {
#if defined(DEATH_TRACE_VERBOSE_IO)
					LOGE("Compressed stream ended prematurely");
#endif
					_inPos = 0;
					_inLength = 0;
					_state = State::Failed;
					return Stream::Invalid;
				}

				if (_inputSize > 0) {
					_inputSize -= std::int32_t(bytesRead);
				}

				_inPos = 0;
				_inLength = std::int32_t(bytesRead);
			}

			std::size_t dstSize = _outCapacity;
			std::size_t srcSize = _inLength - _inPos;
			auto result = LZ4F_decompress(_ctx, &_outBuffer[0], &dstSize, &_inBuffer[_inPos], &srcSize, nullptr);
			if (LZ4F_isError(result) || (result != 0 && srcSize == 0 && dstSize == 0)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("LZ4F_decompress() failed with error 0x{:x} ({})", result, LZ4F_isError(result) ? LZ4F_getErrorName(result) : "No progress");
#endif
				_state = State::Failed;
				return Stream::Invalid;
			}

			_inPos += std::int32_t(srcSize);
			_outPos = 0;
			_outLength = std::int32_t(dstSize);
			n = _outLength;

			if (result == 0 && !HasNextFrame()) {
				CeaseReading();
			}
		}

		if (n > size) {
			n = size;
		}
		std::memcpy(ptr, &_outBuffer[_outPos], n);
		_outPos += n;
		_outPosTotal += n;
		return n;
	}

	bool Lz4Stream::HasNextFrame()
	{
		// Concatenated frames are valid, the magic number tells the next one from unrelated trailing data
		std::int32_t available = _inLength - _inPos;
		if (available < 4 && _inputSize != 0) {
			std::memmove(_inBuffer, &_inBuffer[_inPos], available);
			_inPos = 0;
			_inLength = available;
			while (_inLength < 4 && _inputSize != 0) {
				std::int32_t bytesToRead = std::int32_t(sizeof(_inBuffer)) - _inLength;
				if (_inputSize > 0 && bytesToRead > _inputSize) {
					bytesToRead = _inputSize;
				}
				std::int64_t bytesRead = _inputStream->Read(&_inBuffer[_inLength], bytesToRead);
				if (bytesRead <= 0) {
					break;
				}
				if (_inputSize > 0) {
					_inputSize -= std::int32_t(bytesRead);
				}
				_inLength += std::int32_t(bytesRead);
			}
			available = _inLength;
		}
		if (available < 4) {
			return false;
		}

		const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(&_inBuffer[_inPos]);
		std::uint32_t magic = std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
		// LZ4 frame or skippable frame
		return (magic == 0x184D2204u || (magic & 0xFFFFFFF0u) == 0x184D2A50u);
	}

	bool Lz4Stream::CeaseReading()
	{
		if (_state != State::Initialized) {
			return true;
		}

		std::int64_t seekToEnd = (_inputSize >= 0 ? std::int64_t(_inputSize) : -std::int64_t(_inLength - _inPos));
		if (seekToEnd != 0) {
			_inputStream->Seek(seekToEnd, SeekOrigin::Current);
		}
		_inPos = 0;
		_inLength = 0;
		if (_inputSize > 0) {
			_inputSize = 0;
		}

		_state = State::Finished;
		_size = std::int64_t(_outPosTotal) + (_outLength - _outPos);
		return true;
	}

	Lz4Writer::Lz4Writer(Stream& outputStream, std::int32_t compressionLevel)
		: _outputStream(&outputStream), _ctx(nullptr), _state(State::Created), _outCapacity(0), _outLength(0)
	{
		auto result = LZ4F_createCompressionContext(&_ctx, LZ4F_VERSION);
		if (LZ4F_isError(result)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("LZ4F_createCompressionContext() failed with error 0x{:x} ({})", result, LZ4F_getErrorName(result));
#endif
			_ctx = nullptr;
			_state = State::Failed;
			return;
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
		if (LZ4F_isError(headerSize)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("LZ4F_compressBegin() failed with error 0x{:x} ({})", headerSize, LZ4F_getErrorName(headerSize));
#endif
			_state = State::Failed;
			return;
		}

		// Header needs to be written before the first frame
		_outLength = std::int32_t(headerSize);
	}

	Lz4Writer::~Lz4Writer()
	{
		Lz4Writer::Dispose();
	}

	void Lz4Writer::Dispose()
	{
		if (_state != State::Created && _state != State::Initialized) {
			LZ4F_freeCompressionContext(_ctx);
			_ctx = nullptr;
			return;
		}

		WriteInternal(nullptr, 0, true);

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

	bool Lz4Writer::Flush()
	{
		if (_state != State::Created && _state != State::Initialized) {
			return false;
		}
		// The header has to come first
		if (WriteInternal(nullptr, 0, false) < 0) {
			return false;
		}

		std::size_t compressedSize = LZ4F_flush(_ctx, &_outBuffer[0], _outCapacity, nullptr);
		if (LZ4F_isError(compressedSize)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("LZ4F_flush() failed with error 0x{:x} ({})", compressedSize, LZ4F_getErrorName(compressedSize));
#endif
			_state = State::Failed;
			return false;
		}

		if (compressedSize > 0 && _outputStream->Write(&_outBuffer[0], std::int64_t(compressedSize)) != std::int64_t(compressedSize)) {
			_state = State::Failed;
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

	std::int64_t Lz4Writer::SetSize(std::int64_t size)
	{
		return Stream::Invalid;
	}

	std::int32_t Lz4Writer::WriteInternal(const void* buffer, std::int32_t bytesToWrite, bool finish)
	{
		if (_outLength > 0) {
			if (_outputStream->Write(&_outBuffer[0], _outLength) != _outLength) {
#if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Failed to write frame info to output stream");
#endif
				_state = State::Failed;
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
				LOGE("LZ4F_compressUpdate() failed with error 0x{:x} ({})", compressedSize, LZ4F_getErrorName(compressedSize));
#endif
				_state = State::Failed;
				return Stream::Invalid;
			}

			if (compressedSize > 0 && _outputStream->Write(&_outBuffer[0], std::int64_t(compressedSize)) != std::int64_t(compressedSize)) {
				_state = State::Failed;
				return Stream::Invalid;
			}
		}

		if (finish) {
			std::size_t compressedSize = LZ4F_compressEnd(_ctx, &_outBuffer[0], _outCapacity, nullptr);
			if (LZ4F_isError(compressedSize)) {
#if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("LZ4F_compressEnd() failed with error 0x{:x} ({})", compressedSize, LZ4F_getErrorName(compressedSize));
#endif
				_state = State::Failed;
				return Stream::Invalid;
			}

			if (compressedSize > 0 && _outputStream->Write(&_outBuffer[0], std::int64_t(compressedSize)) != std::int64_t(compressedSize)) {
				_state = State::Failed;
				return Stream::Invalid;
			}
		}

		return bytesToWrite;
	}

}}}

#endif