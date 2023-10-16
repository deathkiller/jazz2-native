#pragma once

#include "../Common.h"
#include "../Containers/String.h"

#include <cstdio>		// For FILE
#include <memory>

namespace Death::IO
{
	enum struct FileAccessMode {
		None = 0,

		Read = 0x01,
		Write = 0x02,

#if !defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_MINGW)
		FileDescriptor = 0x80,
#endif
	};

	DEFINE_ENUM_OPERATORS(FileAccessMode);

	enum class SeekOrigin {
		Begin = SEEK_SET,
		Current = SEEK_CUR,
		End = SEEK_END
	};

	/** @brief Provides a generic view of a sequence of bytes */
	class Stream
	{
	public:
		enum class Type {
			None,
			File,
			Memory,
			AndroidAsset
		};

		explicit Stream() : _type(Type::None), _size(0) { }
		virtual ~Stream() { }

		Type GetType() const {
			return _type;
		}

		/** @brief Tries to open the stream */
		virtual void Open(FileAccessMode mode) = 0;
		/** @brief Closes the stream */
		virtual void Close() = 0;
		/** @brief Seeks in an opened stream */
		virtual std::int32_t Seek(std::int32_t offset, SeekOrigin origin) const = 0;
		/** @brief Tells the seek position of an opened stream */
		virtual std::int32_t GetPosition() const = 0;
		/** @brief Reads a certain amount of bytes from the stream to a buffer */
		virtual std::int32_t Read(void* buffer, std::int32_t bytes) const = 0;
		/** @brief Writes a certain amount of bytes from a buffer to the stream */
		virtual std::int32_t Write(const void* buffer, std::int32_t bytes) = 0;
		/** @brief Returns true if the stream has been sucessfully opened */
		virtual bool IsValid() const = 0;

		/** @brief Returns file path if any */
		Containers::StringView GetPath() const {
			return _path;
		}

		/** @brief Returns stream size in bytes */
		DEATH_ALWAYS_INLINE std::int32_t GetSize() const {
			return _size;
		}

		virtual void SetCloseOnDestruction(bool shouldCloseOnDestruction) { }

		template<typename T, typename std::enable_if<std::is_trivially_constructible<T>::value>::type* = nullptr>
		DEATH_ALWAYS_INLINE T ReadValue()
		{
			T buffer;
			Read(&buffer, sizeof(T));
			return buffer;
		}

		template<typename T, typename std::enable_if<std::is_trivially_constructible<T>::value>::type* = nullptr>
		DEATH_ALWAYS_INLINE void WriteValue(const T& value)
		{
			Write(&value, sizeof(T));
		}

		std::int32_t ReadVariableInt32()
		{
			std::uint32_t n = ReadVariableUint32();
			return (std::int32_t)(n >> 1) ^ -(std::int32_t)(n & 1);
		}

		std::int64_t ReadVariableInt64()
		{
			std::uint64_t n = ReadVariableUint64();
			return (std::int64_t)(n >> 1) ^ -(std::int64_t)(n & 1);
		}

		std::uint32_t ReadVariableUint32()
		{
			std::int32_t pos = GetPosition();
			std::uint32_t result = 0;
			std::uint32_t shift = 0;
			while (pos < _size) {
				std::uint8_t byte = ReadValue<std::uint8_t>();
				result |= (byte & 0x7f) << shift;
				shift += 7;
				if ((byte & 0x80) == 0) {
					break;
				}
				pos++;
			}
			return result;
		}

		std::uint64_t ReadVariableUint64()
		{
			std::int32_t pos = GetPosition();
			std::uint64_t result = 0;
			std::uint64_t shift = 0;
			while (pos < _size) {
				std::uint8_t byte = ReadValue<std::uint8_t>();
				result |= (byte & 0x7f) << shift;
				shift += 7;
				if ((byte & 0x80) == 0) {
					break;
				}
				pos++;
			}
			return result;
		}

