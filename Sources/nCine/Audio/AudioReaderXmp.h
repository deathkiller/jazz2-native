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
		
		Lightweight alternative to @ref AudioReaderMpt for slow platforms (e.g. Amiga). The whole module
		is loaded into memory, so rendering needs no file I/O.
	*/
	class AudioReaderXmp : public IAudioReader
	{
	public:
		AudioReaderXmp(std::unique_ptr<Death::IO::Stream> fileHandle, std::int32_t frequency);
		~AudioReaderXmp();

		AudioReaderXmp(const AudioReaderXmp&) = delete;
		AudioReaderXmp& operator=(const AudioReaderXmp&) = delete;

		std::int32_t Read(void* buffer, std::int32_t bufferSize) const override;
		void Rewind() const override;
		void SetLooping(bool value) override;

	private:
		std::int32_t _frequency;
		bool _looping;
		/** @brief libxmp context (an opaque `char*` in the xmp API), `nullptr` when the module failed to load */
		void* _context;
		bool _playerStarted;
	};
}

#endif
