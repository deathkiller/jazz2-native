#pragma once

#include "../../Common.h"

#include <cstdio>	// for FILE
#include <cstdint>	// for endianness conversions
#include <memory>

#include <Containers/String.h>

using namespace Death::Containers;

namespace nCine
{
	enum struct FileAccessMode {
		None = 0x00,

#if !(defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW))
		FileDescriptor = 0x01,
#endif
		Read = 0x02,
		Write = 0x04
	};

	DEFINE_ENUM_OPERATORS(FileAccessMode);

	enum class SeekOrigin {
		Begin = SEEK_SET,
		Current = SEEK_CUR,
		End = SEEK_END
	};

	/// The interface class dealing with file operations
	class IFileStream
	{
	public:
		enum class FileType {
			Base = 0,
			Memory,
			Standard,
			Asset
		};

		/// Constructs a base file object
		/*! \param filename File name including its path */
		explicit IFileStream(const String& filename);
		virtual ~IFileStream() {}

		/// Returns the file type (RTTI)
		FileType GetType() const {
			return type_;
		}

		/// Tries to open the file
		virtual void Open(FileAccessMode mode, bool shouldExitOnFailToOpen) = 0;
		/// Closes the file
		virtual void Close() = 0;
		/// Seeks in an opened file
		virtual int32_t Seek(int32_t offset, SeekOrigin origin) const = 0;
		/// Tells the seek position of an opened file
		virtual int32_t GetPosition() const = 0;
		/// Reads a certain amount of bytes from the file to a buffer
		/*! \return Number of bytes read */
		virtual uint32_t Read(void* buffer, uint32_t bytes) const = 0;
		/// Writes a certain amount of bytes from a buffer to the file
		/*! \return Number of bytes written */
		virtual uint32_t Write(const void* buffer, uint32_t bytes) = 0;

		/// Sets the close on destruction flag
		/*! If the flag is true the file is closed upon object destruction. */
		inline void SetCloseOnDestruction(bool shouldCloseOnDestruction) {
			shouldCloseOnDestruction_ = shouldCloseOnDestruction;
		}
		/// Returns true if the file is already opened
		virtual bool IsOpened() const;

		/// Returns file name with path
		const char* GetFilename() const {
			return filename_.data();
		}

		/// Returns file descriptor
		inline int GetFileDescriptor() const {
			return fileDescriptor_;
		}
		/// Returns file stream pointer
		inline FILE* Ptr() const {
			return filePointer_;
		}
		/// Returns file size in bytes
		inline long int GetSize() const {
			return fileSize_;
		}

		template<typename T>
		inline T ReadValue() {
			T buffer;
			Read(&buffer, sizeof(T));
			return buffer;
		}

		template<typename T>
		inline void WriteValue(const T& value) {
			Write(&value, sizeof(T));
		}

		/// Reads a little endian 16 bit unsigned integer
		inline static uint16_t int16FromLE(uint16_t number) {
			return number;
		}
		/// Reads a little endian 32 bit unsigned integer
		inline static uint32_t int32FromLE(uint32_t number) {
			return number;
		}
		/// Reads a little endian 64 bit unsigned integer
		inline static uint64_t int64FromLE(uint64_t number) {
			return number;
		}
		/// Reads a big endian 16 bit unsigned integer
		inline static uint16_t int16FromBE(uint16_t number)
		{
			return (number >> 8) | (number << 8);
		}
		/// Reads a big endian 32 bit unsigned integer
		inline static uint32_t int32FromBE(uint32_t number)
		{
			return (number >> 24) | ((number << 8) & 0x00FF0000) | ((number >> 8) & 0x0000FF00) | (number << 24);
		}
		/// Reads a big endian 64 bit unsigned integer
		inline static uint64_t int64FromBE(uint64_t number)
		{
			return (number >> 56) | ((number << 40) & 0x00FF000000000000ULL) | ((number << 24) & 0x0000FF0000000000ULL) |
				((number << 8) & 0x000000FF00000000ULL) | ((number >> 8) & 0x00000000FF000000ULL) |
				((number >> 24) & 0x0000000000FF0000ULL) | ((number >> 40) & 0x000000000000FF00ULL) | (number << 56);
		}

	protected:
		/// File type
		FileType type_;

		/// Maximum number of characters for a file name (path included)
		static const unsigned int MaxFilenameLength = 256;
		/// File name with path
		String filename_;

		/// File descriptor for `open()` and `close()`
		int fileDescriptor_;
		/// File pointer for `fopen()` and `fclose()`
		FILE* filePointer_;
		/// A flag indicating whether the destructor should also close the file
		/*! Useful for `ov_open()`/`ov_fopen()` and `ov_clear()` functions of the <em>Vorbisfile</em> library. */
		bool shouldCloseOnDestruction_;

		/// File size in bytes
		unsigned long int fileSize_;
	};

}

