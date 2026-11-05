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

#include <algorithm>
#include <cstring>

#include <Base/Move.h>

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
	AudioReaderMpt::openmpt_module_set_position_order_row_t* AudioReaderMpt::_openmpt_module_set_position_order_row = nullptr;
	AudioReaderMpt::openmpt_module_get_current_int_t* AudioReaderMpt::_openmpt_module_get_current_order = nullptr;
	AudioReaderMpt::openmpt_module_get_current_int_t* AudioReaderMpt::_openmpt_module_get_current_row = nullptr;
	AudioReaderMpt::openmpt_module_get_current_int_t* AudioReaderMpt::_openmpt_module_get_current_speed = nullptr;
	AudioReaderMpt::openmpt_module_get_current_int_t* AudioReaderMpt::_openmpt_module_get_current_tempo = nullptr;
	AudioReaderMpt::openmpt_module_get_current_tempo2_t* AudioReaderMpt::_openmpt_module_get_current_tempo2 = nullptr;
	AudioReaderMpt::openmpt_module_get_current_int_t* AudioReaderMpt::_openmpt_module_get_num_channels = nullptr;
#	if defined(NCINE_HAS_OPENMPT_EXT)
	AudioReaderMpt::openmpt_module_ext_create_t* AudioReaderMpt::_openmpt_module_ext_create = nullptr;
	AudioReaderMpt::openmpt_module_ext_destroy_t* AudioReaderMpt::_openmpt_module_ext_destroy = nullptr;
	AudioReaderMpt::openmpt_module_ext_get_module_t* AudioReaderMpt::_openmpt_module_ext_get_module = nullptr;
	AudioReaderMpt::openmpt_module_ext_get_interface_t* AudioReaderMpt::_openmpt_module_ext_get_interface = nullptr;
#	endif
#	define MPT_FUNC(name) _##name
#else
#	define MPT_FUNC(name) name
#endif

	// Spelled with the fixed-width type the declaration uses: where `std::int32_t` is `long int` (MIPS,
	// among others) a plain `int` parameter would be a different signature and match nothing
	AudioReaderMpt::AudioReaderMpt(std::unique_ptr<Stream> fileHandle, std::int32_t frequency)
		: _fileHandle(Death::move(fileHandle)), _frequency(frequency), _module(nullptr),
#if defined(NCINE_HAS_OPENMPT_EXT)
			_moduleExt(nullptr), _interactive{}, _hasInteractive(false),
#	if defined(NCINE_HAS_OPENMPT_TEMPO2)
			_interactive3{}, _hasInteractive3(false),
