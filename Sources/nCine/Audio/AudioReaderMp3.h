#pragma once

#include "../../Main.h"

#if defined(WITH_MPG123) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IAudioReader.h"

#include <cstddef>
#include <memory>

#ifndef DOXYGEN_GENERATING_OUTPUT
#	if defined(WITH_MPG123_DYNAMIC)
// The library is loaded at runtime, so only the few declarations used are needed instead of <mpg123.h>
struct mpg123_handle_struct;
typedef struct mpg123_handle_struct mpg123_handle;
#	else
#		include <mpg123.h>
#	endif
#endif

#if defined(WITH_MPG123_DYNAMIC) && defined(DEATH_TARGET_WINDOWS)
#	include <CommonWindows.h>
#endif

#include <IO/Stream.h>

namespace nCine
{
	class AudioLoaderMp3;

	/**
		@brief Audio reader for the MPEG audio format (MP3), backed by `libmpg123`
	*/
	class AudioReaderMp3 : public IAudioReader
	{
		friend class AudioLoaderMp3;

	public:
		AudioReaderMp3(std::unique_ptr<Death::IO::Stream> fileHandle, mpg123_handle* handle);
		~AudioReaderMp3() override;

		AudioReaderMp3(const AudioReaderMp3&) = delete;
		AudioReaderMp3& operator=(const AudioReaderMp3&) = delete;

		std::int32_t Read(void* buffer, std::int32_t bufferSize) const override;
		void Rewind() const override;

	private:
		/** @brief Audio file handle */
		std::unique_ptr<Death::IO::Stream> _fileHandle;
		/** @brief Decoder handle */
		mpg123_handle* _handle;

		// Opens a decoder for the stream with 16-bit signed output, `nullptr` on failure
		static mpg123_handle* OpenDecoder(Death::IO::Stream* stream, std::int32_t& frequency, std::int32_t& channels, std::int64_t& samples);
		// Closes and deletes a decoder opened by OpenDecoder()
		static void CloseDecoder(mpg123_handle* handle);

#if defined(WITH_MPG123_DYNAMIC)
		using mpg123_init_t = int();
		using mpg123_new_t = mpg123_handle*(const char*, int*);
		using mpg123_delete_t = void(mpg123_handle*);
		using mpg123_param_t = int(mpg123_handle*, int, long, double);
		using mpg123_reader64_t = int(mpg123_handle*, int(*)(void*, void*, std::size_t, std::size_t*), std::int64_t(*)(void*, std::int64_t, int), void(*)(void*));
		using mpg123_open_handle64_t = int(mpg123_handle*, void*);
		using mpg123_getformat_t = int(mpg123_handle*, long*, int*, int*);
		using mpg123_format_none_t = int(mpg123_handle*);
		using mpg123_format_t = int(mpg123_handle*, long, int, int);
		using mpg123_length64_t = std::int64_t(mpg123_handle*);
		using mpg123_read_t = int(mpg123_handle*, void*, std::size_t, std::size_t*);
		using mpg123_seek64_t = std::int64_t(mpg123_handle*, std::int64_t, int);
		using mpg123_close_t = int(mpg123_handle*);

		static HMODULE _mpgLib;
		static bool _mpgLibInitialized;
		static mpg123_init_t* _mpg123_init;
		static mpg123_new_t* _mpg123_new;
		static mpg123_delete_t* _mpg123_delete;
		static mpg123_param_t* _mpg123_param;
		static mpg123_reader64_t* _mpg123_reader64;
		static mpg123_open_handle64_t* _mpg123_open_handle64;
		static mpg123_getformat_t* _mpg123_getformat;
		static mpg123_format_none_t* _mpg123_format_none;
		static mpg123_format_t* _mpg123_format;
		static mpg123_length64_t* _mpg123_length64;
		static mpg123_read_t* _mpg123_read;
		static mpg123_seek64_t* _mpg123_seek64;
		static mpg123_close_t* _mpg123_close;
#endif

		static bool TryLoadLibrary();
	};
}

#endif
