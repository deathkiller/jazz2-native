#pragma once

#include "../../Common.h"

#include <cstdio> // for FILE
#include <cstdint> // for endianness conversions
#include <string>
#include <memory>

namespace nCine
{
	enum struct FileAccessMode {
#if !(defined(_WIN32) && !defined(__MINGW32__))
		FileDescriptor = 1,
#endif
		Read = 2,
		Write = 4
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
		explicit IFileStream(const char* filename);
		virtual ~IFileStream() {}

		/// Returns the file type (RTTI)
		FileType type() const {
			return type_;
		}

		/// Tries to open the file
		virtual void Open(FileAccessMode mode) = 0;
		/// Closes the file
		virtual void Close() = 0;
		/// Seeks in an opened file
		virtual long int Seek(long int offset, SeekOrigin origin) const = 0;
		/// Tells the seek position of an opened file
		virtual long int GetPosition() const = 0;
		/// Reads a certain amount of bytes from the file to a buffer
		/*! \return Number of bytes read */
		virtual unsigned long int Read(void* buffer, unsigned long int bytes) const = 0;
		/// Writes a certain amount of bytes from a buffer to the file
		/*! \return Number of bytes written */
		virtual unsigned long int Write(void* buffer, unsigned long int bytes) = 0;

		/// Sets the close on destruction flag
		/*! If the flag is true the file is closed upon object destruction. */
		inline void setCloseOnDestruction(bool shouldCloseOnDestruction) {
			shouldCloseOnDestruction_ = shouldCloseOnDestruction;
		}
		/// Sets the exit on fail to open flag
		/*! If the flag is true the application exits if the file cannot be opened. */
		inline void setExitOnFailToOpen(bool shouldExitOnFailToOpen) {
			shouldExitOnFailToOpen_ = shouldExitOnFailToOpen;
		}
		/// Returns true if the file is already opened
		virtual bool isOpened() const;

		/// Returns file name with path
		const char* filename() const {
			return filename_.data();
		}

		/// Returns file descriptor
		inline int fd() const {
			return fileDescriptor_;
		}
		/// Returns file stream pointer
		inline FILE* ptr() const {
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

		/// Returns a memory file with the specified name
		static std::unique_ptr<IFileStream> createFromMemory(const char* bufferName, unsigned char* bufferPtr, unsigned long int bufferSize);
		/// Returns a read-only memory file with the specified name
		static std::unique_ptr<IFileStream> createFromMemory(const char* bufferName, const unsigned char* bufferPtr, unsigned long int bufferSize);
		/// Returns a memory file
		static std::unique_ptr<IFileStream> createFromMemory(unsigned char* bufferPtr, unsigned long int bufferSize);
		/// Returns a read-only memory file
		static std::unique_ptr<IFileStream> createFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize);

		/// Returns the proper file handle according to prepended tags
		static std::unique_ptr<IFileStream> createFileHandle(const char* filename);

	protected:
		/// File type
		FileType type_;

		/// Maximum number of characters for a file name (path included)
		static const unsigned int MaxFilenameLength = 256;
		/// File name with path
		std::string filename_;

		/// File descriptor for `open()` and `close()`
		int fileDescriptor_;
		/// File pointer for `fopen()` and `fclose()`
		FILE* filePointer_;
		/// A flag indicating whether the destructor should also close the file
		/*! Useful for `ov_open()`/`ov_fopen()` and `ov_clear()` functions of the <em>Vorbisfile</em> library. */
		bool shouldCloseOnDestruction_;
		/// A flag indicating whether the application should exit if the file cannot be opened
		/*! Useful for the log file creation. */
		bool shouldExitOnFailToOpen_;

		/// File size in bytes
		unsigned long int fileSize_;

	private:
		/// The `TextureSaverPng` class needs to access the `filePointer_`
		//friend class TextureSaverPng;
	};

}

