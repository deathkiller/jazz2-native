#pragma once

namespace Jazz2
{
	/** @brief Animation loop mode */
	enum class AnimationLoopMode
	{
		/** @brief The animation is played once an then remains in its last frame */
		Once,
		/** @brief The animation is looped: When reaching the last frame, it begins again at the first one */
		Loop,
		/** @brief A fixed, single frame is displayed */
		FixedSingle
	};
}