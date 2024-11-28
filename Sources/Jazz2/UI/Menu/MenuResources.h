#pragma once

#include "../../AnimState.h"
#include "../../PreferencesCache.h"
#include "../../../nCine/Input/InputEvents.h"

namespace Jazz2::UI::Menu::Resources
{
	static constexpr AnimState MenuCarrot = (AnimState)0;
	static constexpr AnimState Snow = (AnimState)1;
	static constexpr AnimState MenuLine = (AnimState)2;
	static constexpr AnimState MenuLineArrow = (AnimState)3;
	static constexpr AnimState MenuDim = (AnimState)4;
	static constexpr AnimState MenuGlow = (AnimState)5;
	static constexpr AnimState EpisodeComplete = (AnimState)10;
	static constexpr AnimState MenuDifficultyJazz = (AnimState)11;
	static constexpr AnimState MenuDifficultySpaz = (AnimState)12;
	static constexpr AnimState MenuDifficultyLori = (AnimState)13;
	static constexpr AnimState Uac = (AnimState)20;
	static constexpr AnimState Storage = (AnimState)21;
	static constexpr AnimState ShowKeyboard = (AnimState)22;
	static constexpr AnimState RestInPeace = (AnimState)23;

	static constexpr AnimState PickupGemRed = (AnimState)30;
	static constexpr AnimState PickupGemGreen = (AnimState)31;
	static constexpr AnimState PickupGemBlue = (AnimState)32;
	static constexpr AnimState PickupGemPurple = (AnimState)33;
	static constexpr AnimState PickupStopwatch = (AnimState)34;

	static constexpr AnimState GamepadXboxA = (AnimState)40;
	static constexpr AnimState GamepadXboxB = (AnimState)41;
	static constexpr AnimState GamepadXboxX = (AnimState)42;
	static constexpr AnimState GamepadXboxY = (AnimState)43;
	static constexpr AnimState GamepadXboxDPadLeft = (AnimState)44;
	static constexpr AnimState GamepadXboxDPadRight = (AnimState)45;
	static constexpr AnimState GamepadXboxDPadUp = (AnimState)46;
	static constexpr AnimState GamepadXboxDPadDown = (AnimState)47;
	static constexpr AnimState GamepadXboxGuide = (AnimState)48;
	static constexpr AnimState GamepadXboxBack = (AnimState)49;
	static constexpr AnimState GamepadXboxStart = (AnimState)50;
	static constexpr AnimState GamepadXboxLeftShoulder = (AnimState)51;
	static constexpr AnimState GamepadXboxLeftStick = (AnimState)52;
	static constexpr AnimState GamepadXboxLeftTrigger = (AnimState)53;
	static constexpr AnimState GamepadXboxRightShoulder = (AnimState)54;
	static constexpr AnimState GamepadXboxRightStick = (AnimState)55;
	static constexpr AnimState GamepadXboxRightTrigger = (AnimState)56;
	static constexpr AnimState GamepadXboxMisc1 = (AnimState)57;

	static constexpr AnimState GamepadPsA = (AnimState)58;
	static constexpr AnimState GamepadPsB = (AnimState)59;
	static constexpr AnimState GamepadPsX = (AnimState)60;
	static constexpr AnimState GamepadPsY = (AnimState)61;
	static constexpr AnimState GamepadPsDPadLeft = (AnimState)62;
	static constexpr AnimState GamepadPsDPadRight = (AnimState)63;
	static constexpr AnimState GamepadPsDPadUp = (AnimState)64;
	static constexpr AnimState GamepadPsDPadDown = (AnimState)65;
	static constexpr AnimState GamepadPsGuide = (AnimState)66;
	static constexpr AnimState GamepadPsBack = (AnimState)67;
	static constexpr AnimState GamepadPsStart = (AnimState)68;
	static constexpr AnimState GamepadPsLeftShoulder = (AnimState)69;
	static constexpr AnimState GamepadPsRightShoulder = (AnimState)70;
	static constexpr AnimState GamepadPsMisc1 = (AnimState)71;
	static constexpr AnimState GamepadPsTouchpad = (AnimState)72;

