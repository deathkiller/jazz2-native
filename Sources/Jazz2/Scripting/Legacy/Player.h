#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Canvas.h"
#include "Objects.h"
#include "../../WeaponDefinition.h"

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		/**
		 * @brief A player in the level
		 *
		 * Script view of one player: position and speed, health, lives, score, ammo and powerups, the current
		 * character, pressed keys, camera, timer and many state counters, plus actions such as warping, morphing or
		 * hurting. Positions are in pixels and durations in ticks, 70 ticks per second. Scripts get players from
		 * @ref jjP, @ref jjPlayers or @ref jjLocalPlayers. The level script loader owns one instance per player and
		 * wraps the game's own player actor with it.
		 * @partialsupport A handle that doesn't stand for a player in the level has isActive set to false, most of its
		 * properties read as 0 and writes to them are ignored.
		 */
		class jjPLAYER
		{
			friend class Scripting::LevelScriptLoader;
			friend class jjLAYER;	// jjLAYER::getXPosition/getYPosition read the player's camera
			friend class jjOBJ;		// jjOBJ::doesCollidePlayer() reads the hitbox of the player

		public:
			/** @brief Creates a new instance wrapping the given engine player */
			jjPLAYER(LevelScriptLoader* levelScripts, Actors::Player* player);
			~jjPLAYER();

			jjPLAYER& operator=(const jjPLAYER& o);

			/** @brief Returns the player's score */
			std::int32_t get_score() const;
			/** @brief Sets the player's score */
			std::int32_t set_score(std::int32_t value);
			/**
			 * @brief Returns the score shown on the HUD
			 *
			 * @partialsupport The HUD shows the score without counting up to it, so this is the same value as score and
			 * setting either one changes both.
			 */
			std::int32_t get_scoreDisplayed() const;
			/** @brief Sets the score shown on the HUD */
			std::int32_t set_scoreDisplayed(std::int32_t value);

			/**
			 * @brief Sets the player's score
			 *
			 * Same as assigning score.
			 * @return The new score
			 */
			std::int32_t setScore(std::int32_t value);

			/**
			 * @brief Returns the horizontal position in pixels
			 *
			 * Assigning it moves the player there at once, without colliding with or triggering anything on the way.
			 */
			float get_xPos() const;
			/** @brief Sets the horizontal position in pixels */
			float set_xPos(float value);
			/**
			 * @brief Returns the vertical position in pixels
			 *
			 * Assigning it moves the player there at once, without colliding with or triggering anything on the way.
			 */
			float get_yPos() const;
			/** @brief Sets the vertical position in pixels */
			float set_yPos(float value);
			/**
			 * @brief Returns the horizontal acceleration
			 *
			 * An external force applied to the player's horizontal speed, positive to the right.
			 */
			float get_xAcc() const;
			/** @brief Sets the horizontal acceleration */
			float set_xAcc(float value);
			/**
			 * @brief Returns the vertical acceleration
			 *
			 * An external force applied to the player's vertical speed, positive downwards.
			 */
			float get_yAcc() const;
			/** @brief Sets the vertical acceleration */
			float set_yAcc(float value);
			/**
			 * @brief Returns the horizontal respawn position in pixels
			 *
			 * Where the player comes back after dying, usually the last checkpoint reached.
			 */
			float get_xOrg() const;
			/** @brief Sets the horizontal respawn position in pixels */
			float set_xOrg(float value);
			/**
			 * @brief Returns the vertical respawn position in pixels
			 *
			 * Where the player comes back after dying, usually the last checkpoint reached.
			 */
			float get_yOrg() const;
			/** @brief Sets the vertical respawn position in pixels */
			float set_yOrg(float value);
			/**
			 * @brief Returns the horizontal speed in pixels per tick
			 *
			 * Positive values move the player to the right.
			 */
			float get_xSpeed();
			/** @brief Sets the horizontal speed in pixels per tick */
			float set_xSpeed(float value);
			/**
			 * @brief Returns the vertical speed in pixels per tick
			 *
			 * Positive values move the player down.
			 */
			float get_ySpeed();
			/** @brief Sets the vertical speed in pixels per tick */
			float set_ySpeed(float value);

			/**
			 * @brief Vertical speed the player gains by jumping
			 *
			 * @partialsupport Currently reads 0 and has no effect.
			 */
			float jumpStrength = 0.0f;
			/**
			 * @brief Returns how many more ticks the player stays frozen
			 *
			 * 0 when the player isn't frozen, at most 127. Assigning a positive value freezes the player for that long,
			 * 0 or less unfreezes it.
			 */
			std::int8_t get_frozen() const;
			/** @brief Sets how many more ticks the player stays frozen */
			std::int8_t set_frozen(std::int8_t value);

			/**
			 * @brief Freezes or unfreezes the player
			 *
			 * Freezing lasts 3 seconds (210 ticks).
			 * @param frozen Pass false to unfreeze the player instead
			 */
			void freeze(bool frozen);
			/**
			 * @brief Returns the tile the player is in, packed into one number
			 *
			 * Equals `int(xPos) / 32 + int(yPos) / 32 * 65536`. Mostly useful for telling whether the player moved
			 * to another tile since an earlier check.
			 */
			std::int32_t get_currTile();
			/**
			 * @brief Gives the player a sugar rush
			 *
			 * @param time Duration in ticks
			 * @return False without starting it while a boss fight is active or the game mode doesn't allow it,
			 *		true otherwise
			 */
			bool startSugarRush(std::int32_t time);
			/** @brief Returns how many hearts the player has left */
			std::int8_t get_health() const;
			/** @brief Sets how many hearts the player has left */
			std::int8_t set_health(std::int8_t value);

			/**
			 * @brief Warp the player is currently going through
			 *
			 * @partialsupport Currently always 0.
			 */
			std::int32_t warpID = 0;

			/**
			 * @brief Returns the delay between shots in ticks
			 *
			 * 35 without any fast fire, lowered by about 3 ticks with each fast fire collected, down to 6. An assigned
			 * value is rounded to the closest number of fast fires.
			 */
			std::int32_t get_fastfire() const;
			/** @brief Sets the delay between shots in ticks */
			std::int32_t set_fastfire(std::int32_t value);
			/**
			 * @brief Returns the selected weapon
			 *
			 * A 1-based @ref WEAPON::Weapon number. Assigning a number that isn't a weapon is ignored.
			 */
			std::int8_t get_currWeapon() const;
			/** @brief Sets the selected weapon */
			std::int8_t set_currWeapon(std::int8_t value);

			/** @brief Returns how many lives the player has left */
			std::int32_t get_lives() const;
			/** @brief Sets how many lives the player has left */
			std::int32_t set_lives(std::int32_t value);

			/**
			 * @brief Returns the remaining invincibility in ticks
			 *
			 * Assigning a positive value makes the player invincible for that long, 0 or less ends it.
			 * @partialsupport Invincibility and `blink` share one timer, starting one ends the other.
			 */
			std::int32_t get_invincibility() const;
			/** @brief Sets the remaining invincibility in ticks */
			std::int32_t set_invincibility(std::int32_t value);
			/**
			 * @brief Returns the type of light the player emits
			 *
			 * Either @ref LIGHT::PLAYER, the default, or @ref LIGHT::NONE. Assigning @ref LIGHT::NONE turns the player's light off.
			 * @partialsupport Any other type turns the default light back on, it reads back as @ref LIGHT::PLAYER.
			 */
			std::uint32_t get_lightType() const;
			/** @brief Sets the type of light the player emits */
			std::uint32_t set_lightType(std::uint32_t value);
			/**
			 * @brief Returns how many more ticks the player blinks
			 *
			 * A blinking player, as after being hurt, can't be hurt again. Assigning a positive value starts blinking,
			 * 0 ends it without touching other kinds of invincibility.
			 */
			std::int32_t get_blink() const;
			/** @brief Sets how many more ticks the player blinks */
			std::int32_t set_blink(std::int32_t value);
			/**
			 * @brief Extends the player's invincibility
			 *
			 * @param duration Ticks to add, a negative value shortens it but never below 0
			 * @return The remaining invincibility in ticks
			 * @partialsupport While the player blinks, the blinking ends and the invincibility starts from 0.
			 */
			std::int32_t extendInvincibility(std::int32_t duration);
			/**
			 * @brief Returns how much food the player has eaten
			 *
			 * Assigning it only changes the counter, use @ref startSugarRush() to start a sugar rush.
			 */
			std::int32_t get_food() const;
			/** @brief Sets how much food the player has eaten */
			std::int32_t set_food(std::int32_t value);
			/** @brief Returns how many coins the player has */
			std::int32_t get_coins() const;
			/** @brief Sets how many coins the player has */
			std::int32_t set_coins(std::int32_t value);
			/**
			 * @brief Spends coins if the player has enough
			 *
			 * Works like a coin warp whose outcome the script decides.
			 * @return True if the player had at least `numberOfCoins` coins, which are then taken away, false otherwise
			 *		with the coins left untouched
			 */
			bool testForCoins(std::int32_t numberOfCoins);
			/**
			 * @brief Returns how many gems of the given color the player has
			 *
			 * Indexed by @ref GEM::Color. Other indices read as 0 and ignore writes.
			 */
			std::int32_t get_gems(std::uint32_t type) const;
			/** @brief Sets how many gems of the given color the player has */
			std::int32_t set_gems(std::uint32_t type, std::int32_t value);
			/**
			 * @brief Spends gems of one color if the player has enough
			 *
			 * @param numberOfGems Number of gems to take away
			 * @param type @ref GEM::Color of the gems
			 * @return True if the player had enough gems, which are then taken away, false otherwise with the gems left
			 *		untouched
			 */
			bool testForGems(std::int32_t numberOfGems, std::uint32_t type);

			/**
			 * @brief Returns the shield the player has
			 *
			 * A @ref SHIELD::Shield value. Assigning a value that isn't a known shield is ignored.
			 */
			std::int32_t get_shieldType() const;
			/** @brief Sets the shield the player has */
			std::int32_t set_shieldType(std::int32_t value);
			/**
			 * @brief Returns how many more ticks the shield lasts
			 *
			 * 0 when the player has no shield.
			 */
			std::int32_t get_shieldTime() const;
			/** @brief Sets how many more ticks the shield lasts */
			std::int32_t set_shieldTime(std::int32_t value);
			/**
			 * @brief Returns how many more ticks the player stays rolled up in a ball
			 *
			 * Assigning a positive value rolls the player up and takes away control until the time runs out, 0 ends it
			 * at once.
			 */
			std::int32_t get_rolling() const;
			/** @brief Sets how many more ticks the player stays rolled up in a ball */
			std::int32_t set_rolling(std::int32_t value);

			/**
			 * @brief Object ID of the boss whose health the boss meter shows
			 *
			 * While bossActivated is true, the meter shows the energy of that object as a share of the energy of its
			 * entry in @ref jjObjectPresets. 0 if there's no boss. The game fills it in while one of its own bosses is
			 * active, otherwise it keeps what the script assigned.
			 */
			std::int32_t bossNumber = 0;
			/** @brief Object id of the player's boss */
			std::int32_t boss = 0;
			/**
			 * @brief Whether a boss fight is active
			 *
			 * Set by the Activate Boss event or @ref activateBoss(). The game keeps it in line with its own bosses, but a
			 * value the script assigns stays until the game's own boss state changes.
			 */
			bool bossActive = false;
			/**
			 * @brief Returns the direction the player faces
			 *
			 * -1 for left, 1 for right. Assigning a negative value turns the player left, a positive one right and 0
			 * is ignored.
			 */
			std::int8_t get_direction() const;
			/** @brief Sets the direction the player faces */
			std::int8_t set_direction(std::int8_t value);
			/**
			 * @brief Object ID of the object the player stands on, or pushes (see @ref jjOBJ::beSolid())
			 *
			 * 0 when the player isn't standing on an object scripts can see. The game refreshes it, so assigning it has
			 * no lasting effect.
			 */
			std::int32_t platform = 0;
			/**
			 * @brief Object ID of the flag the player carries
			 *
			 * @partialsupport Currently always 0.
			 */
			std::int32_t flag = 0;
			/**
			 * @brief ID of the game client controlling the player
			 *
			 * @partialsupport Currently always 0.
			 */
			std::int32_t clientID = 0;

			/**
			 * @brief Returns the index of the player
			 *
			 * Currently the same as localPlayerID.
			 * @partialsupport Every player counts as local, also in online multiplayer.
			 */
			std::int8_t get_playerID() const;
			/**
			 * @brief Returns the index of the player among local players
			 *
			 * 0 to 3 in a splitscreen game.
			 * @partialsupport Every player counts as local, also in online multiplayer.
			 */
			std::int32_t get_localPlayerID() const;

			/**
			 * @brief Returns the team the player belongs to
			 *
			 * A @ref TEAM::Color value, @ref TEAM::BLUE outside of team-based game modes.
			 */
			std::int32_t get_team() const;
			/** @brief Returns whether the player isn't on the blue team */
			bool get_teamRed() const;

			/**
			 * @brief Returns whether the player is running
			 *
			 * Follows the run key. Assigning it holds or releases the run key for the current tick, or switches running
			 * on or off when the run key toggles.
			 */
			bool get_running() const;
			/** @brief Sets whether the player is running */
			bool set_running(bool value);

			/**
			 * @brief Returns how many double jumps the player made in the air
			 *
			 * Counted since the last jump from the ground, currently either 0 or 1. Assigning 0 allows another double
			 * jump, any other value uses it up.
			 */
			std::int32_t get_doubleJumpCount() const;
			/** @brief Sets how many double jumps the player made in the air */
			std::int32_t set_doubleJumpCount(std::int32_t value);
			/**
			 * @brief Returns whether the player already used the double jump in the air
			 *
			 * Older form of doubleJumpCount, which should be used instead.
			 */
			bool get_alreadyDoubleJumped() const;
			/** @brief Sets whether the player already used the double jump in the air */
			bool set_alreadyDoubleJumped(bool value);

			/**
			 * @brief Returns how many more ticks the player stays dizzy
			 *
			 * Assigning a positive value makes the player dizzy for that long.
			 */
			std::int32_t get_stoned();
			/** @brief Sets how many more ticks the player stays dizzy */
			std::int32_t set_stoned(std::int32_t value);

			/**
			 * @brief Buttstomp phase of the player
			 *
			 * The game refreshes it, so assigning it has no lasting effect.
			 * @partialsupport Reads only 1 while the player is buttstomping and 50 otherwise, not the phases between.
			 */
			std::int32_t buttstomp = 0;
			/**
			 * @brief Returns how many more ticks the helicopter ears last
			 *
			 * 0 when the player isn't using them.
			 */
			std::int32_t get_helicopter() const;
			/** @brief Sets how many more ticks the helicopter ears last */
			std::int32_t set_helicopter(std::int32_t value);
			/**
			 * @brief Ticks spent using helicopter ears since the last jump
			 *
			 * @partialsupport Currently always 0 and has no effect.
			 */
			std::int32_t helicopterElapsed = 0;
			/**
			 * @brief Special move the player is performing
			 *
			 * 0 when there's none. The game refreshes it, so assigning it has no lasting effect.
			 * @partialsupport A non-zero value identifies the move rather than how long it has lasted.
			 */
			std::int32_t specialMove = 0;
			/**
			 * @brief Returns how many ticks the player has been idle
			 *
			 * 0 when the player isn't idle.
			 */
			std::int32_t get_idle() const;
			/** @brief Sets how many ticks the player has been idle */
			std::int32_t set_idle(std::int32_t value);

			/**
			 * @brief Moves the player as if touching a sucker tube event
			 *
			 * @param xSpeed Horizontal speed
			 * @param ySpeed Vertical speed
			 * @param center Whether to center the player in its tile on the axis it doesn't move along, moving
			 *		diagonally with it every tick can get the player stuck
			 * @param noclip Whether the player passes through walls while in the tube
			 * @param trigSample Currently ignored
			 * @partialsupport The `trigSample` parameter is currently ignored.
			 */
			void suckerTube(std::int32_t xSpeed, std::int32_t ySpeed, bool center, bool noclip, bool trigSample);
			/**
			 * @brief Swings the player around a pole and launches it
			 *
			 * @partialsupport Currently has no effect.
			 */
			void poleSpin(float xSpeed, float ySpeed, std::uint32_t delay);
			/**
			 * @brief Bounces the player as if touching a spring
			 *
			 * Speeds use the scale of springs in the original game, 16 for red, 24 for green and 32 for blue, so an
			 * upward red spring is `spring(0, -16, ...)`. Nothing happens if both speeds are 0.
			 * @param xSpeed Horizontal speed
			 * @param ySpeed Vertical speed
			 * @param keepZeroSpeeds If true, an axis with zero speed keeps the player's current speed instead of
			 *		stopping it
			 * @param sample Whether to play the spring sound
			 */
			void spring(float xSpeed, float ySpeed, bool keepZeroSpeeds, bool sample);

			/**
			 * @brief Whether the player is controlled on this machine
			 *
			 * True for every player in the level, false for a handle that doesn't stand for a player.
			 * @partialsupport Also true for players of other machines in online multiplayer.
			 */
			bool isLocal = true;
			/**
			 * @brief Whether the handle stands for a player in the level
			 *
			 * When false, most other properties read as 0 and writes to them are ignored.
			 */
			bool isActive = true;

			/**
			 * @brief Returns whether the player is still joining an online server
			 *
			 * @partialsupport Currently always false.
			 */
			bool get_isConnecting() const;
			/**
			 * @brief Returns whether the player is an idle server that isn't in the level
			 *
			 * @partialsupport Currently always false.
			 */
			bool get_isIdle() const;
			/**
			 * @brief Returns whether the player is out of a last-rabbit-standing game
			 *
			 * @partialsupport Currently always false.
			 */
			bool get_isOut() const;
			/**
			 * @brief Returns whether the player is spectating by choice
			 *
			 * @partialsupport Currently always false.
			 */
			bool get_isSpectating() const;
			/**
			 * @brief Returns whether the player actually takes part in the game
			 *
			 * Currently the same as isActive.
			 */
			bool get_isInGame() const;

			/**
			 * @brief Returns the player's name, including color codes
			 *
			 * @partialsupport Currently always an empty string.
			 */
			String get_name() const;
			/**
			 * @brief Returns the player's name without color codes
			 *
			 * @partialsupport Currently always an empty string.
			 */
			String get_nameUnformatted() const;
			/**
			 * @brief Changes the player's name
			 *
			 * @partialsupport Currently has no effect and always returns false.
			 */
			bool setName(const String& name);
			/**
			 * @brief Returns the intensity of the light the player emits
			 *
			 * @partialsupport Currently always 0, assigning it has no effect.
			 */
			std::int8_t get_light() const;
			/** @brief Sets the intensity of the light the player emits */
			std::int8_t set_light(std::int8_t value);
			/**
			 * @brief Returns the player's fur colors packed into one number
			 *
			 * Four palette indices, one per byte, the first fur section in the lowest byte. Assigning it recolors the
			 * player at once. @ref furGet() and @ref furSet() work with the four indices separately.
			 */
			std::uint32_t get_fur() const;
			/** @brief Sets the player's fur colors packed into one number */
			std::uint32_t set_fur(std::uint32_t value);
			/**
			 * @brief Reads the player's four fur colors
			 *
			 * Each output receives the palette index of one fur section.
			 */
			void getFur(std::uint8_t& a, std::uint8_t& b, std::uint8_t& c, std::uint8_t& d) const;
			/**
			 * @brief Changes the player's four fur colors
			 *
			 * Each argument is the palette index of one fur section. The player is recolored at once.
			 */
			void setFur(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d);

			/** @brief Returns whether the player is prevented from firing */
			bool get_noFire() const;
			/** @brief Sets whether the player is prevented from firing */
			bool set_noFire(bool value);
			/**
			 * @brief Returns whether gravity is turned off for the player
			 *
			 * While true, the player isn't pulled down by gravity.
			 * @partialsupport It doesn't fall upwards and isn't drawn upside down.
			 */
			bool get_antiGrav() const;
			/** @brief Sets whether gravity is turned off for the player */
			bool set_antiGrav(bool value);
			/**
			 * @brief Returns whether the player is invisible
			 *
			 * @partialsupport Currently it reports whether the player is invulnerable instead, and assigning it has no
			 * effect.
			 */
			bool get_invisibility() const;
			/** @brief Sets whether the player is invisible */
			bool set_invisibility(bool value);
			/**
			 * @brief Returns whether the player passes through walls
			 *
			 * While true, the player also ignores gravity and keeps its current speed. Turning it on can trap the player
			 * if the level isn't built for it.
			 */
			bool get_noclipMode() const;
			/** @brief Sets whether the player passes through walls */
			bool set_noclipMode(bool value);
			/**
			 * @brief Returns the ambient light level of the player's view
			 *
			 * 100 means full brightness. Assigning it changes the ambient light for this player only.
			 */
			std::uint8_t get_lighting() const;
			/** @brief Sets the ambient light level of the player's view */
			std::uint8_t set_lighting(std::uint8_t value);
			/**
			 * @brief Resets the ambient light to the level's default
			 *
			 * @return The new ambient light level
			 */
			std::uint8_t resetLight();

			/**
			 * @brief Returns whether the left key is held
			 *
			 * Assigning it simulates pressing or releasing the key.
			 */
			bool get_playerKeyLeftPressed();
			/**
			 * @brief Returns whether the right key is held
			 *
			 * Assigning it simulates pressing or releasing the key.
			 */
			bool get_playerKeyRightPressed();
			/**
			 * @brief Returns whether the up key is held
			 *
			 * Assigning it simulates pressing or releasing the key.
			 */
			bool get_playerKeyUpPressed();
			/**
			 * @brief Returns whether the down key is held
			 *
			 * Assigning it simulates pressing or releasing the key.
			 */
			bool get_playerKeyDownPressed();
			/**
			 * @brief Returns whether the fire key is held
			 *
			 * Assigning it simulates pressing or releasing the key. Holding it doesn't fire every tick, shots still
			 * wait for fastfire.
			 */
			bool get_playerKeyFirePressed();
			/**
			 * @brief Returns whether the weapon select key is held
			 *
			 * Assigning it simulates pressing or releasing the key.
			 */
			bool get_playerKeySelectPressed();
			/**
			 * @brief Returns whether the jump key is held
			 *
			 * Assigning it simulates pressing or releasing the key.
			 */
			bool get_playerKeyJumpPressed();
			/**
			 * @brief Returns whether the run key is held
			 *
			 * Assigning it simulates pressing or releasing the key.
			 */
			bool get_playerKeyRunPressed();
			/** @brief Sets whether the left key is pressed */
			bool set_playerKeyLeftPressed(bool value);
			/** @brief Sets whether the right key is pressed */
			bool set_playerKeyRightPressed(bool value);
			/** @brief Sets whether the up key is pressed */
			bool set_playerKeyUpPressed(bool value);
			/** @brief Sets whether the down key is pressed */
			bool set_playerKeyDownPressed(bool value);
			/** @brief Sets whether the fire key is pressed */
			bool set_playerKeyFirePressed(bool value);
			/** @brief Sets whether the weapon-select key is pressed */
			bool set_playerKeySelectPressed(bool value);
			/** @brief Sets whether the jump key is pressed */
			bool set_playerKeyJumpPressed(bool value);
			/** @brief Sets whether the run key is pressed */
			bool set_playerKeyRunPressed(bool value);

			/**
			 * @brief Returns whether the given weapon is powered up
			 *
			 * Indexed by a 1-based @ref WEAPON::Weapon number, @ref WEAPON::CURRENT is the selected weapon. Other indices
			 * that aren't a weapon read as false and ignore writes.
			 */
			bool get_powerup(std::uint8_t index);
			/** @brief Sets whether the given weapon is powered up */
			bool set_powerup(std::uint8_t index, bool value);
			/**
			 * @brief Returns how much ammo the player has of the given weapon
			 *
			 * Indexed by a 1-based @ref WEAPON::Weapon number, in whole rounds, @ref WEAPON::CURRENT is the selected weapon.
			 * Other indices that aren't a weapon read as 0 and ignore writes. For a weapon with @ref jjWEAPON::infinite set, assigning a positive
			 * value gives the player unlimited ammo of it.
			 */
			std::int32_t get_ammo(std::uint8_t index) const;
			/** @brief Sets how much ammo the player has of the given weapon */
			std::int32_t set_ammo(std::uint8_t index, std::int32_t value);

			/**
			 * @brief Moves the player by the given offset at once
			 *
			 * The move skips the warp effect, which makes it suitable for levels that loop seamlessly.
			 * @param xPixels Pixels to move to the right
			 * @param yPixels Pixels to move down
			 * @return Always true
			 */
			bool offsetPosition(std::int32_t xPixels, std::int32_t yPixels);
			/**
			 * @brief Warps the player to the center of the given tile
			 *
			 * @param xTile Horizontal tile coordinate
			 * @param yTile Vertical tile coordinate
			 * @param fast If true, the player moves at once instead of using the warp effect
			 * @return Always true
			 */
			bool warpToTile(std::int32_t xTile, std::int32_t yTile, bool fast);
			/**
			 * @brief Warps the player to the warp target with the given ID
			 *
			 * @param warpID ID of the Warp Target event
			 * @param fast If true, the player moves at once instead of using the warp effect
			 * @return False if the level has no warp target with that ID
			 */
			bool warpToID(std::uint8_t warpID, bool fast);

			/**
			 * @brief Changes the player into the next character
			 *
			 * Cycles through Jazz, Spaz, Lori (TSF only), the bird and the frog, skipping characters whose sprites
			 * aren't available.
			 * @param rabbitsOnly Whether to cycle only through the rabbits
			 * @param morphEffect Whether to show the morphing effect
			 * @return The player's character afterwards
			 */
			std::uint32_t morph(bool rabbitsOnly, bool morphEffect);
			/**
			 * @brief Changes the player into the given character
			 *
			 * The player stays as it is if the sprites of that character aren't available.
			 * @param charNew The new character
			 * @param morphEffect Whether to show the morphing effect
			 * @return The player's character afterwards
			 */
			std::uint32_t morphTo(std::uint32_t charNew, bool morphEffect);
			/**
			 * @brief Changes the player back into its original character
			 *
			 * Works like the Revert Morph event, the player becomes charOrig.
			 * @param morphEffect Whether to show the morphing effect
			 * @return The player's character afterwards
			 */
			std::uint32_t revertMorph(bool morphEffect);
			/**
			 * @brief Returns the player's current character
			 *
			 * A @ref CHAR::Char value, which is also the index of the animation set the character uses. Change it with
			 * @ref morphTo() or @ref revertMorph().
			 */
			std::uint32_t get_charCurr() const;

			/**
			 * @brief Returns the character the player started the level as
			 *
			 * The character @ref revertMorph() and the Revert Morph event change the player back into. Only @ref CHAR::JAZZ,
			 * @ref CHAR::SPAZ and @ref CHAR::LORI are accepted when assigning it, other values are ignored.
			 */
			std::uint32_t get_charOrig() const;
			/** @brief Sets the character the player started the level as */
			std::uint32_t set_charOrig(std::uint32_t value);

			/** @brief Kills the player at once */
			void kill();
			/**
			 * @brief Hurts the player
			 *
			 * @param damage Number of hearts to take away
			 * @param forceHurt If true, any invincibility or blinking ends first, so it doesn't block the hit
			 * @param attacker Currently ignored
			 * @return Whether the player lost health
			 * @partialsupport The `attacker` parameter is currently ignored.
			 */
			bool hurt(std::int8_t damage, bool forceHurt, jjPLAYER* attacker);

			/**
			 * @brief Returns the state of the player's timer
			 *
			 * A @ref TIMER::State value. Change it with @ref timerStart(), @ref timerPause(), @ref timerResume() and @ref timerStop().
			 */
			std::uint32_t get_timerState() const;
			/**
			 * @brief Returns whether the player's timer keeps running when the player dies
			 *
			 * If false, dying stops the timer.
			 */
			bool get_timerPersists() const;
			/** @brief Sets whether the player's timer keeps running when the player dies */
			bool set_timerPersists(bool value);
			/**
			 * @brief Starts the player's timer
			 *
			 * @param ticks Time on the timer in ticks
			 * @param startPaused Whether the timer starts paused
			 * @return @ref TIMER::PAUSED if `startPaused` is true, @ref TIMER::STARTED otherwise
			 * @partialsupport The timer isn't shown on the HUD.
			 */
			std::uint32_t timerStart(std::int32_t ticks, bool startPaused);
			/**
			 * @brief Pauses the player's timer
			 *
			 * Only a running timer is paused, a stopped one stays stopped.
			 * @return The timer state afterwards
			 */
			std::uint32_t timerPause();
			/**
			 * @brief Resumes the player's paused timer
			 *
			 * @return The timer state afterwards
			 */
			std::uint32_t timerResume();
			/**
			 * @brief Stops the player's timer
			 *
			 * The timer function isn't called.
			 * @return @ref TIMER::STOPPED
			 */
			std::uint32_t timerStop();
			/** @brief Returns how many ticks are left on the player's timer */
			std::int32_t get_timerTime() const;
			/** @brief Sets how many ticks are left on the player's timer */
			std::int32_t set_timerTime(std::int32_t value);
			/**
			 * @brief Sets the function called when the player's timer runs out, by its name
			 *
			 * The function is called only when the timer reaches zero by itself. Without one, @ref onPlayerTimerEnd() is
			 * called. Passing the function itself instead of its name is preferred.
			 */
			void timerFunction(const String& functionName);
			/**
			 * @brief Sets the function called when the player's timer runs out
			 *
			 * The function is called only when the timer reaches zero by itself.
			 */
			void timerFunctionPtr(void* function);
			/**
			 * @brief Sets the function called when the player's timer runs out
			 *
			 * The function is called only when the timer reaches zero by itself, with the player whose timer ran out.
			 */
			void timerFunctionFuncPtr(void* function);

			/**
			 * @brief Activates the bosses of the level
			 *
			 * Works like the player touching an Activate Boss event.
			 * @param activate Currently ignored, the bosses are activated either way
			 * @return Always true
			 * @partialsupport The `activate` parameter is currently ignored.
			 */
			bool activateBoss(bool activate);
			/**
			 * @brief Limits horizontal scrolling of the camera
			 *
			 * Works like a Limit X Scroll event.
			 * @param left Left edge in tiles
			 * @param width Width in tiles
			 * @return Always true
			 */
			bool limitXScroll(std::uint16_t left, std::uint16_t width);
			/**
			 * @brief Fixes the camera at the given position
			 *
			 * The camera stays there until @ref cameraUnfreeze() is called.
			 * @param xPixel Horizontal position in pixels
			 * @param yPixel Vertical position in pixels
			 * @param centered If true, the position is the center of the view, otherwise its top left corner
			 * @param instant If true, the camera jumps there, otherwise it scrolls there smoothly
			 */
			void cameraFreezeFF(float xPixel, float yPixel, bool centered, bool instant);
			/**
			 * @brief Fixes the camera vertically
			 *
			 * @param xUnfreeze If true, the camera follows the player horizontally again, otherwise the horizontal axis
			 *		stays as it is
			 * @param yPixel Vertical position in pixels
			 * @param centered If true, the position is the center of the view, otherwise its top edge
			 * @param instant If true, the camera jumps there, otherwise it scrolls there smoothly
			 */
			void cameraFreezeBF(bool xUnfreeze, float yPixel, bool centered, bool instant);
			/**
			 * @brief Fixes the camera horizontally
			 *
			 * @param xPixel Horizontal position in pixels
			 * @param yUnfreeze If true, the camera follows the player vertically again, otherwise the vertical axis
			 *		stays as it is
			 * @param centered If true, the position is the center of the view, otherwise its left edge
			 * @param instant If true, the camera jumps there, otherwise it scrolls there smoothly
			 */
			void cameraFreezeFB(float xPixel, bool yUnfreeze, bool centered, bool instant);
			/**
			 * @brief Releases or keeps each axis of the camera
			 *
			 * Passing true for both axes is the same as @ref cameraUnfreeze().
			 * @param xUnfreeze If true, the camera follows the player horizontally again
			 * @param yUnfreeze If true, the camera follows the player vertically again
			 * @param centered Has no effect with this overload
			 * @param instant If true, the camera jumps back to the player, otherwise it scrolls there smoothly
			 */
			void cameraFreezeBB(bool xUnfreeze, bool yUnfreeze, bool centered, bool instant);
			/**
			 * @brief Lets the camera follow the player again
			 *
			 * @param instant If true, the camera jumps back to the player, otherwise it scrolls there smoothly
			 */
			void cameraUnfreeze(bool instant);
			/**
			 * @brief Shows text on the screen
			 *
			 * The text appears the way a Text event shows it.
			 * @param text Text to show
			 * @param size @ref STRING::Size of the text
			 * @partialsupport Every player sees the text, not only this one.
			 */
			void showText(const String& text, std::uint32_t size);
			/**
			 * @brief Shows one of the level's text strings on the screen
			 *
			 * Works like a Text event.
			 * @param textID Index of the text string in the level
			 * @param offset Which part of the string to show, when it is split into parts by `|` characters
			 * @param size @ref STRING::Size of the text
			 * @partialsupport Every player sees the text, not only this one.
			 */
			void showTextByID(std::uint32_t textID, std::uint32_t offset, std::uint32_t size);

			/**
			 * @brief Returns how the player is flying
			 *
			 * @ref FLIGHT::FLYCARROT while using a copter, @ref FLIGHT::AIRBOARD while on an airboard, @ref FLIGHT::NONE otherwise.
			 * Assigning one of these values starts or ends flying accordingly.
			 */
			std::uint32_t get_fly() const;
			/** @brief Sets how the player is flying */
			std::uint32_t set_fly(std::uint32_t value);

			/**
			 * @brief Makes the player fire in a direction
			 *
			 * @partialsupport TNT and Thunderbolt aren't fired, it returns 0 for them. A weapon the script doesn't control fires
			 * one bullet, without the spread of its powered-up shots.
			 */
			std::int32_t fireBulletDirection(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, std::uint32_t direction);
			/**
			 * @brief Makes the player fire at an angle
			 *
			 * @partialsupport TNT and Thunderbolt aren't fired, it returns 0 for them. A weapon the script doesn't control fires
			 * one bullet, without the spread of its powered-up shots.
			 */
			std::int32_t fireBulletAngle(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, float angle);

			/**
			 * @brief Returns the left edge of the player's part of the screen
			 *
			 * In pixels from the left edge of the window, 0 unless the screen is split.
			 */
			std::int32_t get_subscreenX() const;
			/**
			 * @brief Returns the top edge of the player's part of the screen
			 *
			 * In pixels from the top edge of the window, 0 unless the screen is split.
			 */
			std::int32_t get_subscreenY() const;

			/**
			 * @brief Returns the left edge of the player's view of the level
			 *
			 * In pixels, measured in layer 4.
			 */
			float get_cameraX() const;
			/**
			 * @brief Returns the top edge of the player's view of the level
			 *
			 * In pixels, measured in layer 4.
			 */
			float get_cameraY() const;
			/**
			 * @brief Returns how many times the player died in a competitive game
			 *
			 * @partialsupport Currently always 0.
			 */
			std::int32_t get_deaths() const;

			/**
			 * @brief Returns whether the player is in jail in Jailbreak
			 *
			 * @partialsupport Currently always false.
			 */
			bool get_isJailed() const;
			/**
			 * @brief Returns whether the player is a zombie in Pestilence
			 *
			 * @partialsupport Currently always false.
			 */
			bool get_isZombie() const;
			/**
			 * @brief Returns the lives left in last-rabbit-standing game modes
			 *
			 * @partialsupport Currently always 0.
			 */
			std::int32_t get_lrsLives() const;
			/**
			 * @brief Returns how many players this player roasted
			 *
			 * @partialsupport Currently always 0.
			 */
			std::int32_t get_roasts() const;
			/**
			 * @brief Returns how many laps the player finished in a race
			 *
			 * @partialsupport Currently always 0.
			 */
			std::int32_t get_laps() const;
			/**
			 * @brief Returns how many ticks the current lap has lasted
			 *
			 * @partialsupport Currently always 0.
			 */
			std::int32_t get_lapTimeCurrent() const;
			/**
			 * @brief Returns the time of one of the last laps in ticks
			 *
			 * Index 0 is the most recent lap.
			 * @partialsupport Currently always 0.
			 */
			std::int32_t get_lapTimes(std::uint32_t index) const;
			/**
			 * @brief Returns the time of the player's fastest lap in ticks
			 *
			 * @partialsupport Currently always 0.
			 */
			std::int32_t get_lapTimeBest() const;
			/**
			 * @brief Returns whether the player is logged in as a server administrator
			 *
			 * @partialsupport Currently always false.
			 */
			bool get_isAdmin() const;
			/**
			 * @brief Returns whether the player has the given administrator privilege
			 *
			 * @partialsupport Currently always returns false.
			 */
			bool hasPrivilege(const String& privilege, std::uint32_t moduleID) const;

			/**
			 * @brief Returns whether the player touches the given object
			 *
			 * The pixels of the frame the object shows are tested against the player's hitbox.
			 * @param object The object to test
			 * @param always Currently ignored
			 * @partialsupport The `always` parameter is currently ignored.
			 */
			bool doesCollide(const jjOBJ* object, bool always) const;
			/**
			 * @brief Returns how hard the player would hit an object
			 *
			 * 1 during a buttstomp or a sugar rush, -1 during another special move, -101 when running fast into the
			 * frozen @p target, 0 otherwise.
			 */
			std::int32_t getObjectHitForce(const jjOBJ* target) const;
			/**
			 * @brief Makes the player collide with an object
			 *
			 * The collision is handled as if the two touched, wherever they are. An object with scripted collisions gets
			 * its @ref onObjectHit() called with this player, otherwise the game handles the collision itself, for example
			 * by collecting a pickup.
			 * @param target The object to collide with
			 * @param force How hard the player hits, used only by scripted collisions: 0 for a plain touch, 1 for a
			 *		buttstomp or sugar rush, -1 for a special move
			 * @param playerHandling Currently ignored
			 * @return Whether the object was affected
			 * @partialsupport The `playerHandling` parameter is ignored, and so is `force` for collisions the game handles.
			 */
			bool objectHit(jjOBJ* target, std::int32_t force, std::uint32_t playerHandling);
			/**
			 * @brief Returns whether this player can hurt the given player
			 *
			 * @partialsupport Currently always returns false.
			 */
			bool isEnemy(const jjPLAYER* victim) const;

			/**
			 * @brief Returns the sprite mode the player is drawn with
			 *
			 * A @ref SPRITE::Mode value. Other modes are kept, but the player is drawn as usual.
			 * @partialsupport Currently only @ref SPRITE::INVISIBLE has an effect, it hides the player.
			 */
			std::uint32_t get_spriteMode() const;
			/** @brief Sets the sprite mode the player is drawn with */
			std::uint32_t set_spriteMode(std::uint32_t value);
			/** @brief Hides the player while `onPlayerDraw()` of the script draws it in another frame */
			void SetDrawnByScript(bool value);
			/**
			 * @brief Returns the parameter of the sprite mode
			 *
			 * @partialsupport Currently only kept, it doesn't change how the player is drawn.
			 */
			std::uint8_t get_spriteParam() const;
			/** @brief Sets the parameter of the sprite mode */
			std::uint8_t set_spriteParam(std::uint8_t value);

			/**
			 * @brief Returns the animation the player shows
			 *
			 * An index into @ref jjAnimations, 0 if the animation shown has no counterpart there.
			 */
			std::uint16_t get_curAnim() const;
			/**
			 * @brief Returns the sprite frame the player shows
			 *
			 * An index into @ref jjAnimFrames, equal to the first frame of curAnim plus frameID.
			 */
			std::uint32_t get_curFrame() const;
			/**
			 * @brief Returns the frame the player shows within its animation
			 *
			 * 0 is the first frame of curAnim.
			 */
			std::uint8_t get_frameID() const;

			/**
			 * @brief Fires the bullets of a weapon whose bullets a script controls, see @ref LevelScriptLoader::FirePlayerWeapon()
			 *
			 * With @p aimAngle (0 up, clockwise) one bullet at that angle, as `fireBullet()`, @p lastShot gets the last one.
			 */
			bool FireScriptedWeapon(WeaponType weaponType, float aimAngle = -1.0f, Actors::ActorBase** lastShot = nullptr);
			/** @brief Moves the player by the `ySpeed` a behavior of an object set in an earlier tick, as JJ2+ moves the players before the objects */
			void ApplyPendingSpeed(std::int32_t currentTick);

		private:
			LevelScriptLoader* _levelScriptLoader;
			Actors::Player* _player;

			// `ballTime` a script gave the player and the tick it did
			std::int32_t _ballTime = 0;
			std::int32_t _ballTimeTick = 0;
			// `ySpeed` a behavior of an object set and the tick it did, the player moves by it from the next tick
			float _pendingYSpeed = 0.0f;
			std::int32_t _pendingYSpeedTick = 0;
			bool _hasPendingYSpeed = false;

			void ApplyYSpeed(float value);

			void* _timerCallback;
			std::uint32_t _timerState;
			float _timerLeft;
			bool _timerPersists;

			// `bossActive` as it was given to the script last, a change of the script stays
			bool _syncedBossActive = false;
			bool _syncedOnce = false;
			// The object the player pushed in that tick, `platform` names it (see jjOBJ::beSolid())
			std::int32_t _pushedObject = 0;
			std::int32_t _pushedTick = INT32_MIN;

			// Finds the animation of the original the player shows and the frame of it, false if it's none of them
			bool FindShownFrame(std::uint32_t& anim, std::uint32_t& position, std::uint32_t& firstFrame) const;
			// Returns the count of gems of a GEM::Color, `nullptr` if there's no such color or player
			std::int32_t* GetGemCount(std::uint32_t type) const;

			// SPRITE::NORMAL and its parameter
			std::uint32_t _spriteMode = 0;
			std::uint8_t _spriteParam = 0;
			// `onPlayerDraw()` of the script draws the player in this tick, the player doesn't draw itself (see SetDrawnByScript())
			bool _drawnByScript = false;

			// Hands the update of a player of SPRITE::INVISIBLE back to the player, which isn't drawn then
			class InvisibleOverride;
			static void RunPlayerUpdate(Actors::Player* player, float timeMult);
			// Installs or removes the override, the player is hidden while it's invisible or drawn by the script
			void UpdateDrawOverride();

			// The ammo JJ2+ shows of each weapon of `jjWEAPON::infinite`, the player has unlimited ammo of it
			std::uint16_t _infiniteAmmo[std::int32_t(WeaponType::Count)] = {};

			// Fires one bullet of the spread, -1 angled up, 0 straight, 1 angled down
			template<typename T>
			Actors::ActorBase* FireSpreadShot(WeaponType weaponType, std::int8_t slot, float aimAngle = -1.0f);
			// Fires one shot of a weapon of the game at an angle (0 up, clockwise), see fireBulletAngle()
			template<typename T>
			Actors::ActorBase* FireAimedNativeShot(WeaponType weaponType, float aimAngle);

			// Gives the properties what the player is now, except `bossActive` if a script changed it
			void SyncPropertiesToBackingStore();
		};

		/**
		 * @brief Options of one weapon
		 *
		 * Reached through jjWeapons. These are the options the game itself plays the level by, so changing them
		 * changes how the weapon behaves for every player.
		 */
		using jjWEAPON = WeaponOptions;

		/**
		 * @brief Character profile
		 *
		 * Movement and ability settings shared by every player using one character, reached through @ref jjCharacters.
		 * Currently every character returns the same placeholder profile and none of its properties have any effect.
		 */
		class jjCHARACTER
		{
		public:
			/**
			 * @brief What the character does when jump is pressed in the air
			 *
			 * An @ref AIR::Jump value.
			 * @partialsupport Currently reads 0 or false, not the default of the character, and has no effect.
			 */
			int airJump;
			/**
			 * @brief What the character does when jump is pressed while crouching
			 *
			 * A @ref GROUND::Jump value.
			 * @partialsupport Currently reads 0 or false, not the default of the character, and has no effect.
			 */
			int groundJump;
			/**
			 * @brief Most jumps the character can make in the air
			 *
			 * Applies when airJump is @ref AIR::DOUBLEJUMP.
			 * @partialsupport Currently reads 0 or false, not the default of the character, and has no effect.
			 */
			int doubleJumpCountMax;
			/**
			 * @brief Horizontal speed gained by a double jump
			 *
			 * Relative to the direction the player faces, negative values push backwards.
			 * @partialsupport Currently reads 0 or false, not the default of the character, and has no effect.
			 */
			float doubleJumpXSpeed;
			/**
			 * @brief Vertical speed gained by a double jump
			 *
			 * @partialsupport Currently reads 0 or false, not the default of the character, and has no effect.
			 */
			float doubleJumpYSpeed;
			/**
			 * @brief Longest time the character can use helicopter ears
			 *
			 * Applies when airJump is @ref AIR::HELICOPTER.
			 * @partialsupport Currently reads 0 or false, not the default of the character, and has no effect.
			 */
			int helicopterDurationMax;
			/**
			 * @brief Horizontal speed while using helicopter ears
			 *
			 * Relative to the direction the player faces, negative values push backwards.
			 * @partialsupport Currently reads 0 or false, not the default of the character, and has no effect.
			 */
			float helicopterXSpeed;
			/**
			 * @brief Vertical speed while using helicopter ears
			 *
			 * @partialsupport Currently reads 0 or false, not the default of the character, and has no effect.
			 */
			float helicopterYSpeed;
			/**
			 * @brief Whether the character's special moves cause damage
			 *
			 * @partialsupport Currently reads 0 or false, not the default of the character, and has no effect.
			 */
			bool canHurt;
			/**
			 * @brief Whether the character can run
			 *
			 * @partialsupport Currently reads 0 or false, not the default of the character, and has no effect.
			 */
			bool canRun;
			/**
			 * @brief Whether morph monitors can turn a player into the character
			 *
			 * @partialsupport Currently reads 0 or false, not the default of the character, and has no effect.
			 */
			bool morphBoxCycle;
		};

		/**
		 * @brief How a player is drawn in this tick
		 *
		 * Passed to the @ref onPlayerDraw() hook, already filled in with how the game would draw the player. Changes apply
		 * to this one drawing only. If the script changes curFrame, angle, xOffset, yOffset, xScale, yScale,
		 * spriteMode or spriteParam, the given frame is drawn in place of the player until the next tick. Setting
		 * sprite to false hides the player.
		 */
		struct jjPLAYERDRAW
		{
			/**
			 * @brief Whether the player's name is drawn
			 *
			 * @partialsupport Currently ignored.
			 */
			bool name;
			/** @brief Whether the player's sprite is drawn */
			bool sprite;
			/**
			 * @brief Whether the sugar rush effect is drawn
			 *
			 * @partialsupport Currently ignored.
			 */
			bool sugarRush;
			/**
			 * @brief Whether the muzzle flash is drawn
			 *
			 * @partialsupport Currently ignored.
			 */
			bool gunFlash;
			/**
			 * @brief Whether the invincibility effect is drawn
			 *
			 * @partialsupport Currently ignored.
			 */
			bool invincibility;
			/**
			 * @brief Whether the trail behind a fast player is drawn
			 *
			 * @partialsupport Currently ignored.
			 */
			bool trail;
			/**
			 * @brief Whether the explosions of morphing are drawn
			 *
			 * @partialsupport Currently ignored.
			 */
			bool morphingExplosions;
			/**
			 * @brief Whether the player bobs up and down as on an airboard
			 *
			 * True by default while the player is on an airboard.
			 * @partialsupport Currently ignored.
			 */
			bool airboardBouncingMotion;
			/**
			 * @brief Whether puffs of an airboard are drawn behind the player
			 *
			 * True by default while the player is on an airboard.
			 * @partialsupport Currently ignored.
			 */
			bool airboardPuff;
			/**
			 * @brief Sprite mode used to draw the player
			 *
			 * Defaults to @ref jjPLAYER::spriteMode.
			 */
			std::int32_t spriteMode;
			/**
			 * @brief Parameter of the sprite mode
			 *
			 * Defaults to @ref jjPLAYER::spriteParam.
			 */
			std::uint8_t spriteParam;
			/**
			 * @brief Type of light the player emits
			 *
			 * @partialsupport Currently ignored.
			 */
			std::int32_t lightType;
			/**
			 * @brief Intensity of the light the player emits
			 *
			 * @partialsupport Currently ignored.
			 */
			std::int8_t light;
			/**
			 * @brief Layer the player is drawn in front of
			 *
			 * 4 by default.
			 */
			std::int32_t layer;
			/**
			 * @brief Sprite frame to draw
			 *
			 * An index into @ref jjAnimFrames, by default @ref jjPLAYER::curFrame. Changing it only changes the look, collisions
			 * still use the player's real frame.
			 */
			std::uint32_t curFrame;
			/**
			 * @brief Angle the sprite is drawn at
			 *
			 * Uses the same units as @ref jjCANVAS::drawRotatedSprite(). 0 by default.
			 */
			std::int32_t angle;
			/**
			 * @brief Horizontal offset of the drawing from the player's position in pixels
			 *
			 * 0 by default.
			 */
			float xOffset;
			/**
			 * @brief Vertical offset of the drawing from the player's position in pixels
			 *
			 * 0 by default.
			 */
			float yOffset;
			/**
			 * @brief Horizontal scale of the drawing
			 *
			 * 1 or -1 by default, depending on which way the player faces.
			 */
			float xScale;
			/**
			 * @brief Vertical scale of the drawing
			 *
			 * 1 by default.
			 */
			float yScale;
			/**
			 * @brief Color of the flag drawn behind the player
			 *
			 * A @ref TEAM::Color value.
			 * @partialsupport Currently ignored.
			 */
			std::int32_t flag;
			/** @brief The player this draw configuration belongs to */
			jjPLAYER* _player;

			/**
			 * @brief Returns whether the given shield is drawn around the player
			 *
			 * Indexed by @ref SHIELD::Shield.
			 * @partialsupport Currently always false, and assigning it has no effect.
			 */
			bool get_shield(std::int32_t shield) const;
			/** @brief Sets whether the given shield is drawn around the player */
			bool set_shield(std::int32_t shield, bool enable);
			/**
			 * @brief Returns the player being drawn
			 *
			 * The handle can't be replaced, but the player's own properties can be changed through it.
			 */
			jjPLAYER* get_player() const;
		};
	}
}

