#pragma once

#if defined(WITH_N64AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

#include <mixer.h>
#include <wav64.h>
#include <xm64.h>

namespace nCine
{
	/**
		@brief Nintendo 64 implementation of @ref IAudioDevice on top of libdragon's RSP mixer

		The console has no sound chip - its Audio Interface is a bare stereo DAC that DMAs 16-bit samples out
		of RDRAM - but it has the RSP, and libdragon's mixer runs there: up to 32 channels resampled, panned
		and summed by microcode, with the result written straight into the buffers the AI plays. This backend
		used to do all of that on the VR4300 itself, one output sample at a time; now the CPU only decides
		what plays where and the RSP does the arithmetic. It is also what makes music possible at all:
		libdragon's tracker player (@ref xm64player_t) and its compressed waveforms (@ref wav64_t) are both
		clients of the same mixer.

		Every source is a logical voice that holds a mixer channel only while it is audible. Sound effects take
		channels from the bottom of the range, native streams (music) from the top, and when a new effect finds
		nothing free the effect that has been playing the longest gives its channel up - the one least likely
		to still be the sound the player is listening for.

		The engine never decodes a sample here (see `NCINE_HAS_NATIVE_AUDIO`), so no sample data is held in RAM
		at all. A buffer is a `.wav64` file (@ref loadNativeBuffer()) the mixer **streams from the cartridge** a
		few hundred samples ahead of playback, so a sound effect costs only its channel's small ring while it
		plays. Music arrives as a native stream (@ref openNativeStream()): a `.xm64` module played by
		libdragon's libxm port, which reads its instruments from the cartridge as well, or a `.wav64` recording
		(the tracks that could not be converted to XM are pre-rendered and compressed by the asset packer).

		*There is no mixer thread* - the mixer is polled from @ref updatePlayers(), once per frame, on the main
		thread, with @ref audio_init() given enough headroom to outlast a late frame. The mixer itself queues
		the RSP work asynchronously, so a poll costs the CPU only the bookkeeping.
	*/
	class N64AudioDevice : public AudioDeviceBase
	{
	public:
		N64AudioDevice();
		~N64AudioDevice() override;

		bool isValid() const override;
		const char* name() const override;

		void setGain(float gain) override;
		void updateListener(const Vector3f& position, const Vector3f& velocity) override;
		std::int32_t nativeFrequency() override;

		std::uint32_t registerPlayer(IAudioPlayer* player) override;
		void updatePlayers() override;

		std::uint32_t createBuffer(BufferUsage usage) override;
		void deleteBuffer(std::uint32_t bufferId) override;

		bool loadNativeBuffer(std::uint32_t bufferId, StringView path, NativeAudioInfo& info) override;
		std::uint32_t openNativeStream(StringView path, NativeAudioInfo& info) override;
		void closeNativeStream(std::uint32_t streamId) override;
		void setSourceNativeStream(std::uint32_t sourceId, std::uint32_t streamId) override;

		void setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override;
		void setSourceGain(std::uint32_t sourceId, float gain) override;
		void setSourcePitch(std::uint32_t sourceId, float pitch) override;
		void setSourceLooping(std::uint32_t sourceId, bool looping) override;
		void setSourceRelative(std::uint32_t sourceId, bool relative) override;
		void setSourcePosition(std::uint32_t sourceId, const Vector3f& position) override;
		void setSourceLowPass(std::uint32_t sourceId, float value) override;
		std::int32_t sourceSampleOffset(std::uint32_t sourceId) override;
		void setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset) override;
		void playSource(std::uint32_t sourceId) override;
		void pauseSource(std::uint32_t sourceId) override;
		void stopSource(std::uint32_t sourceId) override;
		bool isSourcePlaying(std::uint32_t sourceId) override;

		void suspendDevice() override;
		void resumeDevice() override;

