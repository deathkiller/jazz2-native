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

#if defined(WITH_VORBIS_DYNAMIC) && defined(DEATH_TARGET_WINDOWS)
#	include <CommonWindows.h>
#endif

namespace nCine
{
	class AudioLoaderOgg;

	/// Ogg Vorbis audio reader
	class AudioReaderOgg : public IAudioReader
	{
		friend class AudioLoaderOgg;

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

#if defined(WITH_VORBIS_DYNAMIC)
		using ov_clear_t = decltype(ov_clear);
		using ov_read_t = decltype(ov_read);
		using ov_raw_seek_t = decltype(ov_raw_seek);
		using ov_info_t = decltype(ov_info);
		using ov_open_callbacks_t = decltype(ov_open_callbacks);
		using ov_pcm_total_t = decltype(ov_pcm_total);
		using ov_time_total_t = decltype(ov_time_total);

		static HMODULE _ovLib;
		static bool _ovLibInitialized;
		static ov_clear_t* _ov_clear;
		static ov_read_t* _ov_read;
		static ov_raw_seek_t* _ov_raw_seek;
		static ov_info_t* _ov_info;
		static ov_open_callbacks_t* _ov_open_callbacks;
		static ov_pcm_total_t* _ov_pcm_total;
		static ov_time_total_t* _ov_time_total;

		static bool TryLoadLibrary();
#endif

		/// Deleted copy constructor
		AudioReaderOgg(const AudioReaderOgg&) = delete;
		/// Deleted assignment operator
		AudioReaderOgg& operator=(const AudioReaderOgg&) = delete;
	};
}

#endif