namespace Jazz2::Scripting
{
	using namespace Legacy;

	/** @brief Returns the number of players in the level */
	std::int32_t get_jjPlayerCount();
	/**
	 * @brief Returns the number of local players
	 *
	 * Currently the same as @ref jjPlayerCount, every player in the level is local.
	 */
	std::int32_t get_jjLocalPlayerCount();

	/**
	 * @brief Returns the current player
	 *
	 * Always the first player. Prefer @ref jjLocalPlayers or @ref jjPlayers, which work with more than one player.
	 */
	jjPLAYER* get_jjP();
	/**
	 * @brief Returns the player with the given index
	 *
	 * Never null, an index without a player returns a handle with @ref jjPLAYER::isActive set to false.
	 */
	jjPLAYER* get_jjPlayers(std::uint8_t index);
	/**
	 * @brief Returns the local player with the given index
	 *
	 * Currently the same as @ref jjPlayers, every player in the level is local.
	 */
	jjPLAYER* get_jjLocalPlayers(std::uint8_t index);

	/**
	 * @brief Returns the options of the given weapon
	 *
	 * Indexed by a 1-based @ref WEAPON::Weapon number. Other indices return a scratch object whose changes have no
	 * effect.
	 */
	jjWEAPON* get_jjWEAPON(int index);

	/**
	 * @brief Returns the profile of the given character
	 *
	 * Indexed by @ref CHAR::Char. Currently every character shares one placeholder profile whose changes have no effect.
	 */
	jjCHARACTER* get_jjCHARACTER(int index);
}

#endif