	static constexpr AnimState GamepadSwitchA = (AnimState)73;
	static constexpr AnimState GamepadSwitchB = (AnimState)74;
	static constexpr AnimState GamepadSwitchX = (AnimState)75;
	static constexpr AnimState GamepadSwitchY = (AnimState)76;
	static constexpr AnimState GamepadSwitchDPadLeft = (AnimState)77;
	static constexpr AnimState GamepadSwitchDPadRight = (AnimState)78;
	static constexpr AnimState GamepadSwitchDPadUp = (AnimState)79;
	static constexpr AnimState GamepadSwitchDPadDown = (AnimState)80;
	static constexpr AnimState GamepadSwitchGuide = (AnimState)81;
	static constexpr AnimState GamepadSwitchBack = (AnimState)82;
	static constexpr AnimState GamepadSwitchStart = (AnimState)83;
	static constexpr AnimState GamepadSwitchLeftTrigger = (AnimState)84;
	static constexpr AnimState GamepadSwitchRightTrigger = (AnimState)85;
	static constexpr AnimState GamepadSwitchMisc1 = (AnimState)86;

	static constexpr AnimState GamepadSteamA = (AnimState)87;
	static constexpr AnimState GamepadSteamB = (AnimState)88;
	static constexpr AnimState GamepadSteamX = (AnimState)89;
	static constexpr AnimState GamepadSteamY = (AnimState)90;
	static constexpr AnimState GamepadSteamDPadLeft = (AnimState)91;
	static constexpr AnimState GamepadSteamDPadRight = (AnimState)92;
	static constexpr AnimState GamepadSteamDPadUp = (AnimState)93;
	static constexpr AnimState GamepadSteamDPadDown = (AnimState)94;
	static constexpr AnimState GamepadSteamGuide = (AnimState)95;
	static constexpr AnimState GamepadSteamBack = (AnimState)96;
	static constexpr AnimState GamepadSteamStart = (AnimState)97;
	static constexpr AnimState GamepadSteamLeftShoulder = (AnimState)98;
	static constexpr AnimState GamepadSteamRightShoulder = (AnimState)99;
	static constexpr AnimState GamepadSteamMisc1 = (AnimState)100;

	static constexpr AnimState Menu16 = (AnimState)110;
	static constexpr AnimState Menu32 = (AnimState)111;
	static constexpr AnimState Menu128 = (AnimState)112;

	static constexpr AnimState LoriExistsCheck = (AnimState)120;

	inline AnimState GetResourceForAxisName(AxisName axis, Containers::StringView& axisName)
	{
		switch (axis) {
			case AxisName::LeftX: axisName = "X"_s; return GamepadXboxLeftStick; break;
			case AxisName::LeftY: axisName = "Y"_s; return GamepadXboxLeftStick; break;
			case AxisName::RightX: axisName = "X"_s; return GamepadXboxRightStick; break;
			case AxisName::RightY: axisName = "Y"_s; return GamepadXboxRightStick; break;
			case AxisName::LeftTrigger: return (PreferencesCache::GamepadButtonLabels == GamepadType::Switch ? GamepadSwitchLeftTrigger : GamepadXboxLeftTrigger); break;
			case AxisName::RightTrigger: return (PreferencesCache::GamepadButtonLabels == GamepadType::Switch ? GamepadSwitchRightTrigger : GamepadXboxRightTrigger); break;
			default: return AnimState::Default; break;
		}
	}

