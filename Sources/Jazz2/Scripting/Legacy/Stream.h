#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Common.h"

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		/**
		 * @brief A binary data stream for serialization and file I/O
		 *
		 * Reference-counted growable byte buffer that scripts use to serialize values. Typed `push`/`pop` and `get`
		 * helpers append and consume primitives and strings from either end, and the whole stream can be saved to or
		 * loaded from a file. Also used as the payload of networking packets sent via `jjSendPacket`.
		 */
		class jjSTREAM
		{
		public:
			/** @brief Creates a new instance */
			jjSTREAM();
			~jjSTREAM();

			/** @brief Returns a new empty instance */
			static jjSTREAM* Create();
			/** @brief Returns a new instance populated from a file */
			static jjSTREAM* CreateFromFile(const String& filename);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjSTREAM& operator=(const jjSTREAM& o);

			/** @brief Returns the size of the stream in bytes */
			std::uint32_t getSize() const;
			/** @brief Returns whether the stream contains no data */
			bool isEmpty() const;
			/** @brief Saves the stream contents to a file */
			bool save(const String& tilename) const;
			/** @brief Removes all data from the stream */
			void clear();
			/** @brief Discards the given number of bytes from the front of the stream */
			bool discard(std::uint32_t count);

			/** @brief Appends the raw bytes of a string to the stream */
			bool write(const String& value);
			/** @brief Appends the contents of another stream */
			bool write(const jjSTREAM& value);
			/** @brief Reads the given number of bytes from the front into a string */
			bool get(String& value, std::uint32_t count);
			/** @brief Reads the given number of bytes from the front into another stream */
			bool get(jjSTREAM& value, std::uint32_t count);
			/** @brief Reads bytes from the front up to a delimiter into a string */
			bool getLine(String& value, const String& delim);

			/** @brief Appends a boolean value to the stream */
			bool push(bool value);
			/** @brief Appends an unsigned 8-bit value to the stream */
			bool push(std::uint8_t value);
			/** @brief Appends a signed 8-bit value to the stream */
			bool push(std::int8_t value);
			/** @brief Appends an unsigned 16-bit value to the stream */
			bool push(std::uint16_t value);
			/** @brief Appends a signed 16-bit value to the stream */
			bool push(std::int16_t value);
			/** @brief Appends an unsigned 32-bit value to the stream */
			bool push(std::uint32_t value);
			/** @brief Appends a signed 32-bit value to the stream */
			bool push(std::int32_t value);
			/** @brief Appends an unsigned 64-bit value to the stream */
			bool push(std::uint64_t value);
			/** @brief Appends a signed 64-bit value to the stream */
			bool push(std::int64_t value);
			/** @brief Appends a single-precision float to the stream */
			bool push(float value);
			/** @brief Appends a double-precision float to the stream */
			bool push(double value);
			/** @brief Appends a length-prefixed string to the stream */
			bool push(const String& value);
			/** @brief Appends the contents of another stream, prefixed with their length */
			bool push(const jjSTREAM& value);

			/** @brief Reads a boolean value from the front of the stream */
			bool pop(bool& value);
			/** @brief Reads an unsigned 8-bit value from the front of the stream */
			bool pop(std::uint8_t& value);
			/** @brief Reads a signed 8-bit value from the front of the stream */
			bool pop(std::int8_t& value);
			/** @brief Reads an unsigned 16-bit value from the front of the stream */
			bool pop(std::uint16_t& value);
			/** @brief Reads a signed 16-bit value from the front of the stream */
			bool pop(std::int16_t& value);
			/** @brief Reads an unsigned 32-bit value from the front of the stream */
			bool pop(std::uint32_t& value);
			/** @brief Reads a signed 32-bit value from the front of the stream */
			bool pop(std::int32_t& value);
			/** @brief Reads an unsigned 64-bit value from the front of the stream */
			bool pop(std::uint64_t& value);
			/** @brief Reads a signed 64-bit value from the front of the stream */
			bool pop(std::int64_t& value);
			/** @brief Reads a single-precision float from the front of the stream */
			bool pop(float& value);
			/** @brief Reads a double-precision float from the front of the stream */
			bool pop(double& value);
			/** @brief Reads a length-prefixed string from the front of the stream */
			bool pop(String& value);
			/** @brief Reads a length-prefixed stream from the front of the stream into another stream */
			bool pop(jjSTREAM& value);

			/** @brief Compresses the unread bytes of a stream as a zlib stream */
			static bool ZlibCompress(const jjSTREAM& input, jjSTREAM& output);
			/** @brief Decompresses a zlib stream that inflates to exactly @p size bytes */
			static bool ZlibUncompress(const jjSTREAM& input, jjSTREAM& output, std::uint32_t size);
			/** @brief Returns the CRC-32 of the unread bytes of a stream, continuing from @p crc */
			static std::uint32_t CRC32(const jjSTREAM& input, std::uint32_t crc);

		private:
			std::int32_t _refCount;
			SmallVector<std::uint8_t, 0> _data;
			// Reads advance this instead of erasing from the front, so reading a large stream stays linear
			std::uint32_t _readPos;

			// Reads the length before a string or a stream, if that many bytes follow it
			bool PopLength(std::uint32_t& length);
			const std::uint8_t* PeekFront(std::uint32_t count) const;
			bool Take(void* target, std::uint32_t count);
			void Append(const void* source, std::uint32_t count);

			template<class T>
			bool PushValue(const T& value) {
				Append(&value, sizeof(T));
				return true;
			}

			template<class T>
			bool PopValue(T& value) {
				return Take(&value, sizeof(T));
			}
		};

		/** @brief A seedable pseudo-random number generator */
		class jjRNG
		{
		public:
			/** @brief Creates a new instance seeded with the given value */
			jjRNG(std::uint64_t seed);
			~jjRNG();

			/** @brief Returns a new instance seeded with the given value */
			static jjRNG* Create(std::uint64_t seed);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			std::uint64_t operator()();

			jjRNG& operator=(const jjRNG& o);

			bool operator==(const jjRNG& o) const;

			/** @brief Reseeds the generator with the given value */
			void seed(std::uint64_t value);
			/** @brief Advances the generator past the given number of outputs */
			void discard(std::uint64_t count);

		private:
			const std::uint64_t DefaultInitSequence = 0xda3e39cb94b95bdbULL;

			std::int32_t _refCount;
			RandomGenerator _random;
		};
	}
}

#endif
