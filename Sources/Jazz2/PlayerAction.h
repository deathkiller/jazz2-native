#pragma once

namespace Jazz2
{
	/**
		@brief Player action
		
		Logical input action a player can perform, decoupled from the physical key, button or touch control bound to
		it. Covers movement, jumping, running, firing, weapon switching and UI actions (menu, console), plus the
		direct switch-to-weapon shortcuts. @ref PlayerAction::Count is the total number of actions, @ref
		PlayerAction::CountInMenu the subset usable in menus, and @ref PlayerAction::None means no action. Used by the
		control scheme and input handling to query whether an action is pressed or hit.
	*/
	enum class PlayerAction
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