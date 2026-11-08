// Documentation only, never compiled by the game: hooks a level script defines, namespaces of the API, and the types
// declared by a script section of the game

/** @mainpage Scripting reference

A level script is AngelScript code that belongs to a level. The game compiles it when the level loads and then calls
functions of a few well-known names that the script defines, called <em>hooks</em>. Everything a hook does goes through
the API described here.

Where to start:
- @ref language-reference --- types, operators, handles and other basics of the language, if AngelScript is new to you
- @ref hooks --- functions a script defines and the game calls, such as @ref onLevelLoad(), @ref onMain() or
	@ref onPlayer()
- @ref jjPLAYER --- a player, its position, health, weapons and controls
- @ref jjOBJ --- an object of the level (enemies, pickups, bullets, platforms), all of them are in @ref jjObjects
- @ref jjLAYER --- a layer of tiles, its scrolling and appearance
- @ref jjCANVAS --- drawing over the HUD or into the level from the drawing hooks
- Global functions and properties --- the state of the level and the game, sounds, palette and more
- @ref preprocessor --- lines such as `#include` and `#pragma` that are handled before a script is compiled

Time in a level script is measured in ticks, 70 of them per second. @ref jjGameTicks counts them.

A script that starts with `#pragma target Resurrection` uses the native API of the game instead, which isn't
covered by this reference.

@section mainpage-platforms Supported platforms

Scripting is an optional part of the game and it's currently turned off in all official builds. AngelScript has to
call functions of the game directly, which it can do only on some processors, so it could be turned on in the
following versions of the game:

- Windows on x86, x64 and ARM64 processors, including Xbox
- Linux on x86, x64, ARM, ARM64 and RISC-V processors, including PortMaster handhelds
- macOS on Intel and Apple Silicon processors
- Android and iOS
- Nintendo Switch and PlayStation Vita
- Web browsers (WebAssembly)

Other consoles and Amiga systems would need more work or have too little memory for it.
*/

/**
	@brief Called once when the level is loaded

	Runs after the global variables of the script are initialized, before the level starts and before the objects
	of the level are created. The place to set up the palette, layers, water or anything else the level should start
	with.
	@callback
*/
void onLevelLoad();

/**
	@brief Called once when the level begins

	Runs a little later than @ref onLevelLoad(), once the game has created the objects around the starting positions of
	the players, so the script can change those objects too.
	@callback
*/
void onLevelBegin();

/**
	@brief Called after the level returns to the last checkpoint

	When a player dies and the level is rolled back to the last checkpoint, the objects created by scripts are
	removed and this hook is called afterwards. Use it to bring back anything of the script that should survive the
	death of the player.
	@callback
*/
void onLevelReload();

/**
	@brief Called once in every game tick

	The main update of a script, 70 times per second regardless of the frame rate of the game. Within a tick it runs
	after @ref onPlayerInput() and before @ref onPlayer().
	@callback
*/
void onMain();

/**
	@brief Called in every game tick for each player, before @ref onMain()

	Meant for scripts that change what the player believes its controls are doing, such as swapping two keys. The
	key properties of @ref jjPLAYER can be read and overwritten here before the tick uses them.
	@param player   The player being updated
	@callback
*/
void onPlayerInput(jjPLAYER@ player);

/**
	@brief Called in every game tick for each player, after @ref onMain()

	The usual place for anything that concerns a single player, since the player is given directly and there's no
	need to go through @ref jjLocalPlayers.
	@param player   The player being updated
	@callback
*/
void onPlayer(jjPLAYER@ player);

/**
	@brief Called in every game tick for each player to adjust how it is drawn

	The @ref jjPLAYERDRAW structure comes filled with how the game would draw the player. If the script changes the frame,
	offsets, scale, angle or sprite mode, the game draws that sprite in place of the player until the next tick.
	Turning off its <em>sprite</em> hides the player entirely. Runs after @ref onPlayer().
	@param draw     How the player is going to be drawn
	@callback
*/
void onPlayerDraw(jjPLAYERDRAW& draw);

/**
	@brief Called when the timer of a player runs out

	This is the default function for a timer started by @ref jjPLAYER::timerStart(). Another function can be chosen with
	@ref jjPLAYER::timerFunction(). The parameter may be left out.
	@param player   The player whose timer ran out
	@callback
*/
void onPlayerTimerEnd(jjPLAYER@ player);

/**
	@brief Called when a player dies

	@param victim   The player who died
	@param killer   The player responsible for the death, or `null` if not killed by another player
	@callback
	@partialsupport `killer` is always `null` on multiplayer clients.
*/
void onRoast(jjPLAYER@ victim, jjPLAYER@ killer);

