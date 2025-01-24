#pragma once

#include "../../Main.h"

#if defined(WITH_OPENMPT) || defined(DOXYGEN_GENERATING_OUTPUT)

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

	/// Module audio reader using `libopenmpt` library
	class AudioReaderMpt : public IAudioReader
	{
	public:
		AudioReaderMpt(std::unique_ptr<Death::IO::Stream> fileHandle, std::int32_t frequency);
		~AudioReaderMpt();

		AudioReaderMpt(const AudioReaderMpt&) = delete;
		AudioReaderMpt& operator=(const AudioReaderMpt&) = delete;

		std::int32_t read(void* buffer, std::int32_t bufferSize) const override;
		void rewind() const override;
		void setLooping(bool value) override;

	private:
		/// Audio file handle
		std::unique_ptr<Death::IO::Stream> _fileHandle;
		std::int32_t _frequency;
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

		static std::size_t stream_read_func(void* stream, void* dst, std::size_t bytes);
		static std::int32_t stream_seek_func(void* stream, std::int64_t offset, std::int32_t whence);
		static std::int64_t stream_tell_func(void* stream);

		static void InternalLog(const char* message, void* user);
	};
}

#endif