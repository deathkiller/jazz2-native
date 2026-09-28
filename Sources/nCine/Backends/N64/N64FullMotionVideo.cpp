#if defined(WITH_N64)

#include "N64FullMotionVideo.h"
#include "../../ServiceLocator.h"
#if defined(WITH_N64AUDIO)
#	include "../../Audio/Backends/N64/N64AudioDevice.h"
#endif

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

#include <libdragon.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine::Backends
{
	namespace
	{
		/** @brief Frames at the start of a video during which a button does not skip it */
		constexpr std::int32_t SkipGraceFrames = 10;
		/** @brief Longest time a frame spends topping up the audio track, see @ref TopUpAudio() */
		constexpr std::uint32_t AudioTopUpBudgetUs = 20000;
		/** @brief Longest time a frame is held back for an audio track that fell behind it */
		constexpr std::uint32_t AudioCatchUpBudgetUs = 100000;

		/**
			@brief Mixes the audio track ahead, and holds the frame back while the track is behind it

			fmv_play() feeds the mixer twice per video frame, and while the RSP is still busy with the frame's
			decoding the mixer tops up one AI buffer per call and leaves the rest for later, rather than wait
			for the RSP. An AI buffer is 16 ms (512 samples at 32 kHz), so a 24 fps video got 32-34 ms of audio
			for every 42 ms of picture: the soundtrack ran at about 0.8x with gaps, and the video finished
			seconds before it. So the mixer is polled again here, once per frame, until the AI queue is full or
			the budget runs out.

			The player only ever catches the video UP to the audio (by skipping frames), it never holds it back,
			so the audio clock is enforced here as well: while the track has not been mixed up to the frame about
			to be shown, the frame waits for it. The mixed position runs ahead of what is heard by the AI queue,
			so this only triggers when the track has genuinely fallen behind.
		*/
		void TopUpAudio(std::int32_t channel, float frequency, float videoTimeSec)
		{
			const std::int32_t target = audio_get_num_buffers() - 1;
			const double videoPosition = double(videoTimeSec) * double(frequency);
			const std::uint64_t start = get_ticks();
			while (true) {
				const std::uint32_t elapsedUs = std::uint32_t(TICKS_TO_US(get_ticks() - start));
				const bool audioBehind = (mixer_ch_playing(channel) && mixer_ch_get_pos(channel) < videoPosition);
				if (audioBehind ? (elapsedUs >= AudioCatchUpBudgetUs)
								: (audio_get_queued_buffers() >= target || elapsedUs >= AudioTopUpBudgetUs)) {
					break;
				}
				mixer_try_play();
			}
		}

		struct PlaybackContext
		{
			bool Skipped = false;
			std::int32_t AudioChannel = -1;
		};

		void OnVideoFrame(void* ctx, int frameIndex, float timeSec, fmv_control_t* ctrl)
		{
			static_cast<void>(timeSec);
			PlaybackContext& context = *static_cast<PlaybackContext*>(ctx);
			if (context.AudioChannel >= 0 && ctrl->audio != nullptr) {
				TopUpAudio(context.AudioChannel, ctrl->audio->wave.frequency, timeSec);
			}

			// The engine's input manager is not running while the video owns the main thread, so the controllers
			// are polled here. Only fresh presses count, and not in the first frames - the button that started a
			// new game (and with it the ending) or skipped the logo may still be held.
			joypad_poll();
			if (frameIndex < SkipGraceFrames) {
				return;
			}
			for (std::int32_t port = 0; port < JOYPAD_PORT_COUNT; port++) {
				const joypad_buttons_t pressed = joypad_get_buttons_pressed(joypad_port_t(port));
				if (pressed.a || pressed.b || pressed.start) {
					context.Skipped = true;
					ctrl->stop(ctrl);
					break;
				}
			}
		}
	}

	String N64FullMotionVideo::FindVideo(StringView name)
	{
		// MPEG-1 is what the asset packer encodes, and the only decoder linked in (H.264 would be a second one)
		String path = fs::CombinePath({ "rom:/Content/Cinematics"_s, String(name + ".m1v"_s) });
		return (fs::IsReadableFile(path) ? path : String());
	}

	bool N64FullMotionVideo::Play(StringView path, float volume)
	{
		std::int32_t audioChannel = -1;
#if defined(WITH_N64AUDIO)
		// Nothing plays over a video. The audio track takes two channels of its own, the second one only used when
		// the track is stereo.
		IAudioDevice& device = theServiceLocator().GetAudioDevice();
		device.stopPlayers();
		N64AudioDevice* n64Device = (device.isValid() ? static_cast<N64AudioDevice*>(&device) : nullptr);
		if (n64Device != nullptr) {
			audioChannel = n64Device->ReserveExclusiveChannels(2);
		}
#endif

		PlaybackContext context;
		fmv_parms_t parms = {};
		// The engine's display mode stays: the video is scaled into the 320x240 framebuffers with its aspect ratio
		// kept, and there is no black flash from tearing the display down and bringing it back
		parms.disable_display_init = true;
		parms.disable_subtitles = true;
		parms.disable_audio = (audioChannel < 0);
		parms.audio_mixer_channel = (audioChannel >= 0 ? audioChannel : 0);
		parms.osd_callback = &OnVideoFrame;
		parms.osd_ctx = &context;
		context.AudioChannel = audioChannel;

		if (audioChannel >= 0) {
			// fmv_play() starts the track on this channel and leaves its volume alone
			const float gain = (volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume));
			mixer_ch_set_vol(audioChannel, gain, gain);
		}

		static bool codecRegistered = false;
		if (!codecRegistered) {
			video_register_codec(&mpeg1_codec);
			codecRegistered = true;
		}

		String nullTerminatedPath = path;
		LOGI("Playing full-motion video \"{}\"", path);
		fmv_play(nullTerminatedPath.data(), &parms);
		// fmv_play() paces the video with the display's frame limiter and leaves it engaged; the engine runs
		// unlimited (display_get() at the frame rate the RDP allows), so a game after the intro was held to 24 fps
		display_set_fps_limit(0.0f);
		LOGI("Full-motion video \"{}\" {}", path, context.Skipped ? "skipped" : "finished");

#if defined(WITH_N64AUDIO)
		if (n64Device != nullptr && audioChannel >= 0) {
			n64Device->ReleaseExclusiveChannels(audioChannel, 2);
		}
#endif
		return !context.Skipped;
	}
}

#endif