/**
	@brief Called when a player enters a cheat code in the console

	Called for every code starting with `jj`, also for codes the game doesn't know, so a script can add its own.
	Not called while cheats aren't allowed.
	@param cheat    The code as it was entered, such as `jjnxt`
	@return `true` if the script handled the code and the game shouldn't apply it, `false` to let the game apply it
	@callback
*/
bool onCheat(string &in cheat);

/**
	@brief Called by a Script Callback event of the level, numbered from 0 to 255

	The number in the name stands for the <em>Function</em> parameter of the event, so an event with number 7 calls
	`onFunction7()`. Text events of original levels with the AngelScript option enabled are converted to this event,
	their text number becomes the function number. Define only one function for each number.

	Each parameter is optional, the following forms are accepted:
	- `void onFunction7()`
	- `void onFunction7(jjPLAYER@ player)`
	- `void onFunction7(jjPLAYER@ player, bool param)`, `uint8` or `int8` in place of `bool`
	- `void onFunction7(bool param)`, `uint8` or `int8` in place of `bool`

	A function that takes a player is called only when a player triggers the event. If the event has <em>Vanish</em>
	set, the function is disabled after its first call until it's enabled again by @ref jjEnabledASFunctions or
	@ref jjEnableEachASFunction(). It can still be called directly from the script.
	@param player   The player who triggered the event
	@param param    The <em>Param</em> value of the event
	@callback
*/
void onFunction0(jjPLAYER@ player, uint8 param);

/**
	@brief Called when an object with scripted collisions is hit

	Only objects with @ref jjOBJ::scriptedCollisions enabled are affected. If the class of the behavior of the object
	implements its own @ref onObjectHit() method, that one decides first, and this global hook is called only if the
	object has no such method or the method returned `false`.

	An object with @ref HANDLING::PICKUP gets the call when a player touches it, the game never collects it on its own.
	An object with @ref HANDLING::SPECIAL gets the call when a player touches it (at most once per tick) or a bullet hits
	it.
	@param obj      The object that was hit
	@param bullet   The bullet that hit the object, `null` for a touch of a player
	@param player   The player who touched the object or fired the bullet, may be `null` for a bullet
	@param force    For a touch, 1 while the player attacks (a sugar rush or a special move) and 0 otherwise. For a
		bullet, the damage it deals.
	@callback
*/
void onObjectHit(jjOBJ@ obj, jjOBJ@ bullet, jjPLAYER@ player, int force);

