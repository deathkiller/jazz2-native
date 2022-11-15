#pragma once

#include "../../Common.h"

#if defined(WITH_OPENMPT)

#include "IAudioReader.h"
#include "../IO/IFileStream.h"

#if defined(_MSC_VER) && defined(__has_include)
#	if __has_include("../../../Libs/Includes/libopenmpt/libopenmpt.h")
#		define __HAS_LOCAL_LIBOPENMPT
#	endif
#endif
#ifdef __HAS_LOCAL_LIBOPENMPT
#	include "../../../Libs/Includes/libopenmpt/libopenmpt.h"
#else
#	include "../../../Libs/Includes/libopenmpt/libopenmpt.h"
#endif

#include <memory>

namespace nCine
{
	class IFile;

	class AudioReaderMpt : public IAudioReader
	{
	public:
		AudioReaderMpt(std::unique_ptr<IFileStream> fileHandle, int frequency);
		~AudioReaderMpt();

		unsigned long int read(void* buffer, unsigned long int bufferSize) const override;
		void rewind() const override;
		void setLooping(bool isLooping) override;

	private:
		/// Audio file handle
		std::unique_ptr<IFileStream> _fileHandle;
		int _frequency;
		openmpt_module* _module;

#if defined(WITH_OPENMPT_DYNAMIC)
		using openmpt_module_create2_t = openmpt_module* (*)(openmpt_stream_callbacks stream_callbacks, void* stream, openmpt_log_func logfunc, void* loguser, openmpt_error_func errfunc, void* erruser, int* error, const char** error_message, const openmpt_module_initial_ctl* ctls);
		using openmpt_module_destroy_t = void (*)(openmpt_module* mod);
		using openmpt_module_read_interleaved_stereo_t = size_t (*)(openmpt_module* mod, int32_t samplerate, size_t count, int16_t* interleaved_stereo);
		using openmpt_module_set_position_seconds_t = double (*)(openmpt_module* mod, double seconds);
		using openmpt_module_set_repeat_count_t = int (*)(openmpt_module* mod, int32_t repeat_count);;

		static void* _openmptLib;
		static bool _openmptLibInitialized;
		static openmpt_module_create2_t _openmpt_module_create2;
		static openmpt_module_destroy_t _openmpt_module_destroy;
		static openmpt_module_read_interleaved_stereo_t _openmpt_module_read_interleaved_stereo;
		static openmpt_module_set_position_seconds_t _openmpt_module_set_position_seconds;
		static openmpt_module_set_repeat_count_t _openmpt_module_set_repeat_count;

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
