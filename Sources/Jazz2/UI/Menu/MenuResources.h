#pragma once

#include "../../AnimState.h"
#include "../../PreferencesCache.h"
#include "../../../nCine/Input/InputEvents.h"

namespace Jazz2::UI::Menu::Resources
{
	/** @{ @name Constants */

	/** @brief Animated menu carrot logo */
	static constexpr AnimState MenuCarrot = (AnimState)0;
	/** @brief Falling snow background effect */
	static constexpr AnimState Snow = (AnimState)1;
	/** @brief Decorative menu line */
	static constexpr AnimState MenuLine = (AnimState)2;
	/** @brief Decorative menu line with an arrow */
	static constexpr AnimState MenuLineArrow = (AnimState)3;
	/** @brief Dimming overlay */
	static constexpr AnimState MenuDim = (AnimState)4;
	/** @brief Underglow effect */
	static constexpr AnimState MenuGlow = (AnimState)5;
	/** @brief Episode complete banner */
	static constexpr AnimState EpisodeComplete = (AnimState)10;
	/** @brief Jazz difficulty selection portrait */
	static constexpr AnimState MenuDifficultyJazz = (AnimState)11;
	/** @brief Spaz difficulty selection portrait */
	static constexpr AnimState MenuDifficultySpaz = (AnimState)12;
	/** @brief Lori difficulty selection portrait */
	static constexpr AnimState MenuDifficultyLori = (AnimState)13;
	/** @brief UAC import icon */
	static constexpr AnimState Uac = (AnimState)20;
	/** @brief Storage icon */
	static constexpr AnimState Storage = (AnimState)21;
	/** @brief Show on-screen keyboard icon */
	static constexpr AnimState ShowKeyboard = (AnimState)22;
	/** @brief Tombstone icon used on the highscore screen */
	static constexpr AnimState RestInPeace = (AnimState)23;

	/** @brief Red gem pickup icon */
	static constexpr AnimState PickupGemRed = (AnimState)30;
	/** @brief Green gem pickup icon */
	static constexpr AnimState PickupGemGreen = (AnimState)31;
	/** @brief Blue gem pickup icon */
	static constexpr AnimState PickupGemBlue = (AnimState)32;
	/** @brief Purple gem pickup icon */
	static constexpr AnimState PickupGemPurple = (AnimState)33;
	/** @brief Stopwatch pickup icon */
	static constexpr AnimState PickupStopwatch = (AnimState)34;

	/** @brief Xbox gamepad A button icon */
	static constexpr AnimState GamepadXboxA = (AnimState)40;
	/** @brief Xbox gamepad B button icon */
	static constexpr AnimState GamepadXboxB = (AnimState)41;
	/** @brief Xbox gamepad X button icon */
	static constexpr AnimState GamepadXboxX = (AnimState)42;
	/** @brief Xbox gamepad Y button icon */
	static constexpr AnimState GamepadXboxY = (AnimState)43;
	/** @brief Xbox gamepad left D-pad button icon */
	static constexpr AnimState GamepadXboxDPadLeft = (AnimState)44;
	/** @brief Xbox gamepad right D-pad button icon */
	static constexpr AnimState GamepadXboxDPadRight = (AnimState)45;
	/** @brief Xbox gamepad up D-pad button icon */
	static constexpr AnimState GamepadXboxDPadUp = (AnimState)46;
	/** @brief Xbox gamepad down D-pad button icon */
	static constexpr AnimState GamepadXboxDPadDown = (AnimState)47;
	/** @brief Xbox gamepad Guide button icon */
	static constexpr AnimState GamepadXboxGuide = (AnimState)48;
	/** @brief Xbox gamepad Back button icon */
	static constexpr AnimState GamepadXboxBack = (AnimState)49;
	/** @brief Xbox gamepad Start button icon */
	static constexpr AnimState GamepadXboxStart = (AnimState)50;
	/** @brief Xbox gamepad left shoulder button icon */
	static constexpr AnimState GamepadXboxLeftShoulder = (AnimState)51;
	/** @brief Xbox gamepad left stick button icon */
	static constexpr AnimState GamepadXboxLeftStick = (AnimState)52;
	/** @brief Xbox gamepad left trigger icon */
	static constexpr AnimState GamepadXboxLeftTrigger = (AnimState)53;
	/** @brief Xbox gamepad right shoulder button icon */
	static constexpr AnimState GamepadXboxRightShoulder = (AnimState)54;
	/** @brief Xbox gamepad right stick button icon */
	static constexpr AnimState GamepadXboxRightStick = (AnimState)55;
	/** @brief Xbox gamepad right trigger icon */
	static constexpr AnimState GamepadXboxRightTrigger = (AnimState)56;
	/** @brief Xbox gamepad miscellaneous button icon */
	static constexpr AnimState GamepadXboxMisc1 = (AnimState)57;

