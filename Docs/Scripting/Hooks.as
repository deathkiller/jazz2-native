// Documentation only, never compiled by the game

/** @page hooks Hooks
@brief Functions of a script that the game calls

A script doesn't run on its own. It defines functions with well-known names and signatures, called hooks, and the game
calls them at the right moments. Every hook is optional, a script defines only the ones it needs. A hook with a wrong
signature isn't found and is never called, so check the parameter types if a hook seems to do nothing.

@section hooks-level Level

- @ref onLevelLoad() --- once when the level is loaded, after the global variables of the script are initialized and
	before the objects of the level are created
- @ref onLevelBegin() --- once a little later, when the objects around the starting positions of the players exist
- @ref onLevelReload() --- after the level returns to the last checkpoint, when a player died

@section hooks-tick Every tick

There are 70 ticks in a second, independent of the frame rate. In every tick, the hooks are called in this order:

1. @ref onPlayerInput() --- for each player, the place to change what the controls of the player do
2. @ref onMain() --- the main update of the script
3. @ref onPlayer() --- for each player
4. @ref onPlayerDraw() --- for each player, adjusts how the player is drawn

@section hooks-drawing Drawing

Drawing hooks are called every frame for the view of each local player and get a @ref jjCANVAS to draw with.

- @ref onDrawLayer1() to @ref onDrawLayer8() --- draw into the level at the given layer, from the background at the back
	to layer 1 at the front
- @ref onDrawAmmo(), @ref onDrawHealth(), @ref onDrawLives(), @ref onDrawScore() --- draw parts of the HUD. Returning
	`true` replaces the part the game would draw, returning `false` keeps it.

@section hooks-events Events and objects

- @ref onFunction0() --- `onFunction0()` to `onFunction255()`, called by Script Callback events of the level
- @ref onObjectHit() --- when a player or a bullet hits an object with @ref jjOBJ::scriptedCollisions
- @ref onPlayerTimerEnd() --- when the timer of a player started by @ref jjPLAYER::timerStart() runs out
- @ref onRoast() --- when a player dies

A class implementing @ref jjBEHAVIORINTERFACE becomes a behavior of an object, the game then calls its
@ref jjBEHAVIORINTERFACE::onBehave() method every tick, and optionally its @ref jjBEHAVIORINTERFACE::onDraw() and
@ref jjBEHAVIORINTERFACE::onObjectHit() methods.

@section hooks-unsupported Not supported

The following hooks of other games are never called, a script may define them, but they have no effect:

- `onChat()`, `onLocalChat()`, `onCheat()` --- chat messages and cheats
- `onGameStart()`, `onGameStop()` --- starting and stopping of a multiplayer game
- `onReceive()`, `onGetPublicInterface()` --- communication between scripts and over the network
- `onHelp()` --- help pages
- `onDrawPlayerTimer()`, `onDrawGameModeHUD()` --- the game never draws these parts of the HUD through a script

@note A few hooks are called with less information than they could have, or not in all game modes, see
@ref onRoast() and @ref onDrawScore().
*/
