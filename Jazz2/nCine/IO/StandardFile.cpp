#include <cstdlib> // for exit()

// All but MSVC: Linux, Android and MinGW.
#if !(defined(_WIN32) && !defined(__MINGW32__))
#	include <sys/stat.h> // for open()
#	include <fcntl.h> // for open()
#	include <unistd.h> // for close()
#else
#	include <io.h> // for _access()
#endif

#include "StandardFile.h"

namespace nCine {

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	StandardFile::~StandardFile()
	{
		if (shouldCloseOnDestruction_) {
			Close();
		}
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void StandardFile::Open(FileAccessMode mode)
	{
		// Checking if the file is already opened
		if (fileDescriptor_ >= 0 || filePointer_ != nullptr) {
			LOGW_X("File \"%s\" is already opened", filename_.data());
		} else {
#if !(defined(_WIN32) && !defined(__MINGW32__))
			if ((mode & FileAccessMode::FileDescriptor) == FileAccessMode::FileDescriptor) {
				// Opening with a file descriptor
				OpenFD(mode);
			} else
#endif
			{
				// Opening with a file stream
				OpenStream(mode);
			}
		}
	}

	/*! This method will close a file both normally opened or fopened */
	void StandardFile::Close()
	{
		if (fileDescriptor_ >= 0) {
#if !(defined(_WIN32) && !defined(__MINGW32__))
			const int retValue = ::close(fileDescriptor_);
			if (retValue < 0) {
				LOGW_X("Cannot close the file \"%s\"", filename_.data());
			} else {
				LOGI_X("File \"%s\" closed", filename_.data());
				fileDescriptor_ = -1;
			}
#endif
		} else if (filePointer_) {
			const int retValue = fclose(filePointer_);
			if (retValue == EOF) {
				LOGW_X("Cannot close the file \"%s\"", filename_.data());
			} else {
				LOGI_X("File \"%s\" closed", filename_.data());
				filePointer_ = nullptr;
			}
		}
	}

	long int StandardFile::Seek(long int offset, SeekOrigin origin) const
	{
		long int seekValue = -1;

		if (fileDescriptor_ >= 0) {
#if !(defined(_WIN32) && !defined(__MINGW32__))
			seekValue = lseek(fileDescriptor_, offset, (int)origin);
#endif
		} else if (filePointer_) {
			seekValue = fseek(filePointer_, offset, (int)origin);
		}
		return seekValue;
	}

	long int StandardFile::GetPosition() const
	{
		long int tellValue = -1;

		if (fileDescriptor_ >= 0) {
#if !(defined(_WIN32) && !defined(__MINGW32__))
			tellValue = lseek(fileDescriptor_, 0L, SEEK_CUR);
#endif
		} else if (filePointer_) {
			tellValue = ftell(filePointer_);
		}
		return tellValue;
	}

	unsigned long int StandardFile::Read(void* buffer, unsigned long int bytes) const
	{
		//ASSERT(buffer);

		unsigned long int bytesRead = 0;

		if (fileDescriptor_ >= 0) {
#if !(defined(_WIN32) && !defined(__MINGW32__))
			bytesRead = ::read(fileDescriptor_, buffer, bytes);
#endif
		} else if (filePointer_) {
			bytesRead = static_cast<unsigned long int>(fread(buffer, 1, bytes, filePointer_));
		}
		return bytesRead;
	}

	unsigned long int StandardFile::Write(void* buffer, unsigned long int bytes)
	{
		//ASSERT(buffer);

		unsigned long int bytesWritten = 0;

		if (fileDescriptor_ >= 0) {
#if !(defined(_WIN32) && !defined(__MINGW32__))
			bytesWritten = ::write(fileDescriptor_, buffer, bytes);
#endif
		} else if (filePointer_) {
			bytesWritten = static_cast<unsigned long int>(fwrite(buffer, 1, bytes, filePointer_));
		}
		return bytesWritten;
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////
#if !(defined(_WIN32) && !defined(__MINGW32__))
	void StandardFile::OpenFD(FileAccessMode mode)
	{
		int openFlag = -1;

		switch (mode) {
			case (FileAccessMode::FileDescriptor | FileAccessMode::Read):
				openFlag = O_RDONLY;
				break;
			case (FileAccessMode::FileDescriptor | FileAccessMode::Write):
				openFlag = O_WRONLY;
				break;
			case (FileAccessMode::FileDescriptor | FileAccessMode::Read | FileAccessMode::Write):
				openFlag = O_RDWR;
				break;
			default:
				LOGE_X("Cannot open the file \"%s\", wrong open mode", filename_.data());
				break;
		}

		if (openFlag >= 0) {
			fileDescriptor_ = ::open(filename_.data(), openFlag);

			if (fileDescriptor_ < 0) {
				if (shouldExitOnFailToOpen_) {
					LOGF_X("Cannot open the file \"%s\"", filename_.data());
					exit(EXIT_FAILURE);
				} else {
					LOGE_X("Cannot open the file \"%s\"", filename_.data());
					return;
				}
			} else {
				LOGI_X("File \"%s\" opened", filename_.data());
			}
			// Calculating file size
			fileSize_ = lseek(fileDescriptor_, 0L, SEEK_END);
			lseek(fileDescriptor_, 0L, SEEK_SET);
		}
	}
#endif

	void StandardFile::OpenStream(FileAccessMode mode)
	{
		char modeChars[3] = { '\0', '\0', '\0' };

		switch (mode) {
			case FileAccessMode::Read:
				modeChars[0] = 'r';
				modeChars[1] = 'b';
				break;
			case FileAccessMode::Write:
				modeChars[0] = 'w';
				modeChars[1] = 'b';
				break;
			case FileAccessMode::Read | FileAccessMode::Write:
				modeChars[0] = 'r';
				modeChars[1] = '+';
				modeChars[2] = 'b';
				break;
			default:
				LOGE_X("Cannot open the file \"%s\", wrong open mode", filename_.data());
				break;
		}

		if (modeChars[0] != '\0') {
#if defined(_WIN32) && !defined(__MINGW32__)
			fopen_s(&filePointer_, filename_.data(), modeChars);
#else
			filePointer_ = fopen(filename_.data(), modeChars);
#endif

			if (filePointer_ == nullptr) {
				if (shouldExitOnFailToOpen_) {
					LOGF_X("Cannot open the file \"%s\"", filename_.data());
					exit(EXIT_FAILURE);
				} else {
					LOGE_X("Cannot open the file \"%s\"", filename_.data());
					return;
				}
			} else {
				LOGI_X("File \"%s\" opened", filename_.data());
			}

			// Calculating file size
			fseek(filePointer_, 0L, SEEK_END);
			fileSize_ = ftell(filePointer_);
			fseek(filePointer_, 0L, SEEK_SET);
		}
	}

}
