#pragma once

#include "../../Common.h"

#if defined(WITH_OPENMPT)

#include "IAudioReader.h"

#if !defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("libopenmpt/libopenmpt.h")
#		define __HAS_LOCAL_LIBOPENMPT
#	endif
#endif
#if defined(__HAS_LOCAL_LIBOPENMPT)
#	include "libopenmpt/libopenmpt.h"
#else
#	include <libopenmpt.h>
#endif

#include <memory>

#include <IO/Stream.h>

namespace nCine
{
	class IFile;

	class AudioReaderMpt : public IAudioReader
	{
	public:
		AudioReaderMpt(std::unique_ptr<Death::IO::Stream> fileHandle, int frequency);
		~AudioReaderMpt();

		unsigned long int read(void* buffer, unsigned long int bufferSize) const override;
		void rewind() const override;
		void setLooping(bool isLooping) override;

	private:
		/// Audio file handle
		std::unique_ptr<Death::IO::Stream> _fileHandle;
		int _frequency;
		openmpt_module* _module;

#if defined(WITH_OPENMPT_DYNAMIC)
		using openmpt_module_create2_t = decltype(openmpt_module_create2);
		using openmpt_module_destroy_t = decltype(openmpt_module_destroy);
		using openmpt_module_read_interleaved_stereo_t = decltype(openmpt_module_read_interleaved_stereo);
		using openmpt_module_set_position_seconds_t = decltype(openmpt_module_set_position_seconds);
		using openmpt_module_set_repeat_count_t = decltype(openmpt_module_set_repeat_count);

		static void* _openmptLib;
		static bool _openmptLibInitialized;
		static openmpt_module_create2_t* _openmpt_module_create2;
		static openmpt_module_destroy_t* _openmpt_module_destroy;
		static openmpt_module_read_interleaved_stereo_t* _openmpt_module_read_interleaved_stereo;
		static openmpt_module_set_position_seconds_t* _openmpt_module_set_position_seconds;
		static openmpt_module_set_repeat_count_t* _openmpt_module_set_repeat_count;

		static bool TryLoadLibrary();
#endif

		static size_t stream_read_func(void* stream, void* dst, size_t bytes);
		static int stream_seek_func(void* stream, int64_t offset, int whence);
		static int64_t stream_tell_func(void* stream);

		static void InternalLog(const char* message, void* user);

		/// Deleted copy constructor
		AudioReaderMpt(const AudioReaderMpt&) = delete;
		/// Deleted assignment operator
		AudioReaderMpt& operator=(const AudioReaderMpt&) = delete;
	};
}

#endif