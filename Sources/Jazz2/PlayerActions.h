#pragma once

namespace Jazz2
{
	/** @brief Player action */
	enum class PlayerActions
	{
		Left,							/**< Left */
		Right,							/**< Right */
		Up,								/**< Up */
		Down,							/**< Down */
		Buttstomp,						/**< Buttstomp (Down in the air) */
		Fire,							/**< Fire a weapon */
		Jump,							/**< Jump */
		Run,							/**< Run */
		ChangeWeapon,					/**< Change a weapon */
		Menu,							/**< Menu / Back */
		Console,						/**< Toggle in-game console */

		SwitchToBlaster,				/**< Switch to weapon 1 */
		SwitchToBouncer,				/**< Switch to weapon 2 */
		SwitchToFreezer,				/**< Switch to weapon 3 */
		SwitchToSeeker,					/**< Switch to weapon 4 */
		SwitchToRF,						/**< Switch to weapon 5 */
		SwitchToToaster,				/**< Switch to weapon 6 */
		SwitchToTNT,					/**< Switch to weapon 7 */
		SwitchToPepper,					/**< Switch to weapon 8 */
		SwitchToElectro,				/**< Switch to weapon 9 */
		SwitchToThunderbolt,			/**< Switch to weapon 10 */

		Count,							/**< Number of all actions */
		CountInMenu = SwitchToBlaster,	/**< Number of actions usable in a menu */

		None = -1						/**< No action */
	};
}