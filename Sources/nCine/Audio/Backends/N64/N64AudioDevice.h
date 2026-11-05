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

		Mixing runs on the RSP; the CPU only assigns sources to mixer channels (effects from the bottom, music
		from the top, the longest-playing effect is evicted when nothing is free). Buffers are `.wav64` files
		streamed from the cartridge (@ref LoadNativeBuffer()) and music is a `.xm64` module or a `.wav64`
		recording (@ref OpenNativeStream()), so no sample data is held in RAM. There is no mixer thread - the
		mixer is polled once per frame from @ref UpdatePlayers().
	*/
	class N64AudioDevice : public AudioDeviceBase
	{
	public:
		N64AudioDevice();
		~N64AudioDevice() override;

		bool IsValid() const override;
		const char* GetName() const override;

		void SetGain(float gain) override;
		void UpdateListener(const Vector3f& position, const Vector3f& velocity) override;
		std::int32_t GetNativeFrequency() override;

		std::uint32_t RegisterPlayer(IAudioPlayer* player) override;
		void UpdatePlayers() override;

		std::uint32_t CreateBuffer(BufferUsage usage) override;
		void DeleteBuffer(std::uint32_t bufferId) override;

		bool LoadNativeBuffer(std::uint32_t bufferId, StringView path, NativeAudioInfo& info) override;
		std::uint32_t OpenNativeStream(StringView path, NativeAudioInfo& info) override;
		void CloseNativeStream(std::uint32_t streamId) override;
		void SetSourceNativeStream(std::uint32_t sourceId, std::uint32_t streamId) override;

		void SetSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override;
		void SetSourceGain(std::uint32_t sourceId, float gain) override;
		void SetSourcePitch(std::uint32_t sourceId, float pitch) override;
		void SetSourceLooping(std::uint32_t sourceId, bool looping) override;
		void SetSourceRelative(std::uint32_t sourceId, bool relative) override;
		void SetSourcePosition(std::uint32_t sourceId, const Vector3f& position) override;
		void SetSourceLowPass(std::uint32_t sourceId, float value) override;
		std::int32_t GetSourceSampleOffset(std::uint32_t sourceId) override;
		void SetSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset) override;
		void PlaySource(std::uint32_t sourceId) override;
		void PauseSource(std::uint32_t sourceId) override;
		void StopSource(std::uint32_t sourceId) override;
		bool IsSourcePlaying(std::uint32_t sourceId) override;

		void SuspendDevice() override;
		void ResumeDevice() override;

		/**
			@brief Takes @p count consecutive mixer channels away from the engine, returning the first one or `-1`

			Used by the video player for its audio track. Whatever played there is stopped, and the channels
			stay reserved until @ref ReleaseExclusiveChannels().
		*/
		std::int32_t ReserveExclusiveChannels(std::int32_t count);
		/** @brief Gives channels taken by @ref ReserveExclusiveChannels() back to the engine */
		void ReleaseExclusiveChannels(std::int32_t firstChannel, std::int32_t count);
		/**
			@brief Runs the mixer if the AI queue has room, without the rest of @ref UpdatePlayers()

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

			Nothing the game plays has content above 16 kHz, and every output sample costs RSP time and RDRAM
			bandwidth. The granted rate only lands near this (integer divider of the video clock).
		*/
		static constexpr std::int32_t OutputFrequency = 32000;
		/**
			@brief Highest rate a sound-effect channel is prepared to stream at

			Also bounds memory, since the mixer sizes a streaming channel's ring by it.
		*/
		static constexpr std::int32_t MaxEffectFrequency = 64000;
		/**
			@brief Limit a module's channels get before its player sizes the ones it knows it uses

			Covers the channels the player does not size itself (an instrument may play octaves above its rate).
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

			Held by value: channels point at the waveform inside @ref File (allocated by libdragon), so growing
			the table is safe.
		*/
		struct Buffer
		{
			/** @brief The file streamed from the cartridge, `nullptr` until one is loaded */
			wav64_t* File = nullptr;
			/** @brief Whether the entry is handed out (see @ref CreateBuffer()) */
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
			/** @brief Native stream bound to the source (see @ref SetSourceNativeStream()), or 0 */
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

		/** @brief Returns the source of an id handed out by @ref RegisterPlayer(), or `nullptr` */
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

			A cartridge stream can only seek to positions its file keeps decoder state for, so it is rounded to one.
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
