#pragma once

#include "../../Main.h"

#if defined(WITH_XMP) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IAudioReader.h"

#include <memory>

#include <Containers/Array.h>
#include <IO/Stream.h>

struct xmp_context_wrapper;

namespace nCine
{
	/**
		@brief Audio reader for tracker module formats, backed by `libxmp`

		The lightweight alternative to @ref AudioReaderMpt for platforms where libopenmpt is not
		real-time-viable (the classic Amiga above all - where a tracker player is also the native
		idiom). libxmp covers everything the game's soundtrack ships: `.it`, `.mod`, `.s3m`, `.xm`
		and, through its Galaxy Music System loaders, the original `.j2b` modules.

		Unlike libopenmpt's pull-from-stream interface, libxmp wants the whole file up front, so the
		module (at most a few hundred KB) is read into memory once at load; that also keeps the
		per-frame render free of file I/O, which matters on the slow-storage machines this exists for.
	*/
	class AudioReaderXmp : public IAudioReader
	{
	public:
		AudioReaderXmp(std::unique_ptr<Death::IO::Stream> fileHandle, std::int32_t frequency);
		~AudioReaderXmp();

		AudioReaderXmp(const AudioReaderXmp&) = delete;
		AudioReaderXmp& operator=(const AudioReaderXmp&) = delete;

		std::int32_t read(void* buffer, std::int32_t bufferSize) const override;
		void rewind() const override;
		void setLooping(bool value) override;

	private:
		std::int32_t _frequency;
		bool _looping;
		/** @brief libxmp context (an opaque `char*` in the xmp API), `nullptr` when the module failed to load */
		void* _context;
		bool _playerStarted;
	};
}

#endif
