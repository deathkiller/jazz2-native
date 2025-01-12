#pragma once

namespace Jazz2
{
	/** @brief Player action */
	enum class PlayerActions
	{
		Left,
		Right,
		Up,
		Down,
		Buttstomp,
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