		/**
			@brief Takes @p count consecutive mixer channels away from the engine, returning the first one or `-1`

			For code that drives the mixer by itself for a while - the full-motion video player plays its audio
			track on a channel of its own. Whatever the engine was playing there is stopped. The channels stay
			out of the engine's hands until @ref ReleaseExclusiveChannels().
		*/
		std::int32_t ReserveExclusiveChannels(std::int32_t count);
		/** @brief Gives channels taken by @ref ReserveExclusiveChannels() back to the engine */
		void ReleaseExclusiveChannels(std::int32_t firstChannel, std::int32_t count);
		/**
			@brief Runs the mixer if the AI queue has room, without the rest of @ref updatePlayers()

			For a loop that owns the main thread for a while (the video player) and has to keep the audio fed.
		*/
		void PollMixer();

	private:
		/** @brief Logical voices the engine can hold; only the audible ones occupy a mixer channel */
		static constexpr std::int32_t MaxSources = 32;
		/** @brief Channels of libdragon's mixer, which is also its maximum */
		static constexpr std::int32_t MixerChannelCount = MIXER_MAX_CHANNELS;
		/** @brief Upper bound on native streams open at once (a level's music, and the next one while it loads) */
		static constexpr std::int32_t MaxNativeStreams = 4;
		/**
			@brief Rate the AI is asked for; every channel is resampled to what it actually grants

			32 kHz rather than the 48 kHz this backend used to mix at by hand. The game's own sound effects are
			8-bit and mostly 11-22 kHz, and its music is tracker instruments of the same era, so nothing it plays
			carries content above 16 kHz - and every output sample is RSP time and RDRAM bandwidth the renderer
			shares. The AI derives the rate from the video clock through an integer divider, so the granted rate
			lands near this rather than on it.
		*/
		static constexpr std::int32_t OutputFrequency = 32000;
		/**
			@brief Highest rate a sound-effect channel is prepared to stream at

			The mixer sizes the ring of a channel that streams from the cartridge by the fastest rate it may be
			asked for, so this bounds the memory as well: a pitched-up 44.1 kHz sound is the worst the game
			asks for.
		*/
		static constexpr std::int32_t MaxEffectFrequency = 64000;
		/**
			@brief Limit a module's channels get before its player sizes the ones it knows it uses

			A tracker instrument can be played several octaves above its sample rate; the channels the player sizes
			itself get their own, exact limit, and this only has to cover the rest without an oversized ring.
		*/
		static constexpr std::int32_t MaxModuleFrequency = 128000;
		/** @brief Entries the buffer table starts with */
		static constexpr std::int32_t InitialBufferCapacity = 64;

		/** @brief Marks a mixer channel in @ref _channelOwner that belongs to a native stream */
		static constexpr std::uint8_t ChannelNativeStream = 0xFE;
		/** @brief Marks a mixer channel in @ref _channelOwner that was handed out exclusively */
		static constexpr std::uint8_t ChannelExclusive = 0xFF;

		/**
			@brief One buffer, playable by any number of channels at once

			Held by value in the table: the mixer points at the waveform embedded in @ref File, which libdragon
			allocated, so growing the table moves nothing a channel plays.
		*/
		struct Buffer
		{
			/** @brief The file streamed from the cartridge, `nullptr` until one is loaded */
			wav64_t* File = nullptr;
			/** @brief Whether the entry is handed out (see @ref createBuffer()) */
			bool Used = false;
		};

		/** @brief One logical voice */
		struct Source
		{
			bool Playing = false;
			bool Paused = false;
			bool Looping = false;
			bool Relative = true;
			float Gain = 1.0f;
			float Pitch = 1.0f;
			Vector3f Position = Vector3f(0.0f, 0.0f, 0.0f);

			/** @brief Buffer a static source plays, or 0 */
			std::uint32_t BufferId = 0;
			/** @brief Native stream bound to the source (see @ref setSourceNativeStream()), or 0 */
			std::uint32_t NativeStream = 0;

			/** @brief First mixer channel while audible, or -1 */
			std::int8_t Channel = -1;
			/** @brief Channels held from @ref Channel on (2 for a stereo waveform) */
			std::int8_t ChannelCount = 0;
			/** @brief Order in which the sources were started, for picking which one gives its channel up */
			std::uint32_t StartSerial = 0;
			/** @brief Position to resume from (in frames) after a pause or a seek while silent */
			double PendingPosition = 0.0;
			/** @brief Last gains given to the mixer, so an unchanged source costs no call */
			float AppliedLeft = -1.0f;
			float AppliedRight = -1.0f;
		};

