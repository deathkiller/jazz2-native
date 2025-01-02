#pragma once

#include "Stream.h"
#include "../Containers/Array.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Provides stream interface for a region of memory
	*/ 
	class MemoryStream : public Stream
	{
	public:
		explicit MemoryStream(std::int64_t initialCapacity = 0);
		MemoryStream(void* bufferPtr, std::int64_t bufferSize);
		MemoryStream(const void* bufferPtr, std::int64_t bufferSize);
		MemoryStream(Containers::ArrayView<const char> buffer);
		MemoryStream(Containers::ArrayView<const std::uint8_t> buffer);

		MemoryStream(const MemoryStream&) = delete;
		MemoryStream& operator=(const MemoryStream&) = delete;

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;
		std::int64_t GetSize() const override;

		void ReserveCapacity(std::int64_t bytes);
		std::int64_t FetchFromStream(Stream& source, std::int64_t bytesToRead);

		DEATH_ALWAYS_INLINE std::uint8_t* GetBuffer() {
			return _buffer.data();
		}

		DEATH_ALWAYS_INLINE const std::uint8_t* GetBuffer() const {
			return _buffer.data();
		}

		DEATH_ALWAYS_INLINE const std::uint8_t* GetCurrentPointer(std::int64_t bytes) {
			if (_pos + bytes > _size) {
				return nullptr;
			}
			const std::uint8_t* ptr = &_buffer[_pos];
			_pos += bytes;
			return ptr;
		}

	private:
		enum class AccessMode {
			None,
			ReadOnly,
			Writable,
			Growable
		};

		Containers::Array<std::uint8_t> _buffer;
		std::int64_t _size;
		std::int64_t _pos;
		AccessMode _mode;
	};

}}