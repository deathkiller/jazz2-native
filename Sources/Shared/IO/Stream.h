#pragma once

#if defined(__ANDROID__) && defined(__ANDROID_API__) && __ANDROID_API__ < 24
//	Android fully supports 64-bit file offsets only for API 24 and above.
#else
#	define _FILE_OFFSET_BITS 64
#endif

// Enable newer ABI on Mac OS 10.5 and later, which is needed for struct stat to have birthtime members
#if defined(__APPLE__) || defined(__MACH__)
#	define _DARWIN_USE_64_BIT_INODE 1
#endif

#include "../Common.h"
#include "../Base/IDisposable.h"

#include <cstdio>		// for FILE
#include <memory>

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Specifies the position in a stream to use for seeking
	*/
	enum class SeekOrigin {
		Begin = SEEK_SET,
		Current = SEEK_CUR,
		End = SEEK_END
	};

	/**
		@brief Provides a generic view of a sequence of bytes
	*/
	class Stream : public IDisposable
	{
	public:
		enum {
			/** @brief Returned if @ref Stream doesn't point to valid source */
			Invalid = -1,
			/** @brief Returned if one of the parameters provided to a method is not valid */
			OutOfRange = -2,
			/** @brief Returned if seek operation is not supported by @ref Stream or the stream length is unknown */
			NotSeekable = -3
		};

		Stream();

		/** @brief Closes the stream and releases all assigned resources */
		virtual void Dispose() = 0;
		/** @brief Seeks in an opened stream */
		virtual std::int64_t Seek(std::int64_t offset, SeekOrigin origin) = 0;
		/** @brief Tells the seek position of an opened stream */
		virtual std::int64_t GetPosition() const = 0;
		/** @brief Reads a certain amount of bytes from the stream to a buffer */
		virtual std::int32_t Read(void* buffer, std::int32_t bytes) = 0;
		/** @brief Writes a certain amount of bytes from a buffer to the stream */
		virtual std::int32_t Write(const void* buffer, std::int32_t bytes) = 0;
		/** @brief Clears all buffers for this stream and causes any buffered data to be written to the underlying device */
		virtual bool Flush() = 0;
		/** @brief Returns true if the stream has been sucessfully opened */
		virtual bool IsValid() = 0;

		/** @brief Returns stream size in bytes */
		virtual std::int64_t GetSize() const = 0;

		/** @brief Reads the bytes from the current stream and writes them to the target stream */
		std::int64_t CopyTo(Stream& targetStream);

		template<typename T, class = typename std::enable_if<std::is_trivially_constructible<T>::value>::type>
		DEATH_ALWAYS_INLINE T ReadValue()
		{
			T buffer = { };
			Read(&buffer, sizeof(T));
			return buffer;
		}

		template<typename T, class = typename std::enable_if<std::is_trivially_constructible<T>::value>::type>
		DEATH_ALWAYS_INLINE void WriteValue(const T& value)
		{
			Write(&value, sizeof(T));
		}

		std::int32_t ReadVariableInt32();
		std::int64_t ReadVariableInt64();
		std::uint32_t ReadVariableUint32();
		std::uint64_t ReadVariableUint64();

		std::int32_t WriteVariableInt32(std::int32_t value);
		std::int32_t WriteVariableInt64(std::int64_t value);
		std::int32_t WriteVariableUint32(std::uint32_t value);
		std::int32_t WriteVariableUint64(std::uint64_t value);

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
	};

}}