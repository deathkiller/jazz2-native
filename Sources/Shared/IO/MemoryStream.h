#pragma once

#include "Stream.h"
#include "../Containers/Array.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Stream interface around a memory buffer
	*/ 
	class MemoryStream : public Stream
	{
	public:
		explicit MemoryStream(std::int64_t initialCapacity = 0);
		MemoryStream(std::uint8_t* bufferPtr, std::int64_t bufferSize);
		MemoryStream(const std::uint8_t* bufferPtr, std::int64_t bufferSize);

		MemoryStream(const MemoryStream&) = delete;
		MemoryStream& operator=(const MemoryStream&) = delete;

		void Close() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int32_t Read(void* buffer, std::int32_t bytes) override;
		std::int32_t Write(const void* buffer, std::int32_t bytes) override;

		bool IsValid() override;

		void ReserveCapacity(std::int64_t bytes);
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

		Containers::Array<std::uint8_t> _buffer;
		mutable std::int64_t _seekOffset;
		AccessMode _mode;
	};

}}