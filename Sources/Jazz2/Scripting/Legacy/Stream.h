#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Common.h"

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		/**
		 * @brief A buffer of bytes for storing data and exchanging it with files
		 *
		 * Data is appended at the end and read from the front, which removes it. `push()` and `pop()` store and
		 * restore values of primitive types, strings and other streams in binary form; `write()`, `get()` and
		 * @ref getLine() work with raw bytes. A stream can be saved to a file and created from one.
		 */
		class jjSTREAM
		{
		public:
			/** @brief Creates a new instance */
			jjSTREAM();
			~jjSTREAM();

			/** @brief Creates an empty stream */
			static jjSTREAM* Create();
			/**
			 * @brief Creates a stream with the contents of a file
			 *
			 * The file is looked up next to the game's data. If it doesn't exist or its name points outside that
			 * directory, the stream is empty, so `isEmpty()` tells whether anything was saved before.
			 */
			static jjSTREAM* CreateFromFile(const String& filename);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			/** @brief Replaces the contents of this stream with the unread contents of another one */
			jjSTREAM& operator=(const jjSTREAM& o);

			/** @brief Returns the number of bytes left in the stream */
			std::uint32_t getSize() const;
			/** @brief Returns whether no bytes are left in the stream */
			bool isEmpty() const;
			/**
			 * @brief Saves the contents of the stream to a file and returns whether it succeeded
			 *
			 * The file is written next to the game's data; names that point outside that directory are rejected. The
			 * stream itself is left unchanged. Always fails while the level runs inside the level editor.
			 */
			bool save(const String& tilename) const;
			/** @brief Removes all contents of the stream */
			void clear();
			/**
			 * @brief Removes `count` bytes from the front of the stream
			 *
			 * Returns `false` and leaves the stream unchanged if fewer bytes are left.
			 */
			bool discard(std::uint32_t count);

			/**
			 * @brief Appends the bytes of a string to the stream
			 *
			 * Unlike `push()`, no length is stored, so the text can be read back only with `get()` and a known size, or
			 * with @ref getLine().
			 * @partialsupport Always returns `true`.
			 */
			bool write(const String& value);
			/** @brief Appends the unread bytes of another stream, without storing their length; always returns `true` @partialsupport */
			bool write(const jjSTREAM& value);
			/**
			 * @brief Reads `count` bytes from the front of the stream into a string
			 *
			 * The bytes are removed from the stream. Returns `false` and leaves the stream unchanged if fewer bytes are
			 * left.
			 */
			bool get(String& value, std::uint32_t count);
			/**
			 * @brief Reads `count` bytes from the front of the stream and appends them to another stream
			 *
			 * The bytes are removed from this stream. Returns `false` and leaves it unchanged if fewer bytes are left.
			 */
			bool get(jjSTREAM& value, std::uint32_t count);
			/**
			 * @brief Reads text from the front of the stream up to a delimiter
			 *
			 * The text before `delim` is stored in `value`, and both are removed from the stream. If the delimiter isn't
			 * found, the rest of the stream becomes the line and the stream ends up empty. Returns `false` only if the
			 * stream is empty or `delim` is empty.
			 */
			bool getLine(String& value, const String& delim);

			/**
			 * @brief Appends a boolean value to the stream
			 *
			 * Every `push()` overload appends its value in binary form and returns `true`: 1 byte for `bool`, `int8` and
			 * `uint8`, 2 bytes for the 16-bit types, 4 bytes for `int`, `uint` and `float`, 8 bytes for the 64-bit types
			 * and `double`. Strings and streams take 4 bytes of length followed by their bytes. Read the values back
			 * with `pop()` in the same order and types.
			 */
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
			/** @brief Appends a string to the stream, preceded by its length */
			bool push(const String& value);
			/** @brief Appends the unread contents of another stream, preceded by their length */
			bool push(const jjSTREAM& value);

			/**
			 * @brief Reads a boolean value from the front of the stream
			 *
			 * Every `pop()` overload reads a value stored by the matching `push()` and removes its bytes. Returns `false`
			 * if the stream doesn't hold enough bytes for the value.
			 */
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
			/**
			 * @brief Reads a string stored by `push()` from the front of the stream
			 *
			 * Returns `false` and leaves the stream unchanged if the stored length exceeds the bytes left.
			 */
			bool pop(String& value);
			/**
			 * @brief Reads a stream stored by `push()` from the front of the stream and appends it to another stream
			 *
			 * Returns `false` and leaves the stream unchanged if the stored length exceeds the bytes left.
			 */
			bool pop(jjSTREAM& value);

			/**
			 * @brief Compresses the contents of a stream with zlib
			 *
			 * The compressed data replaces the contents of `output`; `input` is left unchanged. Returns whether it
			 * succeeded, which is always `false` in builds without zlib support.
			 */
			static bool ZlibCompress(const jjSTREAM& input, jjSTREAM& output);
			/**
			 * @brief Decompresses zlib data from a stream
			 *
			 * The decompressed data replaces the contents of `output`. Fails if `input` isn't valid compressed data or
			 * its decompressed size exceeds `size` bytes, and always in builds without zlib support.
			 */
			static bool ZlibUncompress(const jjSTREAM& input, jjSTREAM& output, std::uint32_t size);
			/**
			 * @brief Returns the CRC-32 checksum of the contents of a stream
			 *
			 * Uses the standard CRC-32 of zlib; pass a previous result as `crc` to continue a checksum.
			 * @partialsupport Always returns 0 in builds without zlib support.
			 */
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

		/**
		 * @brief A seedable pseudo-random number generator
		 *
		 * Each instance has a state of its own, so the same seed always produces the same sequence of numbers,
		 * regardless of other random numbers the game or other scripts use.
		 */
		class jjRNG
		{
		public:
			/** @brief Creates a new instance seeded with the given value */
			jjRNG(std::uint64_t seed);
			~jjRNG();

			/** @brief Creates a generator seeded with the given value */
			static jjRNG* Create(std::uint64_t seed);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			/** @brief Advances the generator and returns the next 64-bit number */
			std::uint64_t operator()();

			/** @brief Copies the state of another generator */
			jjRNG& operator=(const jjRNG& o);

			/** @brief Returns whether both refer to the same generator object, the states aren't compared */
			bool operator==(const jjRNG& o) const;

			/** @brief Resets the generator to the state given by a seed */
			void seed(std::uint64_t value);
			/**
			 * @brief Advances the generator without returning the numbers
			 *
			 * @note Each call of the generator currently advances it by two steps, so `discard(2)` skips one number.
			 */
			void discard(std::uint64_t count);

		private:
			const std::uint64_t DefaultInitSequence = 0xda3e39cb94b95bdbULL;

			std::int32_t _refCount;
			RandomGenerator _random;
		};
	}
}

#endif
