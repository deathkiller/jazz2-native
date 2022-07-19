#if defined(_WIN32) && !defined(CMAKE_BUILD)
#   if defined(_M_X64)
#       pragma comment(lib, "../Libs/x64/libopenmpt.lib")
#   elif defined(_M_IX86)
#       pragma comment(lib, "../Libs/x86/libopenmpt.lib")
#   else
#       error Unsupported architecture
#   endif
#endif

#include "AudioReaderMpt.h"
#include "../IO/IFileStream.h"

#include <cstring>

namespace nCine
{
	AudioReaderMpt::AudioReaderMpt(std::unique_ptr<IFileStream> fileHandle)
		: _fileHandle(std::move(fileHandle)), _module(nullptr)
	{
		_fileHandle->Open(FileAccessMode::Read);
		//ASSERT(fileHandle_->isOpened());

		openmpt_stream_callbacks stream_callbacks;
		stream_callbacks.read = stream_read_func;
		stream_callbacks.seek = stream_seek_func;
		stream_callbacks.tell = stream_tell_func;
		_module = openmpt_module_create2(stream_callbacks, this, LibraryLog, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
	}

	AudioReaderMpt::~AudioReaderMpt()
	{
		if (_module != nullptr) {
			openmpt_module_destroy(_module);
			_module = nullptr;
		}
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	unsigned long int AudioReaderMpt::read(void* buffer, unsigned long int bufferSize) const
	{
		if (_module == nullptr) {
			return 0;
		}

		int count = openmpt_module_read_interleaved_stereo(
			_module,
			SampleRate,
			bufferSize / (2 * sizeof(int16_t)), // Buffer size per channel
			(int16_t*)buffer
		);

		// Number of frames * channel count * size of sample
		return count * 2 * sizeof(int16_t);
	}

	void AudioReaderMpt::rewind() const
	{
		if (_module != nullptr) {
			openmpt_module_set_position_seconds(_module, 0.0);
		}
	}

	void AudioReaderMpt::setLooping(bool isLooping)
	{
		if (_module != nullptr) {
			// Turn on infinite repeat if required
			openmpt_module_set_repeat_count(_module, isLooping ? -1 : 0);
		}
	}

	size_t AudioReaderMpt::stream_read_func(void* stream, void* dst, size_t bytes)
	{
		AudioReaderMpt* _this = reinterpret_cast<AudioReaderMpt*>(stream);
		return _this->_fileHandle->Read(dst, bytes);
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

		AudioReaderMpt* _this = reinterpret_cast<AudioReaderMpt*>(stream);
		_this->_fileHandle->Seek(offset, origin);
		return 0;
	}

	int64_t AudioReaderMpt::stream_tell_func(void* stream)
	{
		AudioReaderMpt* _this = reinterpret_cast<AudioReaderMpt*>(stream);
		return _this->_fileHandle->GetPosition();
	}

	void AudioReaderMpt::LibraryLog(const char* message, void* user)
	{
		LOGI_X("%s", message);
	}
}
