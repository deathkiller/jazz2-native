#pragma once

#include "Stream.h"
#include "../Containers/Array.h"

namespace Death::IO
{
	/** @brief The class creating a stream interface around a memory buffer */ 
	class MemoryStream : public Stream
	{
	public:
		explicit MemoryStream(std::int32_t initialCapacity = 0);
		MemoryStream(std::uint8_t* bufferPtr, std::int32_t bufferSize);
		MemoryStream(const std::uint8_t* bufferPtr, std::int32_t bufferSize);

		void Close() override;
		std::int32_t Seek(std::int32_t offset, SeekOrigin origin) override;
		std::int32_t GetPosition() const override;
		std::int32_t Read(void* buffer, std::int32_t bytes) override;
		std::int32_t Write(const void* buffer, std::int32_t bytes) override;

		bool IsValid() const override;

		void ReserveCapacity(std::int32_t bytes);
		std::int32_t FetchFromStream(Stream& s, std::int32_t bytes);

		DEATH_ALWAYS_INLINE const std::uint8_t* GetBuffer() const {
			return _buffer.data();
		}

	private:
		enum class AccessMode {
			None,
			ReadOnly,
			Writable,
			Growable
		};

		MemoryStream(const MemoryStream&) = delete;
		MemoryStream& operator=(const MemoryStream&) = delete;

		Containers::Array<std::uint8_t> _buffer;
		mutable std::int32_t _seekOffset;
		AccessMode _mode;
	};
}
