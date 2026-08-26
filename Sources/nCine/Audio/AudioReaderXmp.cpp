#include "AudioReaderXmp.h"

#if defined(WITH_XMP)

#include <cstring>

#if !defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("../../Dependencies/libxmp/include/xmp.h")
#		define __HAS_LOCAL_LIBXMP
#	endif
#endif
#if defined(__HAS_LOCAL_LIBXMP)
#	include "../../Dependencies/libxmp/include/xmp.h"
#else
#	include <xmp.h>
#endif

using namespace Death::IO;

namespace nCine
{
	AudioReaderXmp::AudioReaderXmp(std::unique_ptr<Stream> fileHandle, std::int32_t frequency)
		: _frequency(frequency), _looping(false), _context(nullptr), _playerStarted(false)
	{
		if (!fileHandle->IsValid()) {
			return;
		}

		// libxmp parses from memory, so the whole module is read in once (the stream is not kept)
		const std::int64_t size = fileHandle->GetSize();
		if (size <= 0 || size > INT32_MAX) {
			return;
		}
		Death::Containers::Array<char> data(Death::Containers::NoInit, std::size_t(size));
		fileHandle->Seek(0, SeekOrigin::Begin);
		if (fileHandle->Read(data.data(), std::int64_t(size)) != std::int64_t(size)) {
			return;
		}

		xmp_context context = xmp_create_context();
		if (context == nullptr) {
			return;
		}
		if (xmp_load_module_from_memory(context, data.data(), long(size)) != 0) {
			LOGE("libxmp cannot load the module ({} bytes)", size);
			xmp_free_context(context);
			return;
		}
		if (xmp_start_player(context, _frequency, 0) != 0) {
			LOGE("libxmp cannot start the player at {} Hz", _frequency);
			xmp_release_module(context);
			xmp_free_context(context);
			return;
		}
		_playerStarted = true;
		_context = context;
	}

	AudioReaderXmp::~AudioReaderXmp()
	{
		if (_context != nullptr) {
			xmp_context context = static_cast<xmp_context>(_context);
			if (_playerStarted) {
				xmp_end_player(context);
			}
			xmp_release_module(context);
			xmp_free_context(context);
			_context = nullptr;
		}
	}

	std::int32_t AudioReaderXmp::read(void* buffer, std::int32_t bufferSize) const
	{
		if (_context == nullptr) {
			return 0;
		}
		// Fills the buffer completely with interleaved 16-bit stereo; the loop count 0 means
		// "loop forever", anything else is the pass after which -XMP_END is returned
		const int result = xmp_play_buffer(static_cast<xmp_context>(_context), buffer, int(bufferSize), _looping ? 0 : 1);
		return (result == 0 ? bufferSize : 0);
	}

	void AudioReaderXmp::rewind() const
	{
		if (_context != nullptr) {
			xmp_context context = static_cast<xmp_context>(_context);
			xmp_restart_module(context);
			// Drop what xmp_play_buffer still holds of the previous position
			xmp_play_buffer(context, nullptr, 0, 0);
		}
	}

	void AudioReaderXmp::setLooping(bool value)
	{
		_looping = value;
	}
}

#endif
