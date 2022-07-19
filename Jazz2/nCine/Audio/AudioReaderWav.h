#pragma once

#include "IAudioReader.h"
#include "../IO/IFileStream.h"

#include <memory>

namespace nCine
{
	class IFile;

	/// WAVE audio reader
	class AudioReaderWav : public IAudioReader
	{
	public:
		AudioReaderWav(std::unique_ptr<IFileStream> fileHandle);

		unsigned long int read(void* buffer, unsigned long int bufferSize) const override;
		void rewind() const override;

	private:
		/// Audio file handle
		std::unique_ptr<IFileStream> fileHandle_;

		/// Deleted copy constructor
		AudioReaderWav(const AudioReaderWav&) = delete;
		/// Deleted assignment operator
		AudioReaderWav& operator=(const AudioReaderWav&) = delete;
	};

}