		std::int32_t WriteVariableInt32(std::int32_t value)
		{
			std::uint32_t n = (std::uint32_t)(value << 1) ^ (std::uint32_t)(value >> 31);
			return WriteVariableUint32(n);
		}

		std::int32_t WriteVariableInt64(std::int64_t value)
		{
			std::uint64_t n = (std::uint64_t)(value << 1) ^ (std::uint64_t)(value >> 63);
			return WriteVariableUint64(n);
		}

		std::int32_t WriteVariableUint32(std::uint32_t value)
		{
			std::int32_t bytesWritten = 1;
			std::uint32_t valueLeft = value;
			while (valueLeft >= 0x80) {
				WriteValue((std::uint8_t)(valueLeft | 0x80));
				valueLeft = valueLeft >> 7;
				bytesWritten++;
			}
			WriteValue((std::uint8_t)valueLeft);
			return bytesWritten;
		}

		std::int32_t WriteVariableUint64(std::uint64_t value)
		{
			std::int32_t bytesWritten = 1;
			std::uint64_t valueLeft = value;
			while (valueLeft >= 0x80) {
				WriteValue((std::uint8_t)(valueLeft | 0x80));
				valueLeft = valueLeft >> 7;
				bytesWritten++;
			}
			WriteValue((std::uint8_t)valueLeft);
			return bytesWritten;
		}

#if defined(DEATH_TARGET_BIG_ENDIAN)
		DEATH_ALWAYS_INLINE static std::uint16_t Uint16FromBE(std::uint16_t value)
#else
		DEATH_ALWAYS_INLINE static std::uint16_t Uint16FromLE(std::uint16_t value)
#endif
		{
			return value;
		}

#if defined(DEATH_TARGET_BIG_ENDIAN)
		DEATH_ALWAYS_INLINE static std::uint32_t Uint32FromBE(std::uint32_t value)
#else
		DEATH_ALWAYS_INLINE static std::uint32_t Uint32FromLE(std::uint32_t value)
#endif
		{
			return value;
		}

#if defined(DEATH_TARGET_BIG_ENDIAN)
		DEATH_ALWAYS_INLINE static std::uint64_t Uint64FromBE(std::uint64_t value)
#else
		DEATH_ALWAYS_INLINE static std::uint64_t Uint64FromLE(std::uint64_t value)
#endif
		{
			return value;
		}

#if defined(DEATH_TARGET_BIG_ENDIAN)
		DEATH_ALWAYS_INLINE static std::uint16_t Uint16FromLE(std::uint16_t value)
#else
		DEATH_ALWAYS_INLINE static std::uint16_t Uint16FromBE(std::uint16_t value)
#endif
		{
			return (value >> 8) | (value << 8);
		}

#if defined(DEATH_TARGET_BIG_ENDIAN)
		DEATH_ALWAYS_INLINE static std::uint32_t Uint32FromLE(std::uint32_t value)
#else
		DEATH_ALWAYS_INLINE static std::uint32_t Uint32FromBE(std::uint32_t value)
#endif
		{
			return (value >> 24) | ((value << 8) & 0x00FF0000) | ((value >> 8) & 0x0000FF00) | (value << 24);
		}

#if defined(DEATH_TARGET_BIG_ENDIAN)
		DEATH_ALWAYS_INLINE static std::uint64_t Uint64FromLE(std::uint64_t value)
#else
		DEATH_ALWAYS_INLINE static std::uint64_t Uint64FromBE(std::uint64_t value)
#endif
		{
			return (value >> 56) | ((value << 40) & 0x00FF000000000000ULL) | ((value << 24) & 0x0000FF0000000000ULL) |
				((value << 8) & 0x000000FF00000000ULL) | ((value >> 8) & 0x00000000FF000000ULL) |
				((value >> 24) & 0x0000000000FF0000ULL) | ((value >> 40) & 0x000000000000FF00ULL) | (value << 56);
		}

	protected:
		Type _type;
		Containers::String _path;
		std::int32_t _size;
	};
}