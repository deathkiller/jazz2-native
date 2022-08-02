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
		void Open(FileAccessMode mode) override;
		/// Closes the standard file
		void Close() override;
		long int Seek(long int offset, SeekOrigin origin) const override;
		long int GetPosition() const override;
		unsigned long int Read(void* buffer, unsigned long int bytes) const override;
		unsigned long int Write(void* buffer, unsigned long int bytes) override;

	private:
		/// Deleted copy constructor
		StandardFile(const StandardFile&) = delete;
		/// Deleted assignment operator
		StandardFile& operator=(const StandardFile&) = delete;

#if !(defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW))
		/// Opens the file with `open()`
		void OpenFD(FileAccessMode mode);
#endif
		/// Opens the file with `fopen()`
		void OpenStream(FileAccessMode mode);
	};

}

