#pragma once

#if defined(__ANDROID__) && defined(__ANDROID_API__) && __ANDROID_API__ < 24
//	Android fully supports 64-bit file offsets only for API 24 and above.
#elif !defined(DOXYGEN_GENERATING_OUTPUT)
#	define _FILE_OFFSET_BITS 64
#endif

// Enable newer ABI on Mac OS 10.5 and later, which is needed for struct stat to have birthtime members
#if defined(__APPLE__) || defined(__MACH__)
#	define _DARWIN_USE_64_BIT_INODE 1
#endif

#include "../Common.h"
#include "../Base/IDisposable.h"

#if defined(DEATH_TARGET_BIG_ENDIAN)
#	include "../Base/Memory.h"
#endif

#include <type_traits>
#if defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
#	include <intrin.h>	// For _byteswap_ushort()/_byteswap_ulong()/_byteswap_uint64()
#endif

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Specifies the position in a stream to use for seeking
	*/
	enum class SeekOrigin {
		/** @brief Specifies the beginning of a stream */
		Begin,
		/** @brief Specifies the current position within a stream */
		Current,
		/** @brief Specifies the end of a stream */
		End
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

		/** @brief Whether the stream has been sucessfully opened */
		explicit operator bool() {
			return IsValid();
		}

		/** @brief Closes the stream and releases all assigned resources */
		virtual void Dispose() = 0;
		/** @brief Seeks in an opened stream */
		virtual std::int64_t Seek(std::int64_t offset, SeekOrigin origin) = 0;
		/** @brief Tells the seek position of an opened stream */
		virtual std::int64_t GetPosition() const = 0;
		/** @brief Reads a certain amount of bytes from the stream to a buffer */
		virtual std::int64_t Read(void* destination, std::int64_t bytesToRead) = 0;
		/** @brief Writes a certain amount of bytes from a buffer to the stream */
		virtual std::int64_t Write(const void* source, std::int64_t bytesToWrite) = 0;
		/** @brief Clears all buffers for this stream and causes any buffered data to be written to the underlying device */
		virtual bool Flush() = 0;
		/** @brief Returns `true` if the stream has been sucessfully opened */
		virtual bool IsValid() = 0;

		/** @brief Returns stream size in bytes */
		virtual std::int64_t GetSize() const = 0;
		/** @brief Sets stream size in bytes */
		virtual std::int64_t SetSize(std::int64_t size) = 0;

		/** @brief Reads the bytes from the current stream and writes them to the target stream */
		std::int64_t CopyTo(Stream& targetStream);

		/** @brief Reads a trivial value from the stream */
		template<typename T>
		DEATH_ALWAYS_INLINE T ReadValue()
		{
			static_assert(std::is_trivially_copyable<T>::value, "ReadValue() requires the source type to be trivially copyable");
			static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value, "ReadValue() must not be used on pointer or reference types");

			T value{};
			Read(&value, sizeof(T));
			return value;
		}

		/** @brief Writes a trivial value to the stream */
		template<typename T>
		DEATH_ALWAYS_INLINE void WriteValue(const T& value)
		{
			static_assert(std::is_trivially_copyable<T>::value, "WriteValue() requires the source type to be trivially copyable");
			static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value, "WriteValue() must not be used on pointer or reference types");

			Write(&value, sizeof(T));
		}

		/** @brief Reads a trivial value from the stream always as Little-Endian */
		template<typename T>
		DEATH_ALWAYS_INLINE T ReadValueAsLE()
		{
			static_assert(std::is_trivially_copyable<T>::value, "ReadValueAsLE() requires the source type to be trivially copyable");
			static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value, "ReadValueAsLE() must not be used on pointer or reference types");
			static_assert(sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "ReadValueAsLE() requires the source type to be 2, 4 or 8 bytes");

			T value{};
			Read(&value, sizeof(T));
#if defined(DEATH_TARGET_BIG_ENDIAN)
			value = Memory::SwapBytes(value);
#endif
			return value;
		}

		/** @brief Writes a trivial value to the stream always as Little-Endian */
		template<typename T>
		DEATH_ALWAYS_INLINE void WriteValueAsLE(T value)
		{
			static_assert(std::is_trivially_copyable<T>::value, "WriteValueAsLE() requires the source type to be trivially copyable");
			static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value, "WriteValueAsLE() must not be used on pointer or reference types");
			static_assert(sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "WriteValueAsLE() requires the source type to be 2, 4 or 8 bytes");

#if defined(DEATH_TARGET_BIG_ENDIAN)
			value = Memory::SwapBytes(value);
#endif
			Write(&value, sizeof(T));
		}

		/** @brief Reads a 32-bit integer value from the stream using variable-length quantity encoding */
		std::int32_t ReadVariableInt32();
		/** @brief Reads a 64-bit integer value from the stream using variable-length quantity encoding */
		std::int64_t ReadVariableInt64();
		/** @brief Reads a 32-bit unsigned integer value from the stream using variable-length quantity encoding */
		std::uint32_t ReadVariableUint32();
		/** @brief Reads a 64-bit unsigned integer value from the stream using variable-length quantity encoding */
		std::uint64_t ReadVariableUint64();

		/** @brief Writes a 32-bit integer value to the stream using variable-length quantity encoding */
		std::int64_t WriteVariableInt32(std::int32_t value);
		/** @brief Writes a 64-bit integer value to the stream using variable-length quantity encoding */
		std::int64_t WriteVariableInt64(std::int64_t value);
		/** @brief Writes a 32-bit unsigned integer value to the stream using variable-length quantity encoding */
		std::int64_t WriteVariableUint32(std::uint32_t value);
		/** @brief Writes a 64-bit unsigned integer value to the stream using variable-length quantity encoding */
		std::int64_t WriteVariableUint64(std::uint64_t value);
	};

}}