#include "ControlScheme.h"
#include "../PreferencesCache.h"

namespace Jazz2::UI
{
	ControlSchemeMapping ControlScheme::_mappings[MaxSupportedPlayers * (std::int32_t)PlayerActions::Count] { };

	void ControlScheme::Reset()
	{
		// Set default mappings
		_mappings[(std::int32_t)PlayerActions::Up].Key1 = KeySym::UP;
		_mappings[(std::int32_t)PlayerActions::Down].Key1 = KeySym::DOWN;
		_mappings[(std::int32_t)PlayerActions::Left].Key1 = KeySym::LEFT;
		_mappings[(std::int32_t)PlayerActions::Right].Key1 = KeySym::RIGHT;
		_mappings[(std::int32_t)PlayerActions::Fire].Key1 = KeySym::SPACE;
		_mappings[(std::int32_t)PlayerActions::Jump].Key1 = KeySym::V;
		_mappings[(std::int32_t)PlayerActions::Run].Key1 = KeySym::C;
		_mappings[(std::int32_t)PlayerActions::ChangeWeapon].Key1 = KeySym::X;
		_mappings[(std::int32_t)PlayerActions::Menu].Key1 = KeySym::ESCAPE;

		_mappings[(std::int32_t)PlayerActions::Up].GamepadButton = ButtonName::DPAD_UP;
		_mappings[(std::int32_t)PlayerActions::Down].GamepadButton = ButtonName::DPAD_DOWN;
		_mappings[(std::int32_t)PlayerActions::Left].GamepadButton = ButtonName::DPAD_LEFT;
		_mappings[(std::int32_t)PlayerActions::Right].GamepadButton = ButtonName::DPAD_RIGHT;
		_mappings[(std::int32_t)PlayerActions::Fire].GamepadButton = ButtonName::X;
		_mappings[(std::int32_t)PlayerActions::Jump].GamepadButton = ButtonName::A;
		_mappings[(std::int32_t)PlayerActions::Run].GamepadButton = ButtonName::B;
		_mappings[(std::int32_t)PlayerActions::ChangeWeapon].GamepadButton = ButtonName::Y;
		_mappings[(std::int32_t)PlayerActions::Menu].GamepadButton = ButtonName::START;

		for (std::int32_t i = 0; i < (std::int32_t)PlayerActions::Count; i++) {
			_mappings[i].Key2 = KeySym::UNKNOWN;
		}

		// Unmap all other players by default
		for (std::int32_t i = (std::int32_t)PlayerActions::Count; i < MaxSupportedPlayers * (std::int32_t)PlayerActions::Count; i++) {
			_mappings[i].Key1 = KeySym::UNKNOWN;
			_mappings[i].Key2 = KeySym::UNKNOWN;
			_mappings[i].GamepadIndex = -1;
		}
	}
}