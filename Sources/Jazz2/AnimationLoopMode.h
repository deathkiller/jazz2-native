#pragma once

namespace Jazz2
{
	/** @brief Animation loop mode */
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