#	endif
#endif
			_decodedFrames(0), _timeline{}, _timelineNext(0), _timelineCount(0), _pendingPosition(false),
			_pendingOrder(0), _pendingRow(0), _pendingSpeed(0), _pendingTempo(0), _recordedOrder(-1), _recordedRow(-1)
	{
		if (!_fileHandle->IsValid()) {
			return;
		}

#if defined(WITH_OPENMPT_DYNAMIC)
		if (!TryLoadLibrary()) {
			return;
		}
#endif

		openmpt_stream_callbacks stream_callbacks;
		stream_callbacks.read = StreamRead;
		stream_callbacks.seek = StreamSeek;
		stream_callbacks.tell = StreamTell;

#if defined(NCINE_HAS_OPENMPT_EXT)
		// The extended module is the one that can change speed, tempo and channel volumes while it plays
#	if defined(WITH_OPENMPT_DYNAMIC)
		bool hasExt = (_openmpt_module_ext_create != nullptr && _openmpt_module_ext_destroy != nullptr &&
			_openmpt_module_ext_get_module != nullptr && _openmpt_module_ext_get_interface != nullptr);
#	else
		bool hasExt = true;
#	endif
		if (hasExt) {
			_moduleExt = MPT_FUNC(openmpt_module_ext_create)(stream_callbacks, this, InternalLog, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
			if (_moduleExt != nullptr) {
				_module = MPT_FUNC(openmpt_module_ext_get_module)(_moduleExt);
				_hasInteractive = (MPT_FUNC(openmpt_module_ext_get_interface)(_moduleExt, LIBOPENMPT_EXT_C_INTERFACE_INTERACTIVE,
					&_interactive, sizeof(_interactive)) != 0);
#	if defined(NCINE_HAS_OPENMPT_TEMPO2)
				_hasInteractive3 = (MPT_FUNC(openmpt_module_ext_get_interface)(_moduleExt, LIBOPENMPT_EXT_C_INTERFACE_INTERACTIVE3,
					&_interactive3, sizeof(_interactive3)) != 0);
#	endif
			} else {
				_fileHandle->Seek(0, SeekOrigin::Begin);
			}
		}
#endif

		if (_module == nullptr) {
			_module = MPT_FUNC(openmpt_module_create2)(stream_callbacks, this, InternalLog, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		}

		if (_module != nullptr) {
			RecordState(0, true);
		}
	}

	AudioReaderMpt::~AudioReaderMpt()
	{
#if defined(NCINE_HAS_OPENMPT_EXT)
		if (_moduleExt != nullptr) {
			// Destroys the module too
			MPT_FUNC(openmpt_module_ext_destroy)(_moduleExt);
			_moduleExt = nullptr;
			_module = nullptr;
		}
#endif
		if (_module != nullptr) {
			MPT_FUNC(openmpt_module_destroy)(_module);
			_module = nullptr;
		}
	}

	std::int32_t AudioReaderMpt::Read(void* buffer, std::int32_t bufferSize) const
	{
		if (_module == nullptr) {
			return 0;
		}

		ApplyPendingChanges();

		// Only this thread writes the count, the lock is for the others
		std::int64_t frame = _decodedFrames;

		// A single call can render less than requested without reaching the end, so keep reading until the buffer
		// is full or the module has ended (returns 0 frames). It's rendered in slices, so the state of the module
		// is known for every part of the buffer.
		std::int16_t* dst = static_cast<std::int16_t*>(buffer);
		std::int32_t totalFrames = 0;
		std::int32_t framesRemaining = std::int32_t(bufferSize / (2 * sizeof(std::int16_t)));
		bool forceRecord = true;
		while (framesRemaining > 0) {
			std::int32_t sliceFrames = std::min(framesRemaining, SliceFrames);
			if (!_activeSlides.empty()) {
				AdvanceSlides(sliceFrames);
			}
			RecordState(frame, forceRecord);
			forceRecord = false;

			std::size_t count = MPT_FUNC(openmpt_module_read_interleaved_stereo)(
				_module, _frequency, sliceFrames, dst + (totalFrames * 2));
			if (count == 0) {
				break;
			}
			totalFrames += std::int32_t(count);
			framesRemaining -= std::int32_t(count);
			frame += std::int64_t(count);
		}

		_lock.lock();
		_decodedFrames = frame;
		_lock.unlock();
		// Number of frames * channel count * size of sample
		return totalFrames * 2 * sizeof(std::int16_t);
	}

	void AudioReaderMpt::Rewind() const
	{
		if (_module != nullptr) {
			MPT_FUNC(openmpt_module_set_position_seconds)(_module, 0.0);
		}
	}

	void AudioReaderMpt::SetLooping(bool value)
	{
		if (_module != nullptr) {
			// Turn on infinite repeat if required
			MPT_FUNC(openmpt_module_set_repeat_count)(_module, value ? -1 : 0);
		}
	}

	std::int64_t AudioReaderMpt::GetDecodedFrames() const
	{
		_lock.lock();
		std::int64_t frames = _decodedFrames;
		_lock.unlock();
		return frames;
	}

	bool AudioReaderMpt::GetModuleStateAt(std::int64_t frame, AudioModuleState& state) const
	{
		if (_module == nullptr) {
			return false;
		}

		_lock.lock();
		// The newest state recorded at or before the frame, or the oldest one kept for a frame before all of them
		std::int32_t index = _timelineNext;
		for (std::int32_t i = 0; i < _timelineCount; i++) {
			index = (index + TimelineSize - 1) % TimelineSize;
			if (_timeline[index].Frame <= frame) {
				break;
			}
		}
		state = _timeline[index].State;
		_lock.unlock();
		return true;
	}

	bool AudioReaderMpt::SetModulePosition(std::int32_t order, std::int32_t row)
	{
		if (!CanSetModulePosition() || order < 0 || row < 0) {
			return false;
		}

		_lock.lock();
		_pendingPosition = true;
		_pendingOrder = order;
		_pendingRow = row;
		_lock.unlock();
		return true;
	}

	bool AudioReaderMpt::SetModuleSpeed(std::int32_t speed)
	{
#if defined(NCINE_HAS_OPENMPT_EXT)
		if (!_hasInteractive || speed <= 0) {
			return false;
		}

		_lock.lock();
		_pendingSpeed = std::min<std::int32_t>(speed, 65535);
		_lock.unlock();
		return true;
#else
		return false;
#endif
	}

	bool AudioReaderMpt::SetModuleTempo(std::int32_t tempo)
	{
#if defined(NCINE_HAS_OPENMPT_TEMPO2)
		if (!_hasInteractive3 || tempo <= 0) {
			return false;
		}

		_lock.lock();
		_pendingTempo = std::clamp<std::int32_t>(tempo, 32, 512);
		_lock.unlock();
		return true;
#else
		return false;
#endif
	}

	bool AudioReaderMpt::SlideModuleChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds)
	{
#if defined(NCINE_HAS_OPENMPT_EXT)
		if (!_hasInteractive || channel < 0) {
			return false;
		}

		ChannelSlide slide;
		slide.Channel = channel;
		slide.From = 0.0f;
		slide.To = std::clamp(volume, 0.0f, 1.0f);
		slide.Length = std::max(std::int64_t(milliseconds), std::int64_t(0)) * _frequency / 1000;
		slide.Elapsed = 0;
		slide.Started = false;

		_lock.lock();
		_pendingSlides.push_back(slide);
		_lock.unlock();
		return true;
#else
		return false;
#endif
	}

	void AudioReaderMpt::ApplyPendingChanges() const
	{
		_lock.lock();
		bool position = _pendingPosition;
		std::int32_t order = _pendingOrder;
		std::int32_t row = _pendingRow;
		std::int32_t speed = _pendingSpeed;
		std::int32_t tempo = _pendingTempo;
		_pendingPosition = false;
		_pendingSpeed = 0;
		_pendingTempo = 0;
		for (const ChannelSlide& slide : _pendingSlides) {
			// A new slide of a channel replaces the one in progress
			for (std::size_t i = 0; i < _activeSlides.size(); i++) {
				if (_activeSlides[i].Channel == slide.Channel) {
					_activeSlides.erase(_activeSlides.begin() + i);
					break;
				}
			}
			_activeSlides.push_back(slide);
		}
		_pendingSlides.clear();
		_lock.unlock();

		if (position) {
			MPT_FUNC(openmpt_module_set_position_order_row)(_module, order, row);
		}
#if defined(NCINE_HAS_OPENMPT_EXT)
		if (speed > 0 && _hasInteractive) {
			_interactive.set_current_speed(_moduleExt, speed);
		}
#endif
#if defined(NCINE_HAS_OPENMPT_TEMPO2)
		if (tempo > 0 && _hasInteractive3) {
			_interactive3.set_current_tempo2(_moduleExt, double(tempo));
		}
#endif
		static_cast<void>(speed);
		static_cast<void>(tempo);

		if (!_activeSlides.empty()) {
			AdvanceSlides(0);
		}
	}

	void AudioReaderMpt::AdvanceSlides(std::int32_t frames) const
	{
#if defined(NCINE_HAS_OPENMPT_EXT)
		std::int32_t channelCount = 0;
#	if defined(WITH_OPENMPT_DYNAMIC)
		if (_openmpt_module_get_num_channels != nullptr) {
			channelCount = _openmpt_module_get_num_channels(_module);
		}
#	else
		channelCount = openmpt_module_get_num_channels(_module);
#	endif

		for (std::size_t i = 0; i < _activeSlides.size(); ) {
			ChannelSlide& slide = _activeSlides[i];
			if (slide.Channel >= channelCount) {
				_activeSlides.erase(_activeSlides.begin() + i);
				continue;
			}
			if (!slide.Started) {
				slide.Started = true;
				slide.From = float(_interactive.get_channel_volume(_moduleExt, slide.Channel));
			}

			slide.Elapsed = std::min(slide.Elapsed + frames, slide.Length);
			float volume = (slide.Elapsed >= slide.Length ? slide.To
				: slide.From + (slide.To - slide.From) * float(double(slide.Elapsed) / double(slide.Length)));
			_interactive.set_channel_volume(_moduleExt, slide.Channel, double(volume));

			if (slide.Elapsed >= slide.Length) {
				_activeSlides.erase(_activeSlides.begin() + i);
			} else {
				i++;
			}
		}
#else
		static_cast<void>(frames);
		_activeSlides.clear();
#endif
	}

	void AudioReaderMpt::RecordState(std::int64_t frame, bool force) const
	{
		// Speed and tempo are queried only when the row changes, or on the first slice of a read
		AudioModuleState state = {};
		QueryPosition(state);
		if (!force && state.Order == _recordedOrder && state.Row == _recordedRow) {
			return;
		}
		_recordedOrder = state.Order;
		_recordedRow = state.Row;
		QueryTiming(state);

		_lock.lock();
		// Only changes are kept, so the timeline reaches further back
		std::int32_t last = (_timelineNext + TimelineSize - 1) % TimelineSize;
		if (_timelineCount == 0 || std::memcmp(&_timeline[last].State, &state, sizeof(state)) != 0) {
			_timeline[_timelineNext] = { frame, state };
			_timelineNext = (_timelineNext + 1) % TimelineSize;
			if (_timelineCount < TimelineSize) {
				_timelineCount++;
			}
		}
		_lock.unlock();
	}

	void AudioReaderMpt::QueryPosition(AudioModuleState& state) const
	{
#if defined(WITH_OPENMPT_DYNAMIC)
		if (_openmpt_module_get_current_order != nullptr) {
			state.Order = _openmpt_module_get_current_order(_module);
		}
		if (_openmpt_module_get_current_row != nullptr) {
			state.Row = _openmpt_module_get_current_row(_module);
		}
#else
		state.Order = openmpt_module_get_current_order(_module);
		state.Row = openmpt_module_get_current_row(_module);
#endif
	}

	void AudioReaderMpt::QueryTiming(AudioModuleState& state) const
	{
#if defined(WITH_OPENMPT_DYNAMIC)
		if (_openmpt_module_get_current_speed != nullptr) {
			state.Speed = _openmpt_module_get_current_speed(_module);
		}
		if (_openmpt_module_get_current_tempo2 != nullptr) {
			state.Tempo = std::int32_t(_openmpt_module_get_current_tempo2(_module) + 0.5);
		} else if (_openmpt_module_get_current_tempo != nullptr) {
			state.Tempo = _openmpt_module_get_current_tempo(_module);
		}
#else
		state.Speed = openmpt_module_get_current_speed(_module);
#	if defined(NCINE_HAS_OPENMPT_0_7)
		state.Tempo = std::int32_t(openmpt_module_get_current_tempo2(_module) + 0.5);
#	else
		state.Tempo = openmpt_module_get_current_tempo(_module);
#	endif
#endif
	}

	bool AudioReaderMpt::CanSetModulePosition() const
	{
#if defined(WITH_OPENMPT_DYNAMIC)
		if (_openmpt_module_set_position_order_row == nullptr) {
			return false;
		}
#endif
		return (_module != nullptr);
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

		// Optional, older versions of the library lack some of them
		_openmpt_module_set_position_order_row = (openmpt_module_set_position_order_row_t*)::dlsym(_openmptLib, "openmpt_module_set_position_order_row");
		_openmpt_module_get_current_order = (openmpt_module_get_current_int_t*)::dlsym(_openmptLib, "openmpt_module_get_current_order");
		_openmpt_module_get_current_row = (openmpt_module_get_current_int_t*)::dlsym(_openmptLib, "openmpt_module_get_current_row");
		_openmpt_module_get_current_speed = (openmpt_module_get_current_int_t*)::dlsym(_openmptLib, "openmpt_module_get_current_speed");
		_openmpt_module_get_current_tempo = (openmpt_module_get_current_int_t*)::dlsym(_openmptLib, "openmpt_module_get_current_tempo");
		_openmpt_module_get_current_tempo2 = (openmpt_module_get_current_tempo2_t*)::dlsym(_openmptLib, "openmpt_module_get_current_tempo2");
		_openmpt_module_get_num_channels = (openmpt_module_get_current_int_t*)::dlsym(_openmptLib, "openmpt_module_get_num_channels");
#	if defined(NCINE_HAS_OPENMPT_EXT)
		_openmpt_module_ext_create = (openmpt_module_ext_create_t*)::dlsym(_openmptLib, "openmpt_module_ext_create");
		_openmpt_module_ext_destroy = (openmpt_module_ext_destroy_t*)::dlsym(_openmptLib, "openmpt_module_ext_destroy");
		_openmpt_module_ext_get_module = (openmpt_module_ext_get_module_t*)::dlsym(_openmptLib, "openmpt_module_ext_get_module");
		_openmpt_module_ext_get_interface = (openmpt_module_ext_get_interface_t*)::dlsym(_openmptLib, "openmpt_module_ext_get_interface");
#	endif

		return true;
	}
#endif

	std::size_t AudioReaderMpt::StreamRead(void* stream, void* dst, std::size_t bytes)
	{
		AudioReaderMpt* _this = static_cast<AudioReaderMpt*>(stream);
		return _this->_fileHandle->Read(dst, (unsigned long)bytes);
	}

	int AudioReaderMpt::StreamSeek(void* stream, std::int64_t offset, int whence)
	{
		SeekOrigin origin;
		switch (whence) {
			default:
			case OPENMPT_STREAM_SEEK_SET: origin = SeekOrigin::Begin; break;
			case OPENMPT_STREAM_SEEK_CUR: origin = SeekOrigin::Current; break;
			case OPENMPT_STREAM_SEEK_END: origin = SeekOrigin::End; break;
		}

		AudioReaderMpt* _this = static_cast<AudioReaderMpt*>(stream);
		_this->_fileHandle->Seek(offset, origin);
		return 0;
	}

	std::int64_t AudioReaderMpt::StreamTell(void* stream)
	{
		AudioReaderMpt* _this = static_cast<AudioReaderMpt*>(stream);
		return _this->_fileHandle->GetPosition();
	}

	void AudioReaderMpt::InternalLog(const char* message, void* user)
	{
		LOGI("{}", message);
	}
}

#endif
