#pragma once

#include "IFileStream.h"

namespace nCine
{
	/// The class handling opening, reading and closing a standard file
	class StandardFile : public IFileStream
	{
	public:
		/// Constructs a standard file object
		/*! \param filename File name including its path */
		explicit StandardFile(const String& filename)
			: IFileStream(filename) {
			type_ = FileType::Standard;
		}
		~StandardFile() override;

		/// Tries to open the standard file
		void Open(FileAccessMode mode, bool shouldExitOnFailToOpen) override;
		/// Closes the standard file
		void Close() override;
		int32_t Seek(int32_t offset, SeekOrigin origin) const override;
		int32_t GetPosition() const override;
		uint32_t Read(void* buffer, uint32_t bytes) const override;
		uint32_t Write(const void* buffer, uint32_t bytes) override;

	private:
		/// Deleted copy constructor
		StandardFile(const StandardFile&) = delete;
		/// Deleted assignment operator
		StandardFile& operator=(const StandardFile&) = delete;

#if !(defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW))
		/// Opens the file with `open()`
		void OpenFD(FileAccessMode mode, bool shouldExitOnFailToOpen);
#endif
		/// Opens the file with `fopen()`
		void OpenStream(FileAccessMode mode, bool shouldExitOnFailToOpen);
	};

}

