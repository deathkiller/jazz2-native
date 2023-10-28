#pragma once

#include "../Common.h"

namespace Jazz2
{
	enum class AnimState
	{
		// Bits 0, 1: Horizontal speed (none, low, med, high)
		Idle = 0x00000000,
		Walk = 0x00000001,
		Run = 0x00000002,
		Dash = 0x00000003,

		// Bits 2, 3: Vertical speed (none, upwards, downwards, suspended)
		VIdle = 0x00000000,
		Jump = 0x00000004,
		Fall = 0x00000008,
		Hook = 0x0000000c,

		// Bit 4: Shoot
		Shoot = 0x00000010,

		// Bits 5-9: Multiple special stances that cannot occur together
		// but still have unique bits due to complications in determining
		// the current actor state
		Crouch = 0x00000020,
		Lookup = 0x00000040,
		Dizzy = 0x00000080,
		Buttstomp = 0x00000100,
		Uppercut = 0x00000200,
		Airboard = 0x00000400,
		Hurt = 0x00000800,
		Swim = 0x00001000,
		Copter = 0x00002000,
		Push = 0x00004000,
		Swing = 0x00008000,

		Freefall = 0x00010000,
		Lift = 0x00020000,
		Spring = 0x0040000,

		// 30th bit: Transition range
		TransitionRunToIdle = 0x40000000,
		TransitionRunToDash = 0x40000001,
		TransitionFallToIdle = 0x40000002,
		TransitionIdleToJump = 0x40000003,
		TransitionShootToIdle = 0x40000004,
		TransitionHookShootToHook = 0x40000005,
		TransitionCopterShootToCopter = 0x40000006,
		TransitionUppercutA = 0x40000007,
		TransitionUppercutB = 0x40000008,
		TransitionUppercutEnd = 0x40000009,
		TransitionButtstompStart = 0x4000000A,
		TransitionPoleH = 0x4000000B,
		TransitionPoleV = 0x4000000C,
		TransitionPoleHSlow = 0x4000000D,
		TransitionPoleVSlow = 0x4000000E,
		TransitionDeath = 0x4000000F,
		TransitionTurn = 0x40000010,
		TransitionWithdraw = 0x40000011,
		TransitionWithdrawEnd = 0x40000012,
		TransitionWarpIn = 0x40000013,
		TransitionWarpOut = 0x40000014,
		//0x40000015
		TransitionEndOfLevel = 0x40000016,

		TransitionWarpInFreefall = 0x40000017,
		TransitionWarpOutFreefall = 0x40000018,

		TransitionIdleBored = 0x40000019,

		TransitionDashToIdle = 0x40000020,
		TransitionIdleToShoot = 0x40000021,
		TransitionButtstompEnd = 0x40000022,

		TransitionLiftStart = 0x40000023,
		TransitionLiftEnd = 0x40000024,

		TransitionLedge = 0x40000025,
		TransitionLedgeClimb = 0x40000026,

		TransitionFallShootToFall = 0x40000030,

		TransitionFromFrog = 0x40000040,

		// Aliases for common object states overlapping player states
		Activated = 0x00000020,
		TransitionActivate = 0x4F000000,
		TransitionAttack = 0x4F000001,
		TransitionAttackEnd = 0x4F000002,

		Uninitialized = -1,
		Default = 0
	};

	DEFINE_ENUM_OPERATORS(AnimState);
}