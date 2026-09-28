#if defined(WITH_N64AUDIO)

#include "N64AudioDevice.h"
#include "../../AudioMixerCommon.h"
#include "../../IAudioPlayer.h"
#include "../../../../Main.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <Containers/String.h>
#include <IO/FileSystem.h>

#include <audio.h>
#include <n64sys.h>
#include <rspq.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine
{
	namespace
	{
		/** @brief Bytes of heap still free, which is what every allocation here is judged against */
		std::int32_t GetFreeHeapBytes()
		{
			heap_stats_t stats;
			sys_get_heap_stats(&stats);
			return stats.free;
		}

		/** @brief Whether @p path names a file of the given (lower-case) extension */
		bool HasExtension(StringView path, StringView extension)
		{
			return fs::GetExtension(path) == extension;
		}
	}

	N64AudioDevice::N64AudioDevice()
		: _valid(false), _suspended(false), _outputFrequency(OutputFrequency), _startSerial(0), _buffers(nullptr),
			_bufferCount(0), _bufferCapacity(0)
	{
		std::memset(_channelOwner, 0, sizeof(_channelOwner));

		// The second argument is a headroom, not a buffer count. With no mixer thread the queue is topped up once
		// per frame from updatePlayers() (see the class documentation), so it has to outlive a late frame; the
		// default is 160 ms, about five frames of this game at its usual pace.
		audio_init(OutputFrequency, AUDIO_DEFAULT_LATENCY);

		// The AI clocks itself off the video clock through an integer divider, so the granted rate is only near
		// the requested one (and differs between NTSC and PAL units); the mixer resamples to the granted one
		_outputFrequency = audio_get_frequency();
		if (_outputFrequency <= 0 || audio_get_buffer_length() <= 0) {
			LOGE("Cannot initialize the audio interface, sound will be disabled");
			audio_close();
			return;
		}

		mixer_init(MixerChannelCount);
		// The mixer sizes the ring of a channel that streams from the cartridge by the fastest rate it may be
		// asked to play at. Channels are prepared for sound effects here; a module retunes the ones it takes
		// for its instruments, and they are prepared again when an effect gets them back (see StartSource()).
		for (std::int32_t ch = 0; ch < MixerChannelCount; ch++) {
			mixer_ch_set_limits(ch, 16, float(MaxEffectFrequency), 0);
		}
		// VADPCM (level 1) is always available; the asset packer compresses pre-rendered music with ULC
		// (level 2), whose decoder is only linked in when asked for. Opus (level 3) is not used - it would
		// cost the CPU far more than the RSP-assisted ULC for the same music.
		__wav64_init_compression_lvl2();

		_buffers = static_cast<Buffer*>(std::malloc(std::size_t(InitialBufferCapacity) * sizeof(Buffer)));
		if (_buffers == nullptr) {
			LOGE("Cannot allocate the audio buffer table ({} bytes of heap free), sound will be disabled", GetFreeHeapBytes());
			mixer_close();
			audio_close();
			return;
		}
		_bufferCapacity = InitialBufferCapacity;
		// Buffer id 0 is reserved as "no buffer"
		_buffers[0] = Buffer{};
		_bufferCount = 1;

		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		setSourcePool(arrayView(sourceIds, MaxSources));

		_valid = true;
		LOGI("Audio device initialized: libdragon RSP mixer, {} channels at {} Hz, {} buffers of {} samples, {} bytes of heap free",
			MixerChannelCount, _outputFrequency, audio_get_num_buffers(), audio_get_buffer_length(), GetFreeHeapBytes());
	}

	N64AudioDevice::~N64AudioDevice()
	{
		if (_valid) {
			for (NativeStream& stream : _nativeStreams) {
				if (stream.Kind != NativeStream::Type::None) {
					closeNativeStream(std::uint32_t(&stream - _nativeStreams) + 1);
				}
			}
			for (std::int32_t ch = 0; ch < MixerChannelCount; ch++) {
				mixer_ch_stop(ch);
			}
			// Mix rounds the RSP has not run yet still point at the sample data released below
			rspq_highpri_sync();
		}

		for (std::int32_t i = 1; i < _bufferCount; i++) {
			if (_buffers[i].Used) {
				ReleaseBufferData(_buffers[i]);
			}
		}
		std::free(_buffers);
		_buffers = nullptr;
		_bufferCount = 0;
		_bufferCapacity = 0;

		if (_valid) {
			mixer_close();
			audio_close();
			_valid = false;
		}
	}

	bool N64AudioDevice::isValid() const
	{
		return _valid;
	}

	const char* N64AudioDevice::name() const
	{
		return "libdragon RSP mixer";
	}

	void N64AudioDevice::setGain(float gain)
	{
		_gain = gain;
		if (_valid) {
			mixer_set_vol(AudioMixer::Clamp01(gain));
		}
	}

	void N64AudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		// No Doppler on this backend, so the velocity is not kept. The panning follows at the next
		// updatePlayers(), which is where every audible source is looked at anyway.
		static_cast<void>(velocity);
		_listenerPos = position;
	}

	std::int32_t N64AudioDevice::nativeFrequency()
	{
		return _outputFrequency;
	}

	std::uint32_t N64AudioDevice::registerPlayer(IAudioPlayer* player)
	{
		const std::uint32_t sourceId = AudioDeviceBase::registerPlayer(player);
		if (sourceId != UnavailableSource) {
			if (Source* source = GetSource(sourceId)) {
				// A source comes back from the pool stopped, but make sure no channel or stream binding of its
				// previous user survives into the new one
				ReleaseSourceChannels(*source);
				*source = Source{};
			}
		}
		return sourceId;
	}

	N64AudioDevice::Source* N64AudioDevice::GetSource(std::uint32_t sourceId)
	{
		if (sourceId == 0 || sourceId > std::uint32_t(MaxSources)) {
			return nullptr;
		}
		return &_sources[sourceId - 1];
	}

	N64AudioDevice::Buffer* N64AudioDevice::GetBuffer(std::uint32_t bufferId)
	{
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return nullptr;
		}
		Buffer& buffer = _buffers[bufferId];
		return (buffer.Used ? &buffer : nullptr);
	}

	N64AudioDevice::NativeStream* N64AudioDevice::GetNativeStream(std::uint32_t streamId)
	{
		if (streamId == 0 || streamId > std::uint32_t(MaxNativeStreams)) {
			return nullptr;
		}
		NativeStream& stream = _nativeStreams[streamId - 1];
		return (stream.Kind != NativeStream::Type::None ? &stream : nullptr);
	}

	std::uint32_t N64AudioDevice::createBuffer(BufferUsage usage)
	{
		// Every buffer is a file streamed from the cartridge (see loadNativeBuffer()), so the usage says
		// nothing this backend can act on
		static_cast<void>(usage);

		if (_buffers == nullptr) {
			return 0;
		}

		std::int32_t index = -1;
		for (std::int32_t i = 1; i < _bufferCount; i++) {
			if (!_buffers[i].Used) {
				index = i;
				break;
			}
		}
		if (index < 0) {
			if (_bufferCount == _bufferCapacity) {
				// The old table stays valid until the new one exists. Returning 0 is the interface's "no buffer",
				// and AudioBuffer already reports and survives it.
				const std::int32_t newCapacity = _bufferCapacity * 2;
				Buffer* grown = static_cast<Buffer*>(std::realloc(_buffers, std::size_t(newCapacity) * sizeof(Buffer)));
				if (grown == nullptr) {
					LOGE("Cannot grow the audio buffer table to {} entries ({} bytes of heap free)", newCapacity, GetFreeHeapBytes());
					return 0;
				}
				_buffers = grown;
				_bufferCapacity = newCapacity;
			}
			index = _bufferCount++;
		}

		_buffers[index] = Buffer{};
		_buffers[index].Used = true;
		return std::uint32_t(index);
	}

	void N64AudioDevice::StopChannelsPlaying(const waveform_t* wave)
	{
		bool stoppedAny = false;
		for (std::int32_t ch = 0; ch < MixerChannelCount; ch++) {
			if (mixer_ch_playing_waveform(ch) == wave) {
				stoppedAny = true;
				const std::uint8_t owner = _channelOwner[ch];
				if (owner > 0 && owner <= MaxSources) {
					Source& source = _sources[owner - 1];
					source.Playing = false;
					source.Paused = false;
					ReleaseSourceChannels(source);
				} else {
					mixer_ch_stop(ch);
				}
			}
		}
		if (stoppedAny) {
			// A mix round already queued for the RSP still reads the data the caller is about to release
			rspq_highpri_sync();
		}
	}

	void N64AudioDevice::ReleaseBufferData(Buffer& buffer)
	{
		if (buffer.File != nullptr) {
			StopChannelsPlaying(&buffer.File->wave);
			// Closing also stops every channel still playing it
			wav64_close(buffer.File);
			buffer.File = nullptr;
		}
	}

	void N64AudioDevice::deleteBuffer(std::uint32_t bufferId)
	{
		Buffer* buffer = GetBuffer(bufferId);
		if (buffer == nullptr) {
			return;
		}
		ReleaseBufferData(*buffer);
		for (Source& source : _sources) {
			if (source.BufferId == bufferId) {
				source.BufferId = 0;
			}
		}
		buffer->Used = false;
	}

	bool N64AudioDevice::loadNativeBuffer(std::uint32_t bufferId, StringView path, NativeAudioInfo& info)
	{
		Buffer* buffer = GetBuffer(bufferId);
		if (!_valid || buffer == nullptr || !HasExtension(path, "wav64"_s)) {
			return false;
		}
		// libdragon asserts on a file it cannot open, so a missing one has to be told apart here
		if (!fs::IsReadableFile(path)) {
			return false;
		}

		ReleaseBufferData(*buffer);

		// Streamed, never preloaded: the samples stay in the cartridge and are DMA'd into the playing channel's
		// small ring a little ahead of playback, which is the whole point of the format on this console
		wav64_loadparms_t parms = {};
		parms.streaming_mode = WAV64_STREAMING_FULL;
		wav64_t* file = wav64_load(String(path).data(), &parms);
		if (file == nullptr) {
			return false;
		}
		// The whole sound as a terminal loop, armed or disarmed per channel (see StartSource())
		wav64_set_loop(file, true);

		buffer->File = file;

		info.BytesPerSample = file->wave.bits / 8;
		info.NumChannels = file->wave.channels;
		info.Frequency = std::int32_t(file->wave.frequency);
		info.NumSamples = file->wave.len;
		return true;
	}

	std::uint32_t N64AudioDevice::openNativeStream(StringView path, NativeAudioInfo& info)
	{
		if (!_valid) {
			return 0;
		}
		const bool isModule = HasExtension(path, "xm64"_s);
		if (!isModule && !HasExtension(path, "wav64"_s)) {
			return 0;
		}
		if (!fs::IsReadableFile(path)) {
			return 0;
		}

		std::int32_t index = -1;
		for (std::int32_t i = 0; i < MaxNativeStreams; i++) {
			if (_nativeStreams[i].Kind == NativeStream::Type::None) {
				index = i;
				break;
			}
		}
		if (index < 0) {
			LOGW("Cannot open \"{}\" - {} native streams are open already", path, MaxNativeStreams);
			return 0;
		}

		NativeStream& stream = _nativeStreams[index];
		stream = NativeStream{};
		String nullTerminatedPath = path;
		if (isModule) {
			// One allocation for the whole module; patterns are decoded as they play and the instruments are
			// streamed from the cartridge like any other wav64
			xm64player_open(&stream.Module, nullTerminatedPath.data());
			stream.Kind = NativeStream::Type::Module;
			stream.ChannelCount = std::int8_t(xm64player_num_channels(&stream.Module));
			// The player loops by default; the source's flag decides (see StartNativeStream())
			xm64player_set_loop(&stream.Module, false);

			info.BytesPerSample = 2;
			info.NumChannels = 2;
			info.Frequency = _outputFrequency;
			info.NumSamples = -1;
			LOGI("Opened module \"{}\" ({} channels, {} bytes of heap free)", path, stream.ChannelCount, GetFreeHeapBytes());
		} else {
			wav64_loadparms_t parms = {};
			parms.streaming_mode = WAV64_STREAMING_FULL;
			stream.Wave = wav64_load(nullTerminatedPath.data(), &parms);
			if (stream.Wave == nullptr) {
				stream = NativeStream{};
				return 0;
			}
			stream.Kind = NativeStream::Type::Wave;
			stream.ChannelCount = std::int8_t(stream.Wave->wave.channels);

			info.BytesPerSample = stream.Wave->wave.bits / 8;
			info.NumChannels = stream.Wave->wave.channels;
			info.Frequency = std::int32_t(stream.Wave->wave.frequency);
			info.NumSamples = stream.Wave->wave.len;
		}

		if (stream.ChannelCount <= 0 || stream.ChannelCount > MixerChannelCount - 4) {
			LOGE("Cannot play \"{}\" - it needs {} of the {} mixer channels", path, stream.ChannelCount, MixerChannelCount);
			closeNativeStream(std::uint32_t(index + 1));
			return 0;
		}
		return std::uint32_t(index + 1);
	}

	void N64AudioDevice::closeNativeStream(std::uint32_t streamId)
	{
		NativeStream* stream = GetNativeStream(streamId);
		if (stream == nullptr) {
			return;
		}

		for (Source& source : _sources) {
			if (source.NativeStream == streamId) {
				source.NativeStream = 0;
				source.Playing = false;
				source.Paused = false;
			}
		}

		if (stream->Kind == NativeStream::Type::Module) {
			// xm64player_close() stops and resets the channels the player was last started on - which are the
			// first ones of the mixer for a player that never played. So the player is pointed at channels it is
			// free to touch first: its own if it holds them, a free run otherwise.
			std::int32_t first = (stream->Active ? stream->Channel : FindFreeChannels(stream->ChannelCount, true));
			if (first < 0) {
				first = MixerChannelCount - stream->ChannelCount;
				for (std::int32_t ch = first; ch < MixerChannelCount; ch++) {
					const std::uint8_t owner = _channelOwner[ch];
					if (owner > 0 && owner <= MaxSources) {
						_sources[owner - 1].Playing = false;
						ReleaseSourceChannels(_sources[owner - 1]);
					}
				}
			}
			stream->Module.first_ch = first;
			xm64player_close(&stream->Module);
			for (std::int32_t ch = first; ch < first + stream->ChannelCount; ch++) {
				// The player resets the channels to the default limit - the output rate - but leaves the frequency of
				// the last note in place, and the mixer checks it against the limit on its next round whether the
				// channel plays or not. So the frequency goes to zero and the channel is prepared for effects again.
				mixer_ch_set_freq(ch, 0.0f);
				mixer_ch_set_limits(ch, 16, float(MaxEffectFrequency), 0);
				if (_channelOwner[ch] == ChannelNativeStream) {
					_channelOwner[ch] = 0;
				}
			}
		} else if (stream->Kind == NativeStream::Type::Wave) {
			StopNativeStream(*stream, false);
			wav64_close(stream->Wave);
		}

		*stream = NativeStream{};
	}

	void N64AudioDevice::setSourceNativeStream(std::uint32_t sourceId, std::uint32_t streamId)
	{
		if (Source* source = GetSource(sourceId)) {
			ReleaseSourceChannels(*source);
			source->BufferId = 0;
			source->NativeStream = (GetNativeStream(streamId) != nullptr ? streamId : 0);
		}
	}

	std::int32_t N64AudioDevice::FindFreeChannels(std::int32_t count, bool fromTop) const
	{
		if (count <= 0 || count > MixerChannelCount) {
			return -1;
		}
		if (fromTop) {
			for (std::int32_t first = MixerChannelCount - count; first >= 0; first--) {
				std::int32_t n = 0;
				while (n < count && _channelOwner[first + n] == 0) {
					n++;
				}
				if (n == count) {
					return first;
				}
			}
		} else {
			for (std::int32_t first = 0; first + count <= MixerChannelCount; first++) {
				std::int32_t n = 0;
				while (n < count && _channelOwner[first + n] == 0) {
					n++;
				}
				if (n == count) {
					return first;
				}
			}
		}
		return -1;
	}

	std::int32_t N64AudioDevice::AcquireEffectChannels(std::uint32_t sourceId, std::int32_t count)
	{
		std::int32_t first = FindFreeChannels(count, false);
		while (first < 0) {
			// Nothing free: the effect that has been playing the longest gives its channel up. It is the one least
			// likely to still matter - a new sound is almost always the one the player is reacting to - and its
			// player notices through isSourcePlaying() and finishes like any other that ran out.
			Source* oldest = nullptr;
			for (Source& source : _sources) {
				if (source.Channel >= 0 && source.NativeStream == 0 &&
					(oldest == nullptr || std::int32_t(source.StartSerial - oldest->StartSerial) < 0)) {
					oldest = &source;
				}
			}
			if (oldest == nullptr) {
				return -1;
			}
			oldest->Playing = false;
			oldest->Paused = false;
			ReleaseSourceChannels(*oldest);
			first = FindFreeChannels(count, false);
		}

		for (std::int32_t ch = first; ch < first + count; ch++) {
			_channelOwner[ch] = std::uint8_t(sourceId);
		}
		return first;
	}

	void N64AudioDevice::ReleaseSourceChannels(Source& source)
	{
		if (source.Channel < 0) {
			return;
		}
		mixer_ch_stop(source.Channel);
		for (std::int32_t ch = source.Channel; ch < source.Channel + source.ChannelCount; ch++) {
			_channelOwner[ch] = 0;
		}
		source.Channel = -1;
		source.ChannelCount = 0;
	}

	waveform_t* N64AudioDevice::GetSourceWave(Source& source)
	{
		Buffer* buffer = GetBuffer(source.BufferId);
		return (buffer != nullptr && buffer->File != nullptr ? &buffer->File->wave : nullptr);
	}

	bool N64AudioDevice::StartSource(std::uint32_t sourceId, Source& source)
	{
		waveform_t* wave = GetSourceWave(source);
		if (wave == nullptr) {
			return false;
		}

		const std::int32_t count = (wave->channels == 2 ? 2 : 1);
		const std::int32_t first = AcquireEffectChannels(sourceId, count);
		if (first < 0) {
			return false;
		}
		// A module may have retuned these channels for its instruments while it held them; a no-op otherwise
		for (std::int32_t ch = first; ch < first + count; ch++) {
			mixer_ch_set_limits(ch, 16, float(MaxEffectFrequency), 0);
		}

		source.Channel = std::int8_t(first);
		source.ChannelCount = std::int8_t(count);
		source.StartSerial = ++_startSerial;

		// The gains go first, so the channel does not ramp in from whatever its previous sound left behind
		source.AppliedLeft = -1.0f;
		ApplySourceMix(source, true);
		mixer_ch_play(first, wave);
		// The channel's ring is sized for MaxEffectFrequency, and the mixer asserts past it
		mixer_ch_set_freq(first, std::min(wave->frequency * source.Pitch, float(MaxEffectFrequency)));
		if (wave->loop_len > 0) {
			mixer_ch_set_loop(first, source.Looping);
		}
		if (source.PendingPosition > 0.0) {
			SeekSourceChannel(source, source.PendingPosition);
			source.PendingPosition = 0.0;
		}
		return true;
	}

	void N64AudioDevice::SeekSourceChannel(Source& source, double position)
	{
		// A VADPCM stream can only be restarted on a frame its file saved the decoder state for - its seek points,
		// or the start - and the mixer asserts on any other ("invalid VADPCM seeking point"). The effects are
		// converted without seek points, and a paused one (the whole level pauses its sounds with the game) is
		// taken off its channel, so resuming it is such a seek; it used to crash whenever the channel it came
		// back on had played something else in between. wav64_seek() rounds the position to what the file
		// allows, which for the effects means they start over. A channel that is not playing the file has no
		// position of it to move.
		const Buffer* buffer = GetBuffer(source.BufferId);
		if (buffer != nullptr && buffer->File != nullptr && mixer_ch_playing_waveform(source.Channel) == &buffer->File->wave) {
			wav64_seek(buffer->File, source.Channel, position / double(buffer->File->wave.frequency));
		}
	}

	void N64AudioDevice::ApplySourceMix(Source& source, bool force)
	{
		if (source.Channel < 0) {
			return;
		}
		// The positional model is shared with the software-mixing backends, so panning sounds the same. The
		// master gain is the mixer's own (see setGain()), so it is left out here.
		float leftGain, rightGain;
		AudioMixer::ComputeStereoGains(source.Relative, source.Position, _listenerPos, source.Gain, 1.0f, leftGain, rightGain);
		if (force || std::abs(leftGain - source.AppliedLeft) > 0.002f || std::abs(rightGain - source.AppliedRight) > 0.002f) {
			source.AppliedLeft = leftGain;
			source.AppliedRight = rightGain;
			mixer_ch_set_vol(source.Channel, leftGain, rightGain);
		}
	}

	void N64AudioDevice::setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		if (Source* source = GetSource(sourceId)) {
			if (source->BufferId != bufferId) {
				ReleaseSourceChannels(*source);
			}
			source->BufferId = bufferId;
			source->PendingPosition = 0.0;
		}
	}

	void N64AudioDevice::setSourceGain(std::uint32_t sourceId, float gain)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Gain = gain;
			if (NativeStream* stream = GetNativeStream(source->NativeStream)) {
				stream->Gain = gain;
				ApplyNativeStreamGain(*stream);
			} else {
				ApplySourceMix(*source, false);
			}
		}
	}

	void N64AudioDevice::setSourcePitch(std::uint32_t sourceId, float pitch)
	{
		if (Source* source = GetSource(sourceId)) {
			// A non-positive pitch would stall or reverse playback, neither of which the mixer expresses
			source->Pitch = (pitch > 0.0f ? pitch : 1.0f);
			if (source->Channel >= 0) {
				if (waveform_t* wave = mixer_ch_playing_waveform(source->Channel)) {
					mixer_ch_set_freq(source->Channel, std::min(wave->frequency * source->Pitch, float(MaxEffectFrequency)));
				}
			}
		}
	}

	void N64AudioDevice::setSourceLooping(std::uint32_t sourceId, bool looping)
	{
		Source* source = GetSource(sourceId);
		if (source == nullptr || source->Looping == looping) {
			return;
		}
		source->Looping = looping;
		if (NativeStream* stream = GetNativeStream(source->NativeStream)) {
			stream->Looping = looping;
			if (stream->Kind == NativeStream::Type::Module) {
				xm64player_set_loop(&stream->Module, looping);
			} else if (stream->Active && stream->Wave->wave.loop_len > 0) {
				mixer_ch_set_loop(stream->Channel, looping);
			}
		} else if (source->Channel >= 0) {
			waveform_t* wave = mixer_ch_playing_waveform(source->Channel);
			if (wave != nullptr && wave->loop_len > 0) {
				mixer_ch_set_loop(source->Channel, looping);
			}
		}
	}

	void N64AudioDevice::setSourceRelative(std::uint32_t sourceId, bool relative)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Relative = relative;
		}
	}

	void N64AudioDevice::setSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Position = position;
		}
	}

	void N64AudioDevice::setSourceLowPass(std::uint32_t sourceId, float value)
	{
		// The mixer has no per-channel filter
		static_cast<void>(sourceId);
		static_cast<void>(value);
	}

	std::int32_t N64AudioDevice::sourceSampleOffset(std::uint32_t sourceId)
	{
		Source* source = GetSource(sourceId);
		if (source == nullptr) {
			return 0;
		}
		const double position = (source->Channel >= 0 ? mixer_ch_get_pos(source->Channel) : source->PendingPosition);
		return std::int32_t(position);
	}

	void N64AudioDevice::setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		Source* source = GetSource(sourceId);
		if (source == nullptr) {
			return;
		}
		const double position = double(offset > 0 ? offset : 0);
		if (source->Channel >= 0) {
			SeekSourceChannel(*source, position);
		} else {
			source->PendingPosition = position;
		}
	}

	void N64AudioDevice::playSource(std::uint32_t sourceId)
	{
		Source* source = GetSource(sourceId);
		if (source == nullptr || !_valid) {
			return;
		}

		if (NativeStream* stream = GetNativeStream(source->NativeStream)) {
			stream->Looping = source->Looping;
			stream->Gain = source->Gain;
			source->Playing = StartNativeStream(*stream);
			source->Paused = false;
			return;
		}

		if (source->Playing && !source->Paused && source->Channel >= 0) {
			// Already audible, nothing to restart
			return;
		}
		source->Paused = false;
		source->Playing = StartSource(sourceId, *source);
	}

	void N64AudioDevice::pauseSource(std::uint32_t sourceId)
	{
		Source* source = GetSource(sourceId);
		if (source == nullptr || !source->Playing) {
			return;
		}
		source->Paused = true;
		if (NativeStream* stream = GetNativeStream(source->NativeStream)) {
			StopNativeStream(*stream, true);
			return;
		}
		// The mixer has no paused state, so the channel is given up and the position kept for the resume
		if (source->Channel >= 0) {
			source->PendingPosition = mixer_ch_get_pos(source->Channel);
			ReleaseSourceChannels(*source);
		}
	}

	void N64AudioDevice::stopSource(std::uint32_t sourceId)
	{
		Source* source = GetSource(sourceId);
		if (source == nullptr) {
			return;
		}
		source->Playing = false;
		source->Paused = false;
		source->PendingPosition = 0.0;
		if (NativeStream* stream = GetNativeStream(source->NativeStream)) {
			StopNativeStream(*stream, false);
			return;
		}
		ReleaseSourceChannels(*source);
	}

	bool N64AudioDevice::isSourcePlaying(std::uint32_t sourceId)
	{
		Source* source = GetSource(sourceId);
		if (source == nullptr || !source->Playing || source->Paused) {
			return false;
		}
		if (NativeStream* stream = GetNativeStream(source->NativeStream)) {
			if (!stream->Active) {
				return false;
			}
			if (stream->Kind == NativeStream::Type::Module) {
				return stream->Module.playing && !stream->Module.stop_requested;
			}
			return mixer_ch_playing(stream->Channel);
		}
		return (source->Channel >= 0 && mixer_ch_playing(source->Channel));
	}

	bool N64AudioDevice::StartNativeStream(NativeStream& stream)
	{
		if (stream.Active) {
			if (stream.Kind == NativeStream::Type::Module) {
				// Resumed before the player's next tick got to a requested stop - simply keep playing
				stream.Module.stop_requested = false;
				xm64player_set_loop(&stream.Module, stream.Looping);
			}
			stream.Paused = false;
			ApplyNativeStreamGain(stream);
			return true;
		}

		const std::int32_t count = stream.ChannelCount;
		std::int32_t first = FindFreeChannels(count, true);
		if (first < 0) {
			// Music takes precedence over effects: the top of the range is cleared of whatever effects sit there
			first = MixerChannelCount - count;
			for (std::int32_t ch = first; ch < MixerChannelCount; ch++) {
				const std::uint8_t owner = _channelOwner[ch];
				if (owner == ChannelNativeStream || owner == ChannelExclusive) {
					LOGW("No {} mixer channels are free for music", count);
					return false;
				}
			}
			for (std::int32_t ch = first; ch < MixerChannelCount; ch++) {
				const std::uint8_t owner = _channelOwner[ch];
				if (owner > 0 && owner <= MaxSources) {
					_sources[owner - 1].Playing = false;
					ReleaseSourceChannels(_sources[owner - 1]);
				}
			}
		}
		for (std::int32_t ch = first; ch < first + count; ch++) {
			_channelOwner[ch] = ChannelNativeStream;
		}
		stream.Channel = std::int8_t(first);
		stream.Active = true;

		if (stream.Kind == NativeStream::Type::Module) {
			// The player retunes the limits of the channels its converter measured as used; one it judged unused
			// keeps whatever limit it had - the output rate by default - and a note played there after all asserts
			// in the mixer the moment its frequency passes that. So every channel gets a generous one first.
			for (std::int32_t ch = first; ch < first + count; ch++) {
				mixer_ch_set_limits(ch, 16, float(MaxModuleFrequency), 0);
			}
			xm64player_set_loop(&stream.Module, stream.Looping);
			xm64player_set_vol(&stream.Module, AudioMixer::Clamp01(stream.Gain));
			// Continues from wherever the player stopped: the start for a fresh or rewound stream (see
			// StopNativeStream()), the paused position otherwise
			xm64player_play(&stream.Module, first);
		} else {
			for (std::int32_t ch = first; ch < first + count; ch++) {
				mixer_ch_set_limits(ch, 16, float(MaxEffectFrequency), 0);
			}
			const float gain = AudioMixer::Clamp01(stream.Gain);
			mixer_ch_set_vol(first, gain, gain);
			mixer_ch_play(first, &stream.Wave->wave);
			if (stream.Wave->wave.loop_len > 0) {
				mixer_ch_set_loop(first, stream.Looping);
			}
			if (stream.Paused && stream.PausedPosition > 0.0) {
				// Compressed audio can only resume on one of its seek points, which wav64_seek() rounds to
				wav64_seek(stream.Wave, first, stream.PausedPosition / double(stream.Wave->wave.frequency));
			}
		}
		stream.Paused = false;
		return true;
	}

	void N64AudioDevice::StopNativeStream(NativeStream& stream, bool pause)
	{
		if (stream.Kind == NativeStream::Type::Module) {
			if (stream.Active && stream.Module.playing) {
				// Asynchronous - the player stops at its next tick, and its channels are given back once it has
				// (see RetireNativeStreams()); they are not the engine's to reuse before then
				xm64player_stop(&stream.Module);
			}
			if (!pause) {
				// Applied at the first tick after the next start
				xm64player_seek(&stream.Module, 0, 0, 0);
			}
		} else if (stream.Kind == NativeStream::Type::Wave) {
			if (stream.Active) {
				stream.PausedPosition = (pause ? mixer_ch_get_pos(stream.Channel) : 0.0);
				mixer_ch_stop(stream.Channel);
				for (std::int32_t ch = stream.Channel; ch < stream.Channel + stream.ChannelCount; ch++) {
					_channelOwner[ch] = 0;
				}
				stream.Active = false;
				stream.Channel = -1;
			} else if (!pause) {
				stream.PausedPosition = 0.0;
			}
		}
		stream.Paused = pause;
	}

	void N64AudioDevice::RetireNativeStreams()
	{
		for (NativeStream& stream : _nativeStreams) {
			if (!stream.Active) {
				continue;
			}
			bool stopped = false;
			if (stream.Kind == NativeStream::Type::Module) {
				stopped = !stream.Module.playing;
			} else if (stream.Kind == NativeStream::Type::Wave) {
				stopped = !mixer_ch_playing(stream.Channel);
				if (stopped) {
					stream.PausedPosition = 0.0;
				}
			}
			if (stopped) {
				for (std::int32_t ch = stream.Channel; ch < stream.Channel + stream.ChannelCount; ch++) {
					if (_channelOwner[ch] == ChannelNativeStream) {
						_channelOwner[ch] = 0;
					}
				}
				stream.Active = false;
				stream.Channel = -1;
			}
		}
	}

	void N64AudioDevice::ApplyNativeStreamGain(NativeStream& stream)
	{
		const float gain = AudioMixer::Clamp01(stream.Gain);
		if (stream.Kind == NativeStream::Type::Module) {
			xm64player_set_vol(&stream.Module, gain);
		} else if (stream.Kind == NativeStream::Type::Wave && stream.Active) {
			mixer_ch_set_vol(stream.Channel, gain, gain);
		}
	}

	std::int32_t N64AudioDevice::ReserveExclusiveChannels(std::int32_t count)
	{
		if (!_valid) {
			return -1;
		}
		std::int32_t first = FindFreeChannels(count, false);
		if (first < 0) {
			// Effects make room; music and other reservations do not
			first = 0;
			for (std::int32_t ch = 0; ch < count; ch++) {
				const std::uint8_t owner = _channelOwner[ch];
				if (owner == ChannelNativeStream || owner == ChannelExclusive) {
					return -1;
				}
				if (owner > 0 && owner <= MaxSources) {
					_sources[owner - 1].Playing = false;
					ReleaseSourceChannels(_sources[owner - 1]);
				}
			}
		}
		for (std::int32_t ch = first; ch < first + count; ch++) {
			mixer_ch_stop(ch);
			_channelOwner[ch] = ChannelExclusive;
		}
		return first;
	}

	void N64AudioDevice::ReleaseExclusiveChannels(std::int32_t firstChannel, std::int32_t count)
	{
		for (std::int32_t ch = firstChannel; ch >= 0 && ch < firstChannel + count && ch < MixerChannelCount; ch++) {
			if (_channelOwner[ch] == ChannelExclusive) {
				mixer_ch_stop(ch);
				_channelOwner[ch] = 0;
			}
		}
	}

	void N64AudioDevice::PollMixer()
	{
		if (_valid && !_suspended) {
			mixer_try_play();
		}
	}

	void N64AudioDevice::suspendDevice()
	{
		// Nothing to program: once the queue stops being topped up the AI drains what it holds and falls silent
		_suspended = true;
	}

	void N64AudioDevice::resumeDevice()
	{
		_suspended = false;
	}

	void N64AudioDevice::updatePlayers()
	{
		// The base class advances the players and retires the finished ones first, so what is mixed below is
		// the state this frame actually asked for
		AudioDeviceBase::updatePlayers();

		if (!_valid) {
			return;
		}

		RetireNativeStreams();

		for (Source& source : _sources) {
			if (source.Channel < 0) {
				continue;
			}
			if (!mixer_ch_playing(source.Channel)) {
				// Played to the end on its own; the player finds out through isSourcePlaying()
				source.Playing = false;
				ReleaseSourceChannels(source);
				continue;
			}
			// The listener moves every frame, so a positional source's panning has to follow it
			ApplySourceMix(source, false);
		}

		if (!_suspended) {
			mixer_try_play();
		}
	}
}

#endif
