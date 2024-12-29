#pragma once

namespace Jazz2
{
	/** @brief Player actions */
	enum class PlayerActions
	{
		Left,
		Right,
		Up,
		Down,
		Fire,
		Jump,
		Run,
		ChangeWeapon,
		Menu,
		Console,

		SwitchToBlaster,
		SwitchToBouncer,
		SwitchToFreezer,
		SwitchToSeeker,
		SwitchToRF,
		SwitchToToaster,
		SwitchToTNT,
		SwitchToPepper,
		SwitchToElectro,
		SwitchToThunderbolt,

		Count,
		CountInMenu = SwitchToBlaster,

		None = -1
	};
}