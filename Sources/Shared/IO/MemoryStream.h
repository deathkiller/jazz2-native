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
		/** @brief Default constructor */
		MemoryStream();
		/** @brief Construct a growable stream with the specified initial capacity */
		explicit MemoryStream(std::int64_t initialCapacity);

		/** @brief Construct a writable stream that references the specified region of memory */
		MemoryStream(void* bufferPtr, std::int64_t bufferSize);
		/** @brief Construct a read-only stream that references the specified region of memory */
		MemoryStream(const void* bufferPtr, std::int64_t bufferSize);
		/** @overload */
		MemoryStream(Containers::ArrayView<const char> buffer);
		/** @overload */
		MemoryStream(Containers::ArrayView<const std::uint8_t> buffer);

		/** @brief Construct a growable stream with a copy of the specified region of memory */
		explicit MemoryStream(Containers::InPlaceInitT, Containers::ArrayView<const char> buffer);
		/** @overload */
		explicit MemoryStream(Containers::InPlaceInitT, Containers::ArrayView<const std::uint8_t> buffer);

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
		std::int64_t SetSize(std::int64_t size) override;

		/** @brief Reserves a capacity in growable memory stream */
		void ReserveCapacity(std::int64_t bytes);
		/** @brief Copies a specified number of bytes from a source stream to the current position */
		std::int64_t FetchFromStream(Stream& source, std::int64_t bytesToRead);

		/** @brief Returns a pointer to underlying buffer */
		DEATH_ALWAYS_INLINE std::uint8_t* GetBuffer() {
			return _data.data();
		}

		/** @overload */
		DEATH_ALWAYS_INLINE const std::uint8_t* GetBuffer() const {
			return _data.data();
		}

		/** @brief Returns a pointer to the current position, which is then incremented by a given number of bytes */
		DEATH_ALWAYS_INLINE const std::uint8_t* GetCurrentPointer(std::int64_t bytes) {
			if (_pos + bytes > _size) {
				return nullptr;
			}
			const std::uint8_t* ptr = _data.data() + _pos;
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

		Containers::Array<std::uint8_t> _data;
		std::int64_t _size;
		std::int64_t _pos;
		AccessMode _mode;
	};

}}

namespace Death { namespace Containers { namespace Implementation {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	template<> struct ArrayViewConverter<std::uint8_t, IO::MemoryStream> {
		static ArrayView<std::uint8_t> from(IO::MemoryStream& other) {
			return { other.GetBuffer(), std::size_t(other.GetSize()) };
		}
		static ArrayView<std::uint8_t> from(IO::MemoryStream&& other) {
			return { other.GetBuffer(), std::size_t(other.GetSize()) };
		}
	};
	template<> struct ArrayViewConverter<const std::uint8_t, IO::MemoryStream> {
		static ArrayView<const std::uint8_t> from(const IO::MemoryStream& other) {
			return { other.GetBuffer(), std::size_t(other.GetSize()) };
		}
	};
	template<> struct ErasedArrayViewConverter<IO::MemoryStream> : ArrayViewConverter<std::uint8_t, IO::MemoryStream> {};
	template<> struct ErasedArrayViewConverter<const IO::MemoryStream> : ArrayViewConverter<const std::uint8_t, IO::MemoryStream> {};

}}}