	/** @brief PlayStation gamepad A button icon */
	static constexpr AnimState GamepadPsA = (AnimState)58;
	/** @brief PlayStation gamepad B button icon */
	static constexpr AnimState GamepadPsB = (AnimState)59;
	/** @brief PlayStation gamepad X button icon */
	static constexpr AnimState GamepadPsX = (AnimState)60;
	/** @brief PlayStation gamepad Y button icon */
	static constexpr AnimState GamepadPsY = (AnimState)61;
	/** @brief PlayStation gamepad left D-pad button icon */
	static constexpr AnimState GamepadPsDPadLeft = (AnimState)62;
	/** @brief PlayStation gamepad right D-pad button icon */
	static constexpr AnimState GamepadPsDPadRight = (AnimState)63;
	/** @brief PlayStation gamepad up D-pad button icon */
	static constexpr AnimState GamepadPsDPadUp = (AnimState)64;
	/** @brief PlayStation gamepad down D-pad button icon */
	static constexpr AnimState GamepadPsDPadDown = (AnimState)65;
	/** @brief PlayStation gamepad Guide button icon */
	static constexpr AnimState GamepadPsGuide = (AnimState)66;
	/** @brief PlayStation gamepad Back button icon */
	static constexpr AnimState GamepadPsBack = (AnimState)67;
	/** @brief PlayStation gamepad Start button icon */
	static constexpr AnimState GamepadPsStart = (AnimState)68;
	/** @brief PlayStation gamepad left shoulder button icon */
	static constexpr AnimState GamepadPsLeftShoulder = (AnimState)69;
	/** @brief PlayStation gamepad left trigger icon */
	static constexpr AnimState GamepadPsLeftTrigger = (AnimState)70;
	/** @brief PlayStation gamepad right shoulder button icon */
	static constexpr AnimState GamepadPsRightShoulder = (AnimState)71;
	/** @brief PlayStation gamepad right trigger icon */
	static constexpr AnimState GamepadPsRightTrigger = (AnimState)72;
	/** @brief PlayStation gamepad miscellaneous button icon */
	static constexpr AnimState GamepadPsMisc1 = (AnimState)73;
	/** @brief PlayStation gamepad touchpad icon */
	static constexpr AnimState GamepadPsTouchpad = (AnimState)74;

	/** @brief Switch gamepad A button icon */
	static constexpr AnimState GamepadSwitchA = (AnimState)75;
	/** @brief Switch gamepad B button icon */
	static constexpr AnimState GamepadSwitchB = (AnimState)76;
	/** @brief Switch gamepad X button icon */
	static constexpr AnimState GamepadSwitchX = (AnimState)77;
	/** @brief Switch gamepad Y button icon */
	static constexpr AnimState GamepadSwitchY = (AnimState)78;
	/** @brief Switch gamepad left D-pad button icon */
	static constexpr AnimState GamepadSwitchDPadLeft = (AnimState)79;
	/** @brief Switch gamepad right D-pad button icon */
	static constexpr AnimState GamepadSwitchDPadRight = (AnimState)80;
	/** @brief Switch gamepad up D-pad button icon */
	static constexpr AnimState GamepadSwitchDPadUp = (AnimState)81;
	/** @brief Switch gamepad down D-pad button icon */
	static constexpr AnimState GamepadSwitchDPadDown = (AnimState)82;
	/** @brief Switch gamepad Guide button icon */
	static constexpr AnimState GamepadSwitchGuide = (AnimState)83;
	/** @brief Switch gamepad Back button icon */
	static constexpr AnimState GamepadSwitchBack = (AnimState)84;
	/** @brief Switch gamepad Start button icon */
	static constexpr AnimState GamepadSwitchStart = (AnimState)85;
	/** @brief Switch gamepad left trigger icon */
	static constexpr AnimState GamepadSwitchLeftTrigger = (AnimState)86;
	/** @brief Switch gamepad right trigger icon */
	static constexpr AnimState GamepadSwitchRightTrigger = (AnimState)87;
	/** @brief Switch gamepad miscellaneous button icon */
	static constexpr AnimState GamepadSwitchMisc1 = (AnimState)88;