/**
	@brief Called when the HUD draws the weapon and ammo of a player

	Called every frame for the view of each player.
	@param player   The player whose view is drawn
	@param canvas   Canvas covering the view of the player
	@return `true` if the script drew its own version and the game should skip the default one, `false` to draw
		the default one too
	@callback
*/
bool onDrawAmmo(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Called when the HUD draws the health of a player

	Called every frame for the view of each player, while the game would show the health.
	@param player   The player whose view is drawn
	@param canvas   Canvas covering the view of the player
	@return `true` if the script drew its own version and the game should skip the default one, `false` to draw
		the default one too
	@callback
*/
bool onDrawHealth(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Called when the HUD draws the lives of a player

	Called every frame for the view of each player.
	@param player   The player whose view is drawn
	@param canvas   Canvas covering the view of the player
	@return `true` if the script drew its own version and the game should skip the default one, `false` to draw
		the default one too
	@callback
*/
bool onDrawLives(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Called when the HUD draws the score of a player

	Called every frame for the view of each player.
	@param player   The player whose view is drawn
	@param canvas   Canvas covering the view of the player
	@return `true` if the script drew its own version and the game should skip the default one, `false` to draw
		the default one too
	@callback
	@partialsupport The HUD of multiplayer games doesn't call it.
*/
bool onDrawScore(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Called when the HUD draws the timer of a player

	Called every frame for the view of each player while the timer started by @ref jjPLAYER::timerStart() runs or is
	paused.
	@param player   The player whose view is drawn
	@param canvas   Canvas covering the view of the player
	@return `true` if the script drew its own version and the game should skip the default one
	@callback
*/
bool onDrawPlayerTimer(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Called when the HUD draws the information of the game mode of a multiplayer game
	@param player   The player whose view is drawn
	@param canvas   Canvas covering the view of the player
	@return `true` if the script drew its own version and the game should skip the default one
	@callback
	@partialsupport Never called by the game.
*/
bool onDrawGameModeHUD(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Draws into the level at layer 1

	There are eight of these hooks, @ref onDrawLayer1() to @ref onDrawLayer8(), one for each layer of the level. They're
	called every frame for the view of each player, from layer 8 at the back to layer 1 at the front. What a hook
	draws scrolls with its layer and appears in front of the tiles of the layer. At layer 4, the sprite layer, it
	stays behind the objects. The canvas only draws, nothing drawn becomes a part of the level.
	@param player   The player whose view is drawn
	@param canvas   Canvas placed at the layer for the view of the player
	@callback
*/
void onDrawLayer1(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Draws into the level at layer 2

	See @ref onDrawLayer1() for details.
	@param player   The player whose view is drawn
	@param canvas   Canvas placed at the layer for the view of the player
	@callback
*/
void onDrawLayer2(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Draws into the level at layer 3

	See @ref onDrawLayer1() for details.
	@param player   The player whose view is drawn
	@param canvas   Canvas placed at the layer for the view of the player
	@callback
*/
void onDrawLayer3(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Draws into the level at layer 4, the sprite layer

	What's drawn here stays behind the objects of the level. See @ref onDrawLayer1() for details.
	@param player   The player whose view is drawn
	@param canvas   Canvas placed at the layer for the view of the player
	@callback
*/
void onDrawLayer4(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Draws into the level at layer 5

	See @ref onDrawLayer1() for details.
	@param player   The player whose view is drawn
	@param canvas   Canvas placed at the layer for the view of the player
	@callback
*/
void onDrawLayer5(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Draws into the level at layer 6

	See @ref onDrawLayer1() for details.
	@param player   The player whose view is drawn
	@param canvas   Canvas placed at the layer for the view of the player
	@callback
*/
void onDrawLayer6(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Draws into the level at layer 7

	See @ref onDrawLayer1() for details.
	@param player   The player whose view is drawn
	@param canvas   Canvas placed at the layer for the view of the player
	@callback
*/
void onDrawLayer7(jjPLAYER@ player, jjCANVAS@ canvas);

/**
	@brief Draws into the level at layer 8, the background

	See @ref onDrawLayer1() for details.
	@param player   The player whose view is drawn
	@param canvas   Canvas placed at the layer for the view of the player
	@callback
*/
void onDrawLayer8(jjPLAYER@ player, jjCANVAS@ canvas);

// Optional methods of behavior classes, looked up by name

interface jjBEHAVIORINTERFACE {
	/**
		@brief Draws the object

		An optional method of a behavior class. The game calls it right after
		@ref jjBEHAVIORINTERFACE::onBehave() each time the behavior runs, unless the object isn't drawn then, such as
		when @ref jjOBJ::behave() is called with `draw` set to `false`. Code that only concerns how the object looks can
		go here to keep @ref jjBEHAVIORINTERFACE::onBehave() tidier, but it may as well stay there.
		@param obj      The object being drawn
	*/
	void onDraw(jjOBJ@ obj);

	/**
		@brief Called when the object is hit by a player or a bullet

		An optional method of a behavior class, the class counterpart of the global @ref onObjectHit() hook, called
		under the same conditions: only for objects with @ref jjOBJ::scriptedCollisions enabled and
		@ref jjOBJ::playerHandling set to @ref HANDLING::PICKUP or @ref HANDLING::SPECIAL. Prefer it to the global
		hook, it only concerns objects with this behavior.

		An object with @ref HANDLING::PICKUP gets the call when a player touches it. To have it collected, set its
		behavior to @ref BEHAVIOR::EXPLOSION2. An object with @ref HANDLING::SPECIAL gets the call when a player
		touches or attacks it, or when a bullet hits it.
		@param obj      The object that was hit
		@param bullet   The bullet that hit the object, `null` for a touch of a player
		@param player   The player who touched the object or fired the bullet, may be `null` for a bullet
		@param force    For a touch, 1 while the player attacks (a sugar rush or a special move) and 0 otherwise. For a
			bullet, the damage it deals.
		@return `true` if the method handled the hit, `false` to let the global @ref onObjectHit() hook handle it
	*/
	bool onObjectHit(jjOBJ@ obj, jjOBJ@ bullet, jjPLAYER@ player, int force);

	/**
		@brief Tells whether the object is active for a joining player

		An optional method of a behavior class, meant for objects placed in the level. Together with
		@ref jjBEHAVIORINTERFACE::onSetActive() it lets a multiplayer server tell joining players which of these
		objects are still there, such as pickups already collected. Without it, the object counts as active while
		@ref jjOBJ::isActive is `true`.
		@param obj      The object in question
		@return Whether the object is active
		@partialsupport Never called by the game, there's no such synchronization yet.
	*/
	bool onGetActive(jjOBJ@ obj);

	/**
		@brief Sets whether the object is active for a joining player

		An optional method of a behavior class, the counterpart of @ref jjBEHAVIORINTERFACE::onGetActive(), called on
		the side of the joining player. Without it, an object that isn't active is deleted.
		@param obj      The object in question
		@param setTo    Whether the object is active
		@partialsupport Never called by the game, there's no such synchronization yet.
	*/
	void onSetActive(jjOBJ@ obj, bool setTo);

	/**
		@brief Tells whether boxes treat the object as solid

		An optional method of a behavior class. Boxes, such as crates, monitors and other objects of similar behaviors,
		can rest on each other and can't be pushed through each other. Returning `true` puts objects with this
		behavior among them. It doesn't change whether players treat the object as solid. Without the method, the
		object isn't solid for boxes.
		@param obj      The object in question
		@return Whether the object is solid for boxes
		@partialsupport Never called by the game.
	*/
	bool onIsSolid(jjOBJ@ obj);

	/**
		@brief Tells whether the object is a bullet of the RF missile kind

		An optional method of a behavior class. RF missiles get a special treatment of their speed when they're fired,
		a behavior that wraps @ref BEHAVIOR::RFBULLET should return `true` to get the same treatment. Without the
		method, the object isn't treated as an RF missile.
		@param obj      The object in question
		@return Whether the object is an RF missile
		@partialsupport Never called by the game.
	*/
	bool onIsRFBullet(jjOBJ@ obj);
}

// Namespaces

/** @namespace AIR
@brief What the jump key does in the air */

/** @namespace ANIM
@brief Animation sets */

/** @namespace AREA
@brief Event types of the tiles of the level */

/** @namespace BEHAVIOR
@brief Built-in behaviors of objects */

/** @namespace CHAR
@brief Playable characters */

/** @namespace CHAT
@brief Kinds of chat messages */

/** @namespace COLOR
@brief Components of a color */

/** @namespace CREATOR
@brief What created an object */

/** @namespace DIRECTION
@brief Directions a player can face */

/** @namespace FLIGHT
@brief Ways a player can fly */

/** @namespace GAME
@brief Game modes, their custom variants, game states and connection types */

/** @namespace GEM
@brief Colors of gems */

/** @namespace GROUND
@brief What the jump key does on the ground */

/** @namespace HANDLING
@brief How objects react to players and bullets */

/** @namespace HELP
@brief Layout of help pages, kept for compatibility */

/** @namespace LAYERSPEEDMODEL
@brief How layers scroll */

/** @namespace LIGHT
@brief Light types and lighting enforcement modes */

/** @namespace MLLE
@brief Support for levels made with extended level editing tools */

/** @namespace OBJECT
@brief Types of objects, as placed by events */

/** @namespace PARTICLE
@brief Types of particles */

/** @namespace RABBIT
@brief Animations of a playable character within its animation set */

/** @namespace SHIELD
@brief Types of shields */

/** @namespace SNOWING
@brief Types of weather */

/** @namespace SOUND
@brief Sound samples */

/** @namespace SPREAD
@brief How a weapon spreads its shots */

/** @namespace SPRITE
@brief Sprite drawing modes and flips */

/** @namespace STATE
@brief States of objects */

/** @namespace STRING
@brief Text sizes, alignments and modes */

/** @namespace SURFACE
@brief Surfaces of textured backgrounds */

/** @namespace TEAM
@brief Team colors */

/** @namespace TEXTURE
@brief Textures and styles of textured backgrounds */

/** @namespace TILE
@brief Tile quadrants and tile flags */

/** @namespace TIMER
@brief States of the timer of a player */

/** @namespace WATERINTERACTION
@brief How players interact with water */

/** @namespace WATERLIGHT
@brief Lighting under water */

/** @namespace WEAPON
@brief Weapons and their styles */

// Help pages

/** @enum HELP::Position
@brief Where the image of a help paragraph is placed next to its text

Values are `LEFT`, `RIGHT`, @ref FLOATLEFT and @ref FLOATRIGHT. Kept for compatibility, the game has no help screen. */

/** @class jjHELPPARAGRAPH
@brief A paragraph of a help page

A block of text, optionally with an image or an animation next to it. The game keeps this type so that scripts that
build help pages still compile, but it has no help screen and never shows them.

The first constructor takes the text, its size and alignment, an optional still image and its position. The second
takes an animation in place of the image. */

/** @property jjHELPPARAGRAPH::text
@brief Text of the paragraph */

/** @property jjHELPPARAGRAPH::textSize
@brief Size of the text */

/** @property jjHELPPARAGRAPH::textAlignment
@brief Alignment of the text */

/** @property jjHELPPARAGRAPH::image
@brief Still image shown next to the text, `null` for none */

/** @property jjHELPPARAGRAPH::animation
@brief Animation shown next to the text, `null` for none */

/** @property jjHELPPARAGRAPH::position
@brief Where the image or the animation is placed next to the text */

/** @class jjHELPPAGE
@brief A page of help

A list of paragraphs with tags. The game keeps this type so that scripts that build help pages still compile, but it
has no help screen and never shows them. */

/** @property jjHELPPAGE::paragraphs
@brief Paragraphs of the page */

/** @property jjHELPPAGE::tags
@brief Tags of the page

Words separated by spaces that describe what the page is about. Scripts may look at them to find pages they want to
change or remove. */
