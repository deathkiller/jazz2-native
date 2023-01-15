#pragma once

#include "../../Common.h"

#if defined(WITH_VORBIS)

#include "IAudioReader.h"
#include "../IO/IFileStream.h"

#define OV_EXCLUDE_STATIC_CALLBACKS

#if defined(_MSC_VER) && defined(__has_include)
#	if __has_include("../../../Libs/Includes/ogg/ogg.h")
#		define __HAS_LOCAL_OGG
#	endif
#endif
#if defined(__HAS_LOCAL_OGG)
#	include "../../../Libs/Includes/ogg/ogg.h"
#else
#	include <ogg/ogg.h>
#endif

#if defined(_MSC_VER) && defined(__has_include)
#	if __has_include("../../../Libs/Includes/vorbis/vorbisfile.h")
#		define __HAS_LOCAL_VORBIS
#	endif
#endif
#if defined(__HAS_LOCAL_VORBIS)
#	include "../../../Libs/Includes/vorbis/vorbisfile.h"
#else
#	include <vorbis/vorbisfile.h>
#endif

namespace nCine
{
	/// Ogg Vorbis audio reader
	class AudioReaderOgg : public IAudioReader
	{
	public:
		AudioReaderOgg(std::unique_ptr<IFileStream> fileHandle, const OggVorbis_File& oggFile);
		~AudioReaderOgg() override;

		unsigned long int read(void* buffer, unsigned long int bufferSize) const override;
		void rewind() const override;

	private:
		/// Audio file handle
		std::unique_ptr<IFileStream> fileHandle_;
		/// Vorbisfile handle
		mutable OggVorbis_File oggFile_;

		/// Deleted copy constructor
		AudioReaderOgg(const AudioReaderOgg&) = delete;
		/// Deleted assignment operator
		AudioReaderOgg& operator=(const AudioReaderOgg&) = delete;
	};
}

#endif