	/** @brief Steam gamepad A button icon */
	static constexpr AnimState GamepadSteamA = (AnimState)89;
	/** @brief Steam gamepad B button icon */
	static constexpr AnimState GamepadSteamB = (AnimState)90;
	/** @brief Steam gamepad X button icon */
	static constexpr AnimState GamepadSteamX = (AnimState)91;
	/** @brief Steam gamepad Y button icon */
	static constexpr AnimState GamepadSteamY = (AnimState)92;
	/** @brief Steam gamepad left D-pad button icon */
	static constexpr AnimState GamepadSteamDPadLeft = (AnimState)93;
	/** @brief Steam gamepad right D-pad button icon */
	static constexpr AnimState GamepadSteamDPadRight = (AnimState)94;
	/** @brief Steam gamepad up D-pad button icon */
	static constexpr AnimState GamepadSteamDPadUp = (AnimState)95;
	/** @brief Steam gamepad down D-pad button icon */
	static constexpr AnimState GamepadSteamDPadDown = (AnimState)96;
	/** @brief Steam gamepad Guide button icon */
	static constexpr AnimState GamepadSteamGuide = (AnimState)97;
	/** @brief Steam gamepad Back button icon */
	static constexpr AnimState GamepadSteamBack = (AnimState)98;
	/** @brief Steam gamepad Start button icon */
	static constexpr AnimState GamepadSteamStart = (AnimState)99;
	/** @brief Steam gamepad left shoulder button icon */
	static constexpr AnimState GamepadSteamLeftShoulder = (AnimState)100;
	/** @brief Steam gamepad right shoulder button icon */
	static constexpr AnimState GamepadSteamRightShoulder = (AnimState)101;
	/** @brief Steam gamepad miscellaneous button icon */
	static constexpr AnimState GamepadSteamMisc1 = (AnimState)102;

	/** @brief 16×16 application icon */
	static constexpr AnimState Menu16 = (AnimState)110;
	/** @brief 32×32 application icon */
	static constexpr AnimState Menu32 = (AnimState)111;
	/** @brief 128×128 application icon */
	static constexpr AnimState Menu128 = (AnimState)112;

	/** @brief Resource used to check whether Lori graphics are available */
	static constexpr AnimState LoriExistsCheck = (AnimState)120;

	/** @brief Touch controls D-pad icon */
	static constexpr AnimState TouchDpad = (AnimState)130;
	/** @brief Touch controls Fire button icon */
	static constexpr AnimState TouchFire = (AnimState)131;
	/** @brief Touch controls Jump button icon */
	static constexpr AnimState TouchJump = (AnimState)132;
	/** @brief Touch controls Run button icon */
	static constexpr AnimState TouchRun = (AnimState)133;
	/** @brief Touch controls Change Weapon button icon */
	static constexpr AnimState TouchChange = (AnimState)134;
	/** @brief Touch controls Pause button icon */
	static constexpr AnimState TouchPause = (AnimState)135;
	/** @brief Touch controls Close button icon */
	static constexpr AnimState TouchClose = (AnimState)136;

	/** @} */

	/** @brief Returns animation resource for the specified gamepad axis */
	inline AnimState GetResourceForAxisName(AxisName axis, Containers::StringView& axisName)
	{
		switch (axis) {
			case AxisName::LeftX: axisName = "X"_s; return GamepadXboxLeftStick; break;
			case AxisName::LeftY: axisName = "Y"_s; return GamepadXboxLeftStick; break;
			case AxisName::RightX: axisName = "X"_s; return GamepadXboxRightStick; break;
			case AxisName::RightY: axisName = "Y"_s; return GamepadXboxRightStick; break;

			case AxisName::LeftTrigger:
				switch (PreferencesCache::GamepadButtonLabels) {
					default:
					case GamepadType::Xbox:
						return GamepadXboxLeftTrigger;
					case GamepadType::PlayStation:
					case GamepadType::Steam:
						return GamepadPsLeftTrigger;
					case GamepadType::Switch:
						return GamepadSwitchLeftTrigger;
				}
			case AxisName::RightTrigger:
				switch (PreferencesCache::GamepadButtonLabels) {
					default:
					case GamepadType::Xbox:
						return GamepadXboxRightTrigger;
					case GamepadType::PlayStation:
					case GamepadType::Steam:
						return GamepadPsRightTrigger;
					case GamepadType::Switch:
						return GamepadSwitchRightTrigger;
				}

			default: return AnimState::Default; break;
		}
	}

	/** @brief Returns animation resource for the specified gamepad button */
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