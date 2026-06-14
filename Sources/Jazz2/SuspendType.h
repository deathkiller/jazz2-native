#pragma once

namespace Jazz2
{
	/**
		@brief Suspend type
		
		Describes what is currently holding a player off the ground, suspending normal gravity and movement --- a
		climbable vine, a hook, or a swinging vine --- with @ref SuspendType::None when the player is not suspended.
		Set when the player enters the corresponding modifier tile and read by player physics.
	*/
	enum class SuspendType
	{
		None,				/**< None */
		Vine,				/**< Vine */
		Hook,				/**< Hook */
		SwingingVine		/**< Swinging vine */
	};
}