#pragma once

#include "../PlayerActions.h"
#include "../../nCine/Base/BitArray.h"
#include "../../nCine/Input/InputEvents.h"
#include "../../nCine/Primitives/Vector2.h"

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::UI::Menu
{
	class RemapControlsSection;
}

namespace Jazz2::Input
{
	/** @brief Navigation flags for @ref ControlScheme::FetchNavigation(), supports a bitwise combination of its member values */
	enum class NavigationFlags
	{
		/** @brief None */
		None = 0,
		/** @brief Allow only keyboard key presses */
		AllowKeyboard = 0x01,
		/** @brief Allow only gamepad button presses */
		AllowGamepads = 0x02,
		/** @brief Allow both keyboard and gamepad presses */
		AllowAll = AllowKeyboard | AllowGamepads
	};

	DEATH_ENUM_FLAGS(NavigationFlags);

	/** @brief Control mapping target */
	struct MappingTarget
	{
		/** @brief Opaque data of the target */
		std::uint32_t Data;
	};

	/** @brief Control mapping for a particular action */
	struct ControlSchemeMapping
	{
		/** @brief List of mapping targets */
		SmallVector<MappingTarget, 3> Targets;
	};

	/** @brief Result returned by @ref ControlScheme::FetchProcessedInput() */
	struct ProcessedInput
	{
		/** @brief Pressed actions */
		std::uint64_t PressedActions;
		/** @brief Movement vector */
		Vector2f Movement;
	};

	/** @brief Provides access to a user configured control scheme */
	class ControlScheme
	{
		friend class UI::Menu::RemapControlsSection;

	public:
		ControlScheme() = delete;
		~ControlScheme() = delete;

		/** @brief Maximum number of supported local players */
		static constexpr std::int32_t MaxSupportedPlayers = 4;
		/** @brief Maximum number of supported connected gamepads */
#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_WINDOWS_RT)
		static constexpr std::int32_t MaxConnectedGamepads = 4;
#else
		static constexpr std::int32_t MaxConnectedGamepads = 6;
#endif

		/** @brief Resets all bindings to default */
		static void Reset();

		/** @brief Fetches processed standard input for specified player according to the current bindings */
		static ProcessedInput FetchProcessedInput(std::int32_t playerIndex, const BitArray& pressedKeys, const ArrayView<const JoyMappedState*> joyStates, bool analogAsButtons = true);
		/** @brief Fetches navigation input according to the current bindings */
		static std::uint32_t FetchNavigation(const BitArray& pressedKeys, const ArrayView<const JoyMappedState*> joyStates, NavigationFlags flags = NavigationFlags::AllowAll);

		/** @brief Returns the entire mapping configuration */
		static ArrayView<ControlSchemeMapping> GetAllMappings();
		/** @brief Returns a mapping configuration for a given player index */
		static ArrayView<ControlSchemeMapping> GetMappings(std::int32_t playerIdx);
		/** @brief Returns a gamepad index for a given player index */
		static std::int32_t GetGamepadForPlayer(std::int32_t playerIdx);

		/** @brief Creates mapping target for a given key */
		static MappingTarget CreateTarget(Keys key);
		/** @brief Creates mapping target for a given gamepad button */
		static MappingTarget CreateTarget(std::uint32_t gamepadIndex, ButtonName button);
		/** @brief Creates mapping target for a given gamepad axis */
		static MappingTarget CreateTarget(std::uint32_t gamepadIndex, AxisName axis, bool negative = false);

	private:
		static constexpr std::uint32_t GamepadMask = 0x80000000u;
		static constexpr std::uint32_t GamepadAnalogMask = 0x40000000u;
		static constexpr std::uint32_t GamepadNegativeMask = 0x20000000u;
		static constexpr std::uint32_t GamepadIndexMask = 0x0FFF0000u;
		static constexpr std::uint32_t ButtonMask = 0x0000FFFFu;
		static constexpr float GamepadDeadZone = 0.1f;

		static ControlSchemeMapping _mappings[MaxSupportedPlayers * (std::int32_t)PlayerActions::Count];
	};
}