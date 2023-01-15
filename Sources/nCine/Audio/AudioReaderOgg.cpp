#include "AudioReaderOgg.h"

#if defined(WITH_VORBIS)

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#	if defined(_M_X64)
#		pragma comment(lib, "../Libs/Windows/x64/libvorbisfile.lib")
#	elif defined(_M_IX86)
#		pragma comment(lib, "../Libs/Windows/x86/libvorbisfile.lib")
#	else
#		error Unsupported architecture
#	endif
#endif

#include "../IO/IFileStream.h"

namespace nCine
{
	AudioReaderOgg::AudioReaderOgg(std::unique_ptr<IFileStream> fileHandle, const OggVorbis_File& oggFile)
		: fileHandle_(std::move(fileHandle)), oggFile_(oggFile)
	{
		ASSERT(fileHandle_->IsOpened());
	}

	AudioReaderOgg::~AudioReaderOgg()
	{
		ov_clear(&oggFile_);
	}

	unsigned long int AudioReaderOgg::read(void* buffer, unsigned long int bufferSize) const
	{
		static int bitStream = 0;
		long bytes = 0;
		unsigned long int bufferSeek = 0;

		do {
			// Read up to a buffer's worth of decoded sound data
			// (0: little endian, 2: 16bit, 1: signed)
			bytes = ov_read(&oggFile_, static_cast<char*>(buffer) + bufferSeek, bufferSize - bufferSeek, 0, 2, 1, &bitStream);

			if (bytes < 0) {
				ov_clear(&oggFile_);
				FATAL_MSG_X("Error decoding at bitstream %d", bitStream);
			}

			// Reset the static variable at the end of a decoding process
			if (bytes <= 0) {
				bitStream = 0;
			}
			bufferSeek += bytes;
		} while (bytes > 0 && bufferSize - bufferSeek > 0);

		return bufferSeek;
	}

	void AudioReaderOgg::rewind() const
	{
		ov_raw_seek(&oggFile_, 0);
	}
}

#endif