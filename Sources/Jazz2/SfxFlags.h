#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Sound effect flags

		Options controlling how a sound effect attached to an actor behaves, mainly on the remote peers of an online
		session, which only ever receive a request to play it. @ref SfxFlags::Exclusive lets an actor keep at most
		one instance of a sound playing everywhere, and lets it be cut off again with @ref ILevelHandler::StopSfx().
		@ref SfxFlags::Looping makes the peers loop it, which is only meaningful together with it, since a looping
		sound has to be stopped. Supports a bitwise combination of its member values.
	*/
	enum class SfxFlags
	{
		None = 0,					/**< Plain one-shot sound */

		Exclusive = 0x01,			/**< Replaces the previous instance of the same sound from the same actor, follows the actor while it plays */
		Looping = 0x02				/**< Loops until stopped, use together with @ref SfxFlags::Exclusive */
	};

	DEATH_ENUM_FLAGS(SfxFlags);
}
