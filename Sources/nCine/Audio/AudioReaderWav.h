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
		AudioReaderWav(std::unique_ptr<Death::IO::Stream> fileHandle);

		AudioReaderWav(const AudioReaderWav&) = delete;
		AudioReaderWav& operator=(const AudioReaderWav&) = delete;

		std::int32_t read(void* buffer, std::int32_t bufferSize) const override;
		void rewind() const override;

	private:
		std::unique_ptr<Death::IO::Stream> fileHandle_;
	};
}
