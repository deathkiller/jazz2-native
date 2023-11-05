#include "AudioReaderMpt.h"

#if defined(WITH_OPENMPT)

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD) && !defined(WITH_OPENMPT_DYNAMIC)
#	if defined(_M_X64)
#		pragma comment(lib, "../Libs/Windows/x64/libopenmpt.lib")
#	elif defined(_M_IX86)
#		pragma comment(lib, "../Libs/Windows/x86/libopenmpt.lib")
#	else
#		error Unsupported architecture
#	endif
#endif

#if defined(WITH_OPENMPT_DYNAMIC) && (defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_APPLE))
#	include <dlfcn.h>
#endif

#include <cstring>

using namespace Death::IO;

namespace nCine
{
#if defined(WITH_OPENMPT_DYNAMIC)
	void* AudioReaderMpt::_openmptLib = nullptr;
	bool AudioReaderMpt::_openmptLibInitialized = false;
	AudioReaderMpt::openmpt_module_create2_t* AudioReaderMpt::_openmpt_module_create2 = nullptr;
	AudioReaderMpt::openmpt_module_destroy_t* AudioReaderMpt::_openmpt_module_destroy = nullptr;
	AudioReaderMpt::openmpt_module_read_interleaved_stereo_t* AudioReaderMpt::_openmpt_module_read_interleaved_stereo = nullptr;
	AudioReaderMpt::openmpt_module_set_position_seconds_t* AudioReaderMpt::_openmpt_module_set_position_seconds = nullptr;
	AudioReaderMpt::openmpt_module_set_repeat_count_t* AudioReaderMpt::_openmpt_module_set_repeat_count = nullptr;
#endif

	AudioReaderMpt::AudioReaderMpt(std::unique_ptr<Stream> fileHandle, int frequency)
		: _fileHandle(std::move(fileHandle)), _frequency(frequency), _module(nullptr)
	{
		ASSERT(_fileHandle->IsValid());

#if defined(WITH_OPENMPT_DYNAMIC)
		if (TryLoadLibrary()) {
			openmpt_stream_callbacks stream_callbacks;
			stream_callbacks.read = stream_read_func;
			stream_callbacks.seek = stream_seek_func;
			stream_callbacks.tell = stream_tell_func;
			_module = _openmpt_module_create2(stream_callbacks, this, InternalLog, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		}
#else
		openmpt_stream_callbacks stream_callbacks;
		stream_callbacks.read = stream_read_func;
		stream_callbacks.seek = stream_seek_func;
		stream_callbacks.tell = stream_tell_func;
		_module = openmpt_module_create2(stream_callbacks, this, InternalLog, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
#endif
	}

	AudioReaderMpt::~AudioReaderMpt()
	{
		if (_module != nullptr) {
#if defined(WITH_OPENMPT_DYNAMIC)
			_openmpt_module_destroy(_module);
#else
			openmpt_module_destroy(_module);
#endif
			_module = nullptr;
		}
	}

	unsigned long int AudioReaderMpt::read(void* buffer, unsigned long int bufferSize) const
	{
		if (_module == nullptr) {
			return 0;
		}

#if defined(WITH_OPENMPT_DYNAMIC)
		int count = _openmpt_module_read_interleaved_stereo(
			_module, _frequency,
			bufferSize / (2 * sizeof(int16_t)), // Buffer size per channel
			(int16_t*)buffer
		);
#else
		size_t count = openmpt_module_read_interleaved_stereo(
			_module, _frequency,
			bufferSize / (2 * sizeof(int16_t)), // Buffer size per channel
			(int16_t*)buffer
		);
#endif
		// Number of frames * channel count * size of sample
		return count * 2 * sizeof(int16_t);
	}

	void AudioReaderMpt::rewind() const
	{
		if (_module != nullptr) {
#if defined(WITH_OPENMPT_DYNAMIC)
			_openmpt_module_set_position_seconds(_module, 0.0);
#else
			openmpt_module_set_position_seconds(_module, 0.0);
#endif
		}
	}

	void AudioReaderMpt::setLooping(bool isLooping)
	{
		if (_module != nullptr) {
			// Turn on infinite repeat if required
#if defined(WITH_OPENMPT_DYNAMIC)
			_openmpt_module_set_repeat_count(_module, isLooping ? -1 : 0);
#else
			openmpt_module_set_repeat_count(_module, isLooping ? -1 : 0);
#endif
		}
	}

#if defined(WITH_OPENMPT_DYNAMIC)
	bool AudioReaderMpt::TryLoadLibrary()
	{
		if (_openmptLibInitialized) {
			return (_openmptLib != nullptr);
		}

		_openmptLibInitialized = true;
		_openmptLib = ::dlopen("libopenmpt.so", RTLD_LAZY);
		if (_openmptLib == nullptr) {
			return false;
		}

		_openmpt_module_create2 = (openmpt_module_create2_t*)::dlsym(_openmptLib, "openmpt_module_create2");
		_openmpt_module_destroy = (openmpt_module_destroy_t*)::dlsym(_openmptLib, "openmpt_module_destroy");
		_openmpt_module_read_interleaved_stereo = (openmpt_module_read_interleaved_stereo_t*)::dlsym(_openmptLib, "openmpt_module_read_interleaved_stereo");
		_openmpt_module_set_position_seconds = (openmpt_module_set_position_seconds_t*)::dlsym(_openmptLib, "openmpt_module_set_position_seconds");
		_openmpt_module_set_repeat_count = (openmpt_module_set_repeat_count_t*)::dlsym(_openmptLib, "openmpt_module_set_repeat_count");

		if (_openmpt_module_create2 == nullptr || _openmpt_module_destroy == nullptr || _openmpt_module_read_interleaved_stereo == nullptr ||
			_openmpt_module_set_position_seconds == nullptr || _openmpt_module_set_repeat_count == nullptr) {
			::dlclose(_openmptLib);
			_openmptLib = nullptr;
			return false;
		}

		return true;
	}
#endif

	size_t AudioReaderMpt::stream_read_func(void* stream, void* dst, size_t bytes)
	{
		AudioReaderMpt* _this = static_cast<AudioReaderMpt*>(stream);
		return _this->_fileHandle->Read(dst, (unsigned long)bytes);
	}

	int AudioReaderMpt::stream_seek_func(void* stream, int64_t offset, int whence)
	{
		SeekOrigin origin;
		switch (whence) {
			default:
			case OPENMPT_STREAM_SEEK_SET: origin = SeekOrigin::Begin; break;
			case OPENMPT_STREAM_SEEK_CUR: origin = SeekOrigin::Current; break;
			case OPENMPT_STREAM_SEEK_END: origin = SeekOrigin::End; break;
		}

		AudioReaderMpt* _this = static_cast<AudioReaderMpt*>(stream);
		_this->_fileHandle->Seek((int32_t)offset, origin);
		return 0;
	}

	int64_t AudioReaderMpt::stream_tell_func(void* stream)
	{
		AudioReaderMpt* _this = static_cast<AudioReaderMpt*>(stream);
		return _this->_fileHandle->GetPosition();
	}

	void AudioReaderMpt::InternalLog(const char* message, void* user)
	{
		LOGI("%s", message);
	}
}

#endif