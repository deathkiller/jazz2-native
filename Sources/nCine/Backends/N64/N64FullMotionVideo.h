#pragma once

#if defined(WITH_N64) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::Backends
{
	/**
		@brief Plays the Nintendo 64's full-motion video cinematics

		The asset packer re-encodes the game's cinematics for this console into an MPEG-1 stream libdragon's
		decoder plays with the RSP's help, with the cinematic's music and sound effects mixed
		into one `.wav64` audio track beside it. Playing that is libdragon's @ref fmv_play(), which owns the main
		thread for the length of the video - so this is a blocking call, made between two frames when the engine
		holds neither the display nor any RDP state (see @ref Play()).
	*/
	class N64FullMotionVideo
	{
	public:
		N64FullMotionVideo() = delete;

		/**
			@brief Returns the video prepared for a cinematic, or an empty string if the content has none

			@param name		Name of the cinematic, as passed to @ref Jazz2::UI::Cinematics
		*/
		static String FindVideo(StringView name);

		/**
			@brief Plays a video to its end or until a button skips it

			Must be called between frames: the engine's renderer has presented its last frame and not yet acquired
			the next display buffer, so libdragon can use the display and the RDP freely. Every sound the engine is
			playing is stopped first, and the audio track plays on mixer channels taken from the audio device for
			the duration.

			@param path		Path returned by @ref FindVideo()
			@param volume	Volume of the audio track, 0-1
			@returns `true` if the video played to its end, `false` if it was skipped
		*/
		static bool Play(StringView path, float volume);
	};
}

#endif