	inline AnimState GetResourceForButtonName(ButtonName button)
	{
		switch (PreferencesCache::GamepadButtonLabels) {
			default:
			case GamepadType::Xbox: {
				switch (button) {
					case ButtonName::A: return GamepadXboxA; break;
					case ButtonName::B: return GamepadXboxB; break;
					case ButtonName::X: return GamepadXboxX; break;
					case ButtonName::Y: return GamepadXboxY; break;
					case ButtonName::Back: return GamepadXboxBack; break;
					case ButtonName::Guide: return GamepadXboxGuide; break;
					case ButtonName::Start: return GamepadXboxStart; break;
					case ButtonName::LeftStick: return GamepadXboxLeftStick; break;
					case ButtonName::RightStick: return GamepadXboxRightStick; break;
					case ButtonName::LeftBumper: return GamepadXboxLeftShoulder; break;
					case ButtonName::RightBumper: return GamepadXboxRightShoulder; break;
					case ButtonName::Up: return GamepadXboxDPadUp; break;
					case ButtonName::Down: return GamepadXboxDPadDown; break;
					case ButtonName::Left: return GamepadXboxDPadLeft; break;
					case ButtonName::Right: return GamepadXboxDPadRight; break;
					case ButtonName::Misc1: return GamepadXboxMisc1; break;
					case ButtonName::Touchpad: return GamepadPsTouchpad; break;				// Not valid for Xbox - reuse PlayStation label
					default: return AnimState::Default; break;
				}
			}
			case GamepadType::PlayStation: {
				switch (button) {
					case ButtonName::A: return GamepadPsA; break;
					case ButtonName::B: return GamepadPsB; break;
					case ButtonName::X: return GamepadPsX; break;
					case ButtonName::Y: return GamepadPsY; break;
					case ButtonName::Back: return GamepadPsBack; break;
					case ButtonName::Guide: return GamepadPsGuide; break;
					case ButtonName::Start: return GamepadPsStart; break;
					case ButtonName::LeftStick: return GamepadXboxLeftStick; break;			// Reuse Xbox label
					case ButtonName::RightStick: return GamepadXboxRightStick; break;		// Reuse Xbox label
					case ButtonName::LeftBumper: return GamepadPsLeftShoulder; break;
					case ButtonName::RightBumper: return GamepadPsRightShoulder; break;
					case ButtonName::Up: return GamepadPsDPadUp; break;
					case ButtonName::Down: return GamepadPsDPadDown; break;
					case ButtonName::Left: return GamepadPsDPadLeft; break;
					case ButtonName::Right: return GamepadPsDPadRight; break;
					case ButtonName::Misc1: return GamepadPsMisc1; break;
					case ButtonName::Touchpad: return GamepadPsTouchpad; break;
					default: return AnimState::Default; break;
				}
			}
			case GamepadType::Steam: {
				switch (button) {
					case ButtonName::A: return GamepadSteamA; break;
					case ButtonName::B: return GamepadSteamB; break;
					case ButtonName::X: return GamepadSteamX; break;
					case ButtonName::Y: return GamepadSteamY; break;
					case ButtonName::Back: return GamepadSteamBack; break;
					case ButtonName::Guide: return GamepadSteamGuide; break;
					case ButtonName::Start: return GamepadSteamStart; break;
					case ButtonName::LeftStick: return GamepadXboxLeftStick; break;			// Reuse Xbox label
					case ButtonName::RightStick: return GamepadXboxRightStick; break;		// Reuse Xbox label
					case ButtonName::LeftBumper: return GamepadSteamLeftShoulder; break;
					case ButtonName::RightBumper: return GamepadSteamRightShoulder; break;
					case ButtonName::Up: return GamepadSteamDPadUp; break;
					case ButtonName::Down: return GamepadSteamDPadDown; break;
					case ButtonName::Left: return GamepadSteamDPadLeft; break;
					case ButtonName::Right: return GamepadSteamDPadRight; break;
					case ButtonName::Misc1: return GamepadSteamMisc1; break;
					case ButtonName::Touchpad: return GamepadPsTouchpad; break;				// Not valid for Steam - reuse PlayStation label
					default: return AnimState::Default; break;
				}
			}
			case GamepadType::Switch: {
				switch (button) {
					case ButtonName::A: return GamepadSwitchA; break;
					case ButtonName::B: return GamepadSwitchB; break;
					case ButtonName::X: return GamepadSwitchX; break;
					case ButtonName::Y: return GamepadSwitchY; break;
					case ButtonName::Back: return GamepadSwitchBack; break;
					case ButtonName::Guide: return GamepadSwitchGuide; break;
					case ButtonName::Start: return GamepadSwitchStart; break;
					case ButtonName::LeftStick: return GamepadXboxLeftStick; break;			// Reuse Xbox label
					case ButtonName::RightStick: return GamepadXboxRightStick; break;		// Reuse Xbox label
					case ButtonName::LeftBumper: return GamepadXboxLeftShoulder; break;		// Reuse Xbox label
					case ButtonName::RightBumper: return GamepadXboxRightShoulder; break;	// Reuse Xbox label
					case ButtonName::Up: return GamepadSwitchDPadUp; break;
					case ButtonName::Down: return GamepadSwitchDPadDown; break;
					case ButtonName::Left: return GamepadSwitchDPadLeft; break;
					case ButtonName::Right: return GamepadSwitchDPadRight; break;
					case ButtonName::Misc1: return GamepadSwitchMisc1; break;
					case ButtonName::Touchpad: return GamepadPsTouchpad; break;				// Not valid for Switch - reuse PlayStation label
					default: return AnimState::Default; break;
				}
			}
		}
	}
}