		/** @brief Music the device plays by itself */
		struct NativeStream
		{
			enum class Type : std::uint8_t {
				None,
				Module,		/**< `.xm64`, played by libdragon's libxm port */
				Wave		/**< `.wav64`, a pre-rendered recording */
			};

			Type Kind = Type::None;
			/** @brief Whether the stream currently holds mixer channels */
			bool Active = false;
			/** @brief Whether the stream should resume where it was paused rather than from the start */
			bool Paused = false;
			bool Looping = false;
			float Gain = 1.0f;
			std::int8_t Channel = -1;
			std::int8_t ChannelCount = 0;
			/** @brief Where a paused recording resumes, in samples */
			double PausedPosition = 0.0;
			xm64player_t Module = {};
			wav64_t* Wave = nullptr;
		};

		bool _valid;
		bool _suspended;
		/** @brief Rate the AI actually granted */
		std::int32_t _outputFrequency;
		/** @brief Counter behind @ref Source::StartSerial */
		std::uint32_t _startSerial;

		/** @brief Buffer table, index 0 reserved as "no buffer" */
		Buffer* _buffers;
		std::int32_t _bufferCount;
		std::int32_t _bufferCapacity;

		Source _sources[MaxSources];
		NativeStream _nativeStreams[MaxNativeStreams];
		/** @brief Source id holding each mixer channel, or one of the Channel* markers, 0 if free */
		std::uint8_t _channelOwner[MixerChannelCount];

		/** @brief Returns the source of an id handed out by @ref registerPlayer(), or `nullptr` */
		Source* GetSource(std::uint32_t sourceId);
		/** @brief Returns the buffer of an id, or `nullptr` */
		Buffer* GetBuffer(std::uint32_t bufferId);
		/** @brief Returns the native stream of an id, or `nullptr` */
		NativeStream* GetNativeStream(std::uint32_t streamId);

		/** @brief Stops every channel that plays @p wave, so its data can be released or replaced */
		void StopChannelsPlaying(const waveform_t* wave);
		/** @brief Closes a buffer's file and stops anything playing it */
		void ReleaseBufferData(Buffer& buffer);

		/**
			@brief Finds @p count consecutive free channels, searching down from the top or up from the bottom

			@returns The first channel of the run, or -1
		*/
		std::int32_t FindFreeChannels(std::int32_t count, bool fromTop) const;
		/** @brief Takes channels for a sound effect, making room by stopping the longest-playing one if needed */
		std::int32_t AcquireEffectChannels(std::uint32_t sourceId, std::int32_t count);
		/** @brief Gives a source's channels back, stopping them */
		void ReleaseSourceChannels(Source& source);
		/** @brief Starts the waveform of a source on channels of its own */
		bool StartSource(std::uint32_t sourceId, Source& source);
		/**
			@brief Moves an audible source to @p position (in frames)

			A sound streamed from the cartridge can only be moved to a position its file keeps the decoder state
			for, so the position is rounded to one (see the implementation).
		*/
		void SeekSourceChannel(Source& source, double position);
		/** @brief Pushes gain, panning and pitch of an audible source to its channel if they changed */
		void ApplySourceMix(Source& source, bool force);
		/** @brief Returns the waveform a source plays, or `nullptr` if it has none yet */
		waveform_t* GetSourceWave(Source& source);

		/** @brief Starts or resumes a native stream on channels of its own */
		bool StartNativeStream(NativeStream& stream);
		/** @brief Stops a native stream, rewinding it unless @p pause is set */
		void StopNativeStream(NativeStream& stream, bool pause);
		/** @brief Gives the channels of a native stream back once it has really stopped */
		void RetireNativeStreams();
		/** @brief Pushes the gain of a native stream to its channels */
		void ApplyNativeStreamGain(NativeStream& stream);
	};
}

#endif
