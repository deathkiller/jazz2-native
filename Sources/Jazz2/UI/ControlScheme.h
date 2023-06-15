#pragma once

#include "../PlayerActions.h"
#include "../../nCine/Input/InputEvents.h"

#include <Containers/ArrayView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::UI
{
	namespace Menu
	{
		class RemapControlsSection;
	}

	struct ControlSchemeMapping
	{
		KeySym Key1;
		KeySym Key2;

		int GamepadIndex;
		ButtonName GamepadButton;
	};

	class ControlScheme
	{
		friend class Menu::RemapControlsSection;

	public:
		static constexpr int32_t MaxSupportedPlayers = 1;
#if defined(DEATH_TARGET_SWITCH)
		// TODO: Game is crashing on Switch if more than 1 gamepad is used
		static constexpr int32_t MaxConnectedGamepads = 1;
#else
		static constexpr int32_t MaxConnectedGamepads = 4;
#endif

		static void Reset();

		static KeySym Key1(int32_t playerIndex, PlayerActions action)
		{
			return _mappings[playerIndex * (int32_t)PlayerActions::Count + (int)action].Key1;
		}

		static KeySym Key2(int32_t playerIndex, PlayerActions action)
		{
			return _mappings[playerIndex * (int32_t)PlayerActions::Count + (int)action].Key2;
		}

		static ButtonName Gamepad(int32_t playerIndex, PlayerActions action, int& gamepadIndex)
		{
			auto& mapping = _mappings[playerIndex * (int32_t)PlayerActions::Count + (int)action];
			gamepadIndex = mapping.GamepadIndex;
			return mapping.GamepadButton;
		}

		static ArrayView<ControlSchemeMapping> GetMappings()
		{
			return _mappings;
		}

	private:
		/// Deleted copy constructor
		ControlScheme(const ControlScheme&) = delete;
		/// Deleted assignment operator
		ControlScheme& operator=(const ControlScheme&) = delete;

		static ControlSchemeMapping _mappings[MaxSupportedPlayers * (int)PlayerActions::Count];
	};
}