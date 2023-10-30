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

		std::int32_t GamepadIndex;
		ButtonName GamepadButton;
	};

	class ControlScheme
	{
		friend class Menu::RemapControlsSection;

	public:
		static constexpr std::int32_t MaxSupportedPlayers = 1;
#if defined(DEATH_TARGET_SWITCH)
		// TODO: Game is crashing on Switch if more than 1 gamepad is used
		static constexpr std::int32_t MaxConnectedGamepads = 1;
#else
		static constexpr std::int32_t MaxConnectedGamepads = 4;
#endif

		static void Reset();

		static KeySym Key1(std::int32_t playerIndex, PlayerActions action)
		{
			return _mappings[playerIndex * (std::int32_t)PlayerActions::Count + (std::int32_t)action].Key1;
		}

		static KeySym Key2(std::int32_t playerIndex, PlayerActions action)
		{
			return _mappings[playerIndex * (std::int32_t)PlayerActions::Count + (std::int32_t)action].Key2;
		}

		static ButtonName Gamepad(std::int32_t playerIndex, PlayerActions action, std::int32_t& gamepadIndex)
		{
			auto& mapping = _mappings[playerIndex * (std::int32_t)PlayerActions::Count + (std::int32_t)action];
			gamepadIndex = mapping.GamepadIndex;
			return mapping.GamepadButton;
		}

		static ArrayView<ControlSchemeMapping> GetMappings()
		{
			return _mappings;
		}

	private:
		ControlScheme(const ControlScheme&) = delete;
		ControlScheme& operator=(const ControlScheme&) = delete;

		static ControlSchemeMapping _mappings[MaxSupportedPlayers * (std::int32_t)PlayerActions::Count];
	};
}