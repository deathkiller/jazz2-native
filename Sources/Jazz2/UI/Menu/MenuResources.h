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

	static constexpr AnimState GamepadA = (AnimState)40;
	static constexpr AnimState GamepadB = (AnimState)41;
	static constexpr AnimState GamepadX = (AnimState)42;
	static constexpr AnimState GamepadY = (AnimState)43;
	static constexpr AnimState GamepadDPadLeft = (AnimState)44;
	static constexpr AnimState GamepadDPadRight = (AnimState)45;
	static constexpr AnimState GamepadDPadUp = (AnimState)46;
	static constexpr AnimState GamepadDPadDown = (AnimState)47;
	static constexpr AnimState GamepadGuide = (AnimState)48;
	static constexpr AnimState GamepadBack = (AnimState)49;
	static constexpr AnimState GamepadStart = (AnimState)50;
	static constexpr AnimState GamepadLeftShoulder = (AnimState)51;
	static constexpr AnimState GamepadLeftStick = (AnimState)52;
	static constexpr AnimState GamepadLeftTrigger = (AnimState)53;
	static constexpr AnimState GamepadRightShoulder = (AnimState)54;
	static constexpr AnimState GamepadRightStick = (AnimState)55;
	static constexpr AnimState GamepadRightTrigger = (AnimState)56;
	//static constexpr AnimState GamepadMisc1 = (AnimState)57;

	static constexpr AnimState GamepadAltA = (AnimState)58;
	static constexpr AnimState GamepadAltB = (AnimState)59;
	static constexpr AnimState GamepadAltX = (AnimState)60;
	static constexpr AnimState GamepadAltY = (AnimState)61;
	static constexpr AnimState GamepadAltDPadLeft = (AnimState)62;
	static constexpr AnimState GamepadAltDPadRight = (AnimState)63;
	static constexpr AnimState GamepadAltDPadUp = (AnimState)64;
	static constexpr AnimState GamepadAltDPadDown = (AnimState)65;
	static constexpr AnimState GamepadAltGuide = (AnimState)66;
	static constexpr AnimState GamepadAltBack = (AnimState)67;
	static constexpr AnimState GamepadAltStart = (AnimState)68;
	static constexpr AnimState GamepadAltMisc1 = (AnimState)69;

	static constexpr AnimState Menu16 = (AnimState)70;
	static constexpr AnimState Menu32 = (AnimState)71;
	static constexpr AnimState Menu128 = (AnimState)72;
	static constexpr AnimState LoriExistsCheck = (AnimState)80;

	inline AnimState GetResourceForAxisName(AxisName axis, Containers::StringView& axisName)
	{
		switch (axis) {
			case AxisName::LeftX: axisName = "X"_s; return GamepadLeftStick; break;
			case AxisName::LeftY: axisName = "Y"_s; return GamepadLeftStick; break;
			case AxisName::RightX: axisName = "X"_s; return GamepadRightStick; break;
			case AxisName::RightY: axisName = "Y"_s; return GamepadRightStick; break;
			case AxisName::LeftTrigger: return GamepadLeftTrigger; break;
			case AxisName::RightTrigger: return GamepadRightTrigger; break;
			default: return AnimState::Default; break;
		}
	}

	inline AnimState GetResourceForButtonName(ButtonName button)
	{
		bool alt = (PreferencesCache::GamepadButtonLabels != GamepadType::Xbox);
		switch (button) {
			case ButtonName::A: return alt ? GamepadAltA : GamepadA; break;
			case ButtonName::B: return alt ? GamepadAltB : GamepadB; break;
			case ButtonName::X: return alt ? GamepadAltX : GamepadX; break;
			case ButtonName::Y: return alt ? GamepadAltY : GamepadY; break;
			case ButtonName::Back: return alt ? GamepadAltBack : GamepadBack; break;
			case ButtonName::Guide: return alt ? GamepadAltGuide : GamepadGuide; break;
			case ButtonName::Start: return alt ? GamepadAltStart : GamepadStart; break;
			case ButtonName::LeftStick: return GamepadLeftStick; break;
			case ButtonName::RightStick: return GamepadRightStick; break;
			case ButtonName::LeftBumper: return GamepadLeftShoulder; break;
			case ButtonName::RightBumper: return GamepadRightShoulder; break;
			case ButtonName::Up: return alt ? GamepadAltDPadUp : GamepadDPadUp; break;
			case ButtonName::Down: return alt ? GamepadAltDPadDown : GamepadDPadDown; break;
			case ButtonName::Left: return alt ? GamepadAltDPadLeft : GamepadDPadLeft; break;
			case ButtonName::Right: return alt ? GamepadAltDPadRight : GamepadDPadRight; break;
			case ButtonName::Misc1: return GamepadAltMisc1; break;
			default: return AnimState::Default; break;
		}
	}
}