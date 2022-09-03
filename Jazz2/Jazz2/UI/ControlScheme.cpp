#include "ControlScheme.h"

namespace Jazz2::UI
{
	ControlSchemeMapping ControlScheme::_mappings[MaxSupportedPlayers * (int)PlayerActions::Count] { };

	void ControlScheme::Initialize()
	{
		// Set default mappings
		_mappings[(int)PlayerActions::Left].Key1 = KeySym::LEFT;
		_mappings[(int)PlayerActions::Right].Key1 = KeySym::RIGHT;
		_mappings[(int)PlayerActions::Up].Key1 = KeySym::UP;
		_mappings[(int)PlayerActions::Down].Key1 = KeySym::DOWN;
		_mappings[(int)PlayerActions::Fire].Key1 = KeySym::SPACE;
		_mappings[(int)PlayerActions::Jump].Key1 = KeySym::V;
		_mappings[(int)PlayerActions::Run].Key1 = KeySym::C;
		_mappings[(int)PlayerActions::SwitchWeapon].Key1 = KeySym::X;
		_mappings[(int)PlayerActions::Menu].Key1 = KeySym::ESCAPE;

		_mappings[(int)PlayerActions::Left].GamepadButton = ButtonName::DPAD_LEFT;
		_mappings[(int)PlayerActions::Right].GamepadButton = ButtonName::DPAD_RIGHT;
		_mappings[(int)PlayerActions::Up].GamepadButton = ButtonName::DPAD_UP;
		_mappings[(int)PlayerActions::Down].GamepadButton = ButtonName::DPAD_DOWN;
		_mappings[(int)PlayerActions::Fire].GamepadButton = ButtonName::X;
		_mappings[(int)PlayerActions::Jump].GamepadButton = ButtonName::A;
		_mappings[(int)PlayerActions::Run].GamepadButton = ButtonName::B;
		_mappings[(int)PlayerActions::SwitchWeapon].GamepadButton = ButtonName::Y;
		_mappings[(int)PlayerActions::Menu].GamepadButton = ButtonName::START;

		for (int i = 0; i < (int)PlayerActions::Count; i++) {
			_mappings[i].Key2 = KeySym::UNKNOWN;
		}

		// Unmap all other players by default
		for (int i = (int)PlayerActions::Count; i < MaxSupportedPlayers * (int)PlayerActions::Count; i++) {
			_mappings[i].Key1 = KeySym::UNKNOWN;
			_mappings[i].Key2 = KeySym::UNKNOWN;
			_mappings[i].GamepadIndex = -1;
		}
	}

	void ControlScheme::Save()
	{
		// TODO: Save to file
	}
}