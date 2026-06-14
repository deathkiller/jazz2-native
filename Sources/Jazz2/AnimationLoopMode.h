#pragma once

namespace Jazz2
{
	/**
		@brief Animation loop mode
		
		Determines how an animation advances once it reaches its last frame. Stored per @ref Resources::GraphicResource
		and used by the animation playback to decide whether to stop on the last frame, restart from the first one, or
		keep showing a single fixed frame.
	*/
	enum class AnimationLoopMode
	{
		/** @brief The animation is played once and then remains in its last frame */
		Once,
		/** @brief The animation is looped --- when reaching the last frame, it begins again at the first one */
		Loop,
		/** @brief A fixed, single frame is displayed */
		FixedSingle
	};
}