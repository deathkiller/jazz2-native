#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** @brief Well-known animation state */
	enum class AnimState
	{
		// Bits 0, 1: Horizontal speed (none, low, med, high)
		Idle = 0x00000000,					/**< Standing still */
		Walk = 0x00000001,					/**< Walking */
		Run = 0x00000002,					/**< Running */
		Dash = 0x00000003,					/**< Dashing */

		// Bits 2, 3: Vertical speed (none, upwards, downwards, suspended)
		VIdle = 0x00000000,					/**< No vertical movement */
		Jump = 0x00000004,					/**< Jumping (moving upwards) */
		Fall = 0x00000008,					/**< Falling (moving downwards) */
		Hook = 0x0000000c,					/**< Suspended (e.g. on a hook) */

		// Bit 4: Shoot
		Shoot = 0x00000010,					/**< Shooting */

		// Bits 5-9: Multiple special stances that cannot occur together
		// but still have unique bits due to complications in determining
		// the current actor state
		Crouch = 0x00000020,				/**< Crouching */
		Lookup = 0x00000040,				/**< Looking up */
		Dizzy = 0x00000080,					/**< Dizzy */
		Buttstomp = 0x00000100,				/**< Buttstomping */
		Uppercut = 0x00000200,				/**< Uppercut */
		Airboard = 0x00000400,				/**< Riding the airboard */
		Hurt = 0x00000800,					/**< Hurt */
		Swim = 0x00001000,					/**< Swimming */
		Copter = 0x00002000,				/**< Using the copter */
		Push = 0x00004000,					/**< Pushing an object */
		Swing = 0x00008000,					/**< Swinging on a vine */

		Freefall = 0x00010000,				/**< Free-falling */
		Lift = 0x00020000,					/**< Being lifted */
		Spring = 0x0040000,					/**< Launched by a spring */

		// 30th bit: Transition range
		TransitionRunToIdle = 0x40000000,				/**< Transition from running to idle */
		TransitionRunToDash = 0x40000001,				/**< Transition from running to dashing */
		TransitionFallToIdle = 0x40000002,				/**< Transition from falling to idle */
		TransitionIdleToJump = 0x40000003,				/**< Transition from idle to jumping */
		TransitionShootToIdle = 0x40000004,				/**< Transition from shooting to idle */
		TransitionHookShootToHook = 0x40000005,			/**< Transition from shooting on a hook back to hanging */
		TransitionCopterShootToCopter = 0x40000006,		/**< Transition from shooting with the copter back to the copter */
		TransitionUppercutA = 0x40000007,				/**< First part of the uppercut */
		TransitionUppercutB = 0x40000008,				/**< Second part of the uppercut */
		TransitionUppercutEnd = 0x40000009,				/**< End of the uppercut */
		TransitionButtstompStart = 0x4000000A,			/**< Start of the buttstomp */
		TransitionPoleH = 0x4000000B,					/**< Spinning around a horizontal pole */
		TransitionPoleV = 0x4000000C,					/**< Spinning around a vertical pole */
		TransitionPoleHSlow = 0x4000000D,				/**< Slow spinning around a horizontal pole */
		TransitionPoleVSlow = 0x4000000E,				/**< Slow spinning around a vertical pole */
		TransitionDeath = 0x4000000F,					/**< Death animation */
		TransitionTurn = 0x40000010,					/**< Turning around */
		//0x40000011
		//0x40000012
		TransitionWarpIn = 0x40000013,					/**< Warping in */
		TransitionWarpOut = 0x40000014,					/**< Warping out */
		//0x40000015
		TransitionEndOfLevel = 0x40000016,				/**< End-of-level animation */

		TransitionWarpInFreefall = 0x40000017,			/**< Warping in while free-falling */
		TransitionWarpOutFreefall = 0x40000018,			/**< Warping out while free-falling */

		TransitionIdleBored = 0x40000019,				/**< Bored idle animation */

		TransitionDashToIdle = 0x40000020,				/**< Transition from dashing to idle */
		TransitionIdleToShoot = 0x40000021,				/**< Transition from idle to shooting */
		TransitionButtstompEnd = 0x40000022,			/**< End of the buttstomp */

		TransitionLiftStart = 0x40000023,				/**< Start of being lifted */
		TransitionLiftEnd = 0x40000024,					/**< End of being lifted */

		TransitionLedge = 0x40000025,					/**< Hanging on a ledge */
		TransitionLedgeClimb = 0x40000026,				/**< Climbing up a ledge */

		TransitionFallShootToFall = 0x40000030,			/**< Transition from shooting while falling back to falling */

		TransitionFromFrog = 0x40000040,				/**< Transition reverting from the frog morph */

		// Aliases for common object states overlapping player states
		Activated = 0x00000020,							/**< Object is activated */
		TransitionActivate = 0x4F000000,				/**< Object activation animation */
		TransitionAttack = 0x4F000001,					/**< Object attack animation */
		TransitionAttackEnd = 0x4F000002,				/**< End of the object attack animation */

		Uninitialized = -1,								/**< Uninitialized/unspecified state */
		Default = 0										/**< Default state */
	};

	DEATH_ENUM_FLAGS(AnimState);
}