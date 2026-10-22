#pragma once

#include "IAudioReader.h"

#include <memory>

#include <IO/Stream.h>

namespace nCine
{
	class IFile;

	/**
		@brief Audio reader for the WAVE (`.wav`) format
	*/
	class AudioReaderWav : public IAudioReader
	{
	public:
		/**
		 * @param bytesPerSample	Needed to byte-swap the samples on big-endian targets
		 * @param dataOffset		Where the samples begin in the file, the stream has to be there already
		 * @param dataSize			Size of the samples in bytes, the file can have other chunks after them
		 */
		AudioReaderWav(std::unique_ptr<Death::IO::Stream> fileHandle, std::int32_t bytesPerSample, std::int64_t dataOffset, std::int64_t dataSize);

		AudioReaderWav(const AudioReaderWav&) = delete;
		AudioReaderWav& operator=(const AudioReaderWav&) = delete;

		std::int32_t read(void* buffer, std::int32_t bufferSize) const override;
		void rewind() const override;

	private:
		std::unique_ptr<Death::IO::Stream> _fileHandle;
		std::int32_t _bytesPerSample;
		std::int64_t _dataOffset;
		std::int64_t _dataSize;
	};
}
