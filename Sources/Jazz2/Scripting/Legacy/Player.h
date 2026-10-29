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
		 * Primary script interface to a player, wrapping an engine @ref Actors::Player and mirroring the original JJ2+
		 * `jjPLAYER`. Exposes position, speed, health, lives, score, weapons/ammo, the active character and a large set
		 * of state queries and actions (freeze, morph, sugar rush, warps, etc.). The level loader owns one backing
		 * instance per player (see @ref LevelScriptLoader::GetPlayerBackingStore), reached from script via `jjPlayers`.
		 */
		class jjPLAYER
		{
			friend class LevelScriptLoader;
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
			/** @brief Returns the score currently shown on the HUD */
			std::int32_t get_scoreDisplayed() const;
			/** @brief Sets the score currently shown on the HUD */
			std::int32_t set_scoreDisplayed(std::int32_t value);

			/** @brief Sets the player's score */
			std::int32_t setScore(std::int32_t value);

			/** @brief Returns the horizontal position in pixels */
			float get_xPos() const;
			/** @brief Sets the horizontal position in pixels */
			float set_xPos(float value);
			/** @brief Returns the vertical position in pixels */
			float get_yPos() const;
			/** @brief Sets the vertical position in pixels */
			float set_yPos(float value);
			/** @brief Returns the horizontal acceleration */
			float get_xAcc() const;
			/** @brief Sets the horizontal acceleration */
			float set_xAcc(float value);
			/** @brief Returns the vertical acceleration */
			float get_yAcc() const;
			/** @brief Sets the vertical acceleration */
			float set_yAcc(float value);
			/** @brief Returns the original (spawn) horizontal position */
			float get_xOrg() const;
			/** @brief Sets the original (spawn) horizontal position */
			float set_xOrg(float value);
			/** @brief Returns the original (spawn) vertical position */
			float get_yOrg() const;
			/** @brief Sets the original (spawn) vertical position */
			float set_yOrg(float value);
			/** @brief Returns the horizontal speed */
			float get_xSpeed();
			/** @brief Sets the horizontal speed */
			float set_xSpeed(float value);
			/** @brief Returns the vertical speed */
			float get_ySpeed();
			/** @brief Sets the vertical speed */
			float set_ySpeed(float value);

			/** @brief Strength of the player's jump */
			float jumpStrength = 0.0f;
			/** @brief Returns the number of ticks the player remains frozen */
			std::int8_t get_frozen() const;
			/** @brief Sets the number of ticks the player remains frozen */
			std::int8_t set_frozen(std::int8_t value);

			/** @brief Freezes or unfreezes the player */
			void freeze(bool frozen);
			/** @brief Returns the tile the player currently occupies */
			std::int32_t get_currTile();
			/** @brief Starts a sugar rush lasting the given number of ticks */
			bool startSugarRush(std::int32_t time);
			/** @brief Returns the player's health */
			std::int8_t get_health() const;
			/** @brief Sets the player's health */
			std::int8_t set_health(std::int8_t value);

			/** @brief Id of the warp the player is using */
			std::int32_t warpID = 0;

			/** @brief Returns the player's fast-fire level */
			std::int32_t get_fastfire() const;
			/** @brief Sets the player's fast-fire level */
			std::int32_t set_fastfire(std::int32_t value);
			/** @brief Returns the currently selected weapon */
			std::int8_t get_currWeapon() const;
			/** @brief Sets the currently selected weapon */
			std::int8_t set_currWeapon(std::int8_t value);

			/** @brief Returns the number of remaining lives */
			std::int32_t get_lives() const;
			/** @brief Sets the number of remaining lives */
			std::int32_t set_lives(std::int32_t value);

			/** @brief Returns the remaining invincibility duration */
			std::int32_t get_invincibility() const;
			/** @brief Sets the remaining invincibility duration */
			std::int32_t set_invincibility(std::int32_t value);
			std::uint32_t get_lightType() const;
			std::uint32_t set_lightType(std::uint32_t value);
			/** @brief Returns the remaining blink (post-hit flicker) duration */
			std::int32_t get_blink() const;
			/** @brief Sets the remaining blink (post-hit flicker) duration */
			std::int32_t set_blink(std::int32_t value);
			/** @brief Extends the player's invincibility by the given duration */
			std::int32_t extendInvincibility(std::int32_t duration);
			/** @brief Returns the player's food count */
			std::int32_t get_food() const;
			/** @brief Sets the player's food count */
			std::int32_t set_food(std::int32_t value);
			/** @brief Returns the player's coin count */
			std::int32_t get_coins() const;
			/** @brief Sets the player's coin count */
			std::int32_t set_coins(std::int32_t value);
			/** @brief Spends the given number of coins if the player has enough */
			bool testForCoins(std::int32_t numberOfCoins);
			/** @brief Returns the player's gem count of the given type */
			std::int32_t get_gems(std::uint32_t type) const;
			/** @brief Sets the player's gem count of the given type */
			std::int32_t set_gems(std::uint32_t type, std::int32_t value);
			/** @brief Spends the given number of gems of a type if the player has enough */
			bool testForGems(std::int32_t numberOfGems, std::uint32_t type);

			/** @brief Returns the active shield type */
			std::int32_t get_shieldType() const;
			/** @brief Sets the active shield type */
			std::int32_t set_shieldType(std::int32_t value);
			/** @brief Returns the remaining shield duration */
			std::int32_t get_shieldTime() const;
			/** @brief Sets the remaining shield duration */
			std::int32_t set_shieldTime(std::int32_t value);
			/** @brief Returns the remaining roll duration */
			std::int32_t get_rolling() const;
			/** @brief Sets the remaining roll duration */
			std::int32_t set_rolling(std::int32_t value);

			/** @brief Index of the boss assigned to the player */
			std::int32_t bossNumber = 0;
			/** @brief Object id of the player's boss */
			std::int32_t boss = 0;
			/** @brief Whether the player's boss is active */
			bool bossActive = false;
			/** @brief Returns the facing direction of the player */
			std::int8_t get_direction() const;
			/** @brief Sets the facing direction of the player, 0 keeps it */
			std::int8_t set_direction(std::int8_t value);
			/** @brief Object id of the platform the player is standing on */
			std::int32_t platform = 0;
			/** @brief Capture-the-flag flag state */
			std::int32_t flag = 0;
			/** @brief Network client id of the player */
			std::int32_t clientID = 0;

			/** @brief Returns the player's id */
			std::int8_t get_playerID() const;
			/** @brief Returns the player's local id */
			std::int32_t get_localPlayerID() const;

			/** @brief Team the player belongs to */
			bool team = false;

			/** @brief Returns whether the player is running */
			bool get_running() const;
			/** @brief Sets whether the player is running */
			bool set_running(bool value);

			/** @brief Returns the number of double jumps made in the air */
			std::int32_t get_doubleJumpCount() const;
			/** @brief Sets the number of double jumps made in the air */
			std::int32_t set_doubleJumpCount(std::int32_t value);
			/** @brief Returns whether the player made a double jump in the air */
			bool get_alreadyDoubleJumped() const;
			/** @brief Sets whether the player made a double jump in the air */
			bool set_alreadyDoubleJumped(bool value);

			/** @brief Returns the remaining stoned (dizzy) duration */
			std::int32_t get_stoned();
			/** @brief Sets the remaining stoned (dizzy) duration */
			std::int32_t set_stoned(std::int32_t value);

			/** @brief Remaining buttstomp duration */
			std::int32_t buttstomp = 0;
			/** @brief Returns the remaining helicopter (copter) duration in ticks */
			std::int32_t get_helicopter() const;
			/** @brief Sets the remaining helicopter (copter) duration in ticks */
			std::int32_t set_helicopter(std::int32_t value);
			/** @brief Elapsed helicopter (copter) time */
			std::int32_t helicopterElapsed = 0;
			/** @brief Active special move */
			std::int32_t specialMove = 0;
			/** @brief Returns the number of ticks the player has been idle */
			std::int32_t get_idle() const;
			/** @brief Sets the number of ticks the player has been idle */
			std::int32_t set_idle(std::int32_t value);

			/** @brief Moves the player through a sucker tube at the given speed */
			void suckerTube(std::int32_t xSpeed, std::int32_t ySpeed, bool center, bool noclip, bool trigSample);
			/** @brief Spins the player off a pole at the given speed */
			void poleSpin(float xSpeed, float ySpeed, std::uint32_t delay);
			/** @brief Bounces the player off a spring at the given speed */
			void spring(float xSpeed, float ySpeed, bool keepZeroSpeeds, bool sample);

			/** @brief Whether the player is local to this client */
			bool isLocal = true;
			/** @brief Whether the player is currently active */
			bool isActive = true;

			/** @brief Returns whether the player is still connecting */
			bool get_isConnecting() const;
			/** @brief Returns whether the player is idle */
			bool get_isIdle() const;
			/** @brief Returns whether the player is out of the game */
			bool get_isOut() const;
			/** @brief Returns whether the player is spectating */
			bool get_isSpectating() const;
			/** @brief Returns whether the player is currently in the game */
			bool get_isInGame() const;

			/** @brief Returns the player's display name */
			String get_name() const;
			/** @brief Returns the player's name without formatting codes */
			String get_nameUnformatted() const;
			/** @brief Sets the player's display name */
			bool setName(const String& name);
			/** @brief Returns the brightness of the light around the player */
			std::int8_t get_light() const;
			/** @brief Sets the brightness of the light around the player */
			std::int8_t set_light(std::int8_t value);
			/** @brief Returns the packed fur (recolor) value */
			std::uint32_t get_fur() const;
			/** @brief Sets the packed fur (recolor) value */
			std::uint32_t set_fur(std::uint32_t value);
			/** @brief Outputs the player's four fur (recolor) palette indices */
			void getFur(std::uint8_t& a, std::uint8_t& b, std::uint8_t& c, std::uint8_t& d) const;
			/** @brief Sets the player's four fur (recolor) palette indices */
			void setFur(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d);

			/** @brief Returns whether the player is prevented from firing */
			bool get_noFire() const;
			/** @brief Sets whether the player is prevented from firing */
			bool set_noFire(bool value);
			/** @brief Returns whether anti-gravity is active */
			bool get_antiGrav() const;
			/** @brief Sets whether anti-gravity is active */
			bool set_antiGrav(bool value);
			/** @brief Returns whether the player is invisible */
			bool get_invisibility() const;
			/** @brief Sets whether the player is invisible */
			bool set_invisibility(bool value);
			/** @brief Returns whether noclip mode is active */
			bool get_noclipMode() const;
			/** @brief Sets whether noclip mode is active */
			bool set_noclipMode(bool value);
			/** @brief Returns the player's lighting level */
			std::uint8_t get_lighting() const;
			/** @brief Sets the player's lighting level */
			std::uint8_t set_lighting(std::uint8_t value);
			/** @brief Resets the player's lighting to the default level */
			std::uint8_t resetLight();

			/** @brief Returns whether the left key is pressed */
			bool get_playerKeyLeftPressed();
			/** @brief Returns whether the right key is pressed */
			bool get_playerKeyRightPressed();
			/** @brief Returns whether the up key is pressed */
			bool get_playerKeyUpPressed();
			/** @brief Returns whether the down key is pressed */
			bool get_playerKeyDownPressed();
			/** @brief Returns whether the fire key is pressed */
			bool get_playerKeyFirePressed();
			/** @brief Returns whether the weapon-select key is pressed */
			bool get_playerKeySelectPressed();
			/** @brief Returns whether the jump key is pressed */
			bool get_playerKeyJumpPressed();
			/** @brief Returns whether the run key is pressed */
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

			/** @brief Returns whether the player has the powerup for the given weapon */
			bool get_powerup(std::uint8_t index);
			/** @brief Sets whether the player has the powerup for the given weapon */
			bool set_powerup(std::uint8_t index, bool value);
			/** @brief Returns the ammo count for the given weapon */
			std::int32_t get_ammo(std::uint8_t index) const;
			/** @brief Sets the ammo count for the given weapon */
			std::int32_t set_ammo(std::uint8_t index, std::int32_t value);

			/** @brief Moves the player by the given pixel offset */
			bool offsetPosition(std::int32_t xPixels, std::int32_t yPixels);
			/** @brief Warps the player to the given tile coordinates */
			bool warpToTile(std::int32_t xTile, std::int32_t yTile, bool fast);
			/** @brief Warps the player to the warp target with the given id */
			bool warpToID(std::uint8_t warpID, bool fast);

			/** @brief Morphs the player into a random character */
			std::uint32_t morph(bool rabbitsOnly, bool morphEffect);
			/** @brief Morphs the player into the given character */
			std::uint32_t morphTo(std::uint32_t charNew, bool morphEffect);
			/** @brief Reverts the player to the original character */
			std::uint32_t revertMorph(bool morphEffect);
			/** @brief Returns the player's current character */
			std::uint32_t get_charCurr() const;

			/** @brief Returns the player's original character, the one @ref revertMorph() turns it back into */
			std::uint32_t get_charOrig() const;
			/** @brief Sets the player's original character */
			std::uint32_t set_charOrig(std::uint32_t value);

			/** @brief Kills the player */
			void kill();
			/** @brief Damages the player and returns whether the hit was applied */
			bool hurt(std::int8_t damage, bool forceHurt, jjPLAYER* attacker);

			/** @brief Returns the state of the player's timer */
			std::uint32_t get_timerState() const;
			/** @brief Returns whether the timer persists across levels */
			bool get_timerPersists() const;
			/** @brief Sets whether the timer persists across levels */
			bool set_timerPersists(bool value);
			/** @brief Starts the player's timer for the given number of ticks */
			std::uint32_t timerStart(std::int32_t ticks, bool startPaused);
			/** @brief Pauses the player's timer */
			std::uint32_t timerPause();
			/** @brief Resumes the player's timer */
			std::uint32_t timerResume();
			/** @brief Stops the player's timer */
			std::uint32_t timerStop();
			/** @brief Returns the time remaining on the player's timer */
			std::int32_t get_timerTime() const;
			/** @brief Sets the time remaining on the player's timer */
			std::int32_t set_timerTime(std::int32_t value);
			/** @brief Sets the script function called when the timer ends, by name */
			void timerFunction(const String& functionName);
			/** @brief Sets the script function called when the timer ends, by pointer */
			void timerFunctionPtr(void* function);
			/** @brief Sets the script function called when the timer ends, by function pointer */
			void timerFunctionFuncPtr(void* function);

			/** @brief Activates or deactivates the player's boss */
			bool activateBoss(bool activate);
			/** @brief Limits horizontal camera scrolling to the given region */
			bool limitXScroll(std::uint16_t left, std::uint16_t width);
			/** @brief Freezes the camera at the given fixed position */
			void cameraFreezeFF(float xPixel, float yPixel, bool centered, bool instant);
			/** @brief Freezes the camera, leaving the horizontal axis tracking the player */
			void cameraFreezeBF(bool xUnfreeze, float yPixel, bool centered, bool instant);
			/** @brief Freezes the camera, leaving the vertical axis tracking the player */
			void cameraFreezeFB(float xPixel, bool yUnfreeze, bool centered, bool instant);
			/** @brief Freezes the camera, leaving both axes optionally tracking the player */
			void cameraFreezeBB(bool xUnfreeze, bool yUnfreeze, bool centered, bool instant);
			/** @brief Unfreezes the camera */
			void cameraUnfreeze(bool instant);
			/** @brief Shows the given text to the player */
			void showText(const String& text, std::uint32_t size);
			/** @brief Shows a string from the level text bank to the player */
			void showTextByID(std::uint32_t textID, std::uint32_t offset, std::uint32_t size);

			/** @brief Returns the remaining fly (airboard) duration */
			std::uint32_t get_fly() const;
			/** @brief Sets the remaining fly (airboard) duration */
			std::uint32_t set_fly(std::uint32_t value);

			/** @brief Fires a bullet in the given direction and returns the new bullet's id */
			std::int32_t fireBulletDirection(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, std::uint32_t direction);
			/** @brief Fires a bullet at the given angle and returns the new bullet's id */
			std::int32_t fireBulletAngle(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, float angle);

			/** @brief Returns the horizontal position of the player's split-screen subscreen */
			std::int32_t get_subscreenX() const;
			/** @brief Returns the vertical position of the player's split-screen subscreen */
			std::int32_t get_subscreenY() const;

			/** @brief Returns the camera's horizontal position */
			float get_cameraX() const;
			/** @brief Returns the camera's vertical position */
			float get_cameraY() const;
			/** @brief Returns the number of times the player has died */
			std::int32_t get_deaths() const;

			/** @brief Returns whether the player is jailed */
			bool get_isJailed() const;
			/** @brief Returns whether the player is a zombie */
			bool get_isZombie() const;
			/** @brief Returns the player's remaining lives in last-rabbit-standing mode */
			std::int32_t get_lrsLives() const;
			/** @brief Returns the number of players this player has roasted */
			std::int32_t get_roasts() const;
			/** @brief Returns the number of laps the player has completed */
			std::int32_t get_laps() const;
			/** @brief Returns the elapsed time of the current lap */
			std::int32_t get_lapTimeCurrent() const;
			/** @brief Returns the recorded time of the lap at the given index */
			std::int32_t get_lapTimes(std::uint32_t index) const;
			/** @brief Returns the player's best lap time */
			std::int32_t get_lapTimeBest() const;
			/** @brief Returns whether the player is a server administrator */
			bool get_isAdmin() const;
			/** @brief Returns whether the player holds the given privilege */
			bool hasPrivilege(const String& privilege, std::uint32_t moduleID) const;

			/** @brief Returns whether the player collides with the given object */
			bool doesCollide(const jjOBJ* object, bool always) const;
			/** @brief Returns the force with which the player would hit the given object */
			std::int32_t getObjectHitForce(const jjOBJ& target) const;
			/** @brief Handles the player hitting the target object */
			bool objectHit(jjOBJ* target, std::int32_t force, std::uint32_t playerHandling);
			/** @brief Returns whether the given player is an enemy of this player */
			bool isEnemy(const jjPLAYER* victim) const;

			/** @brief Returns how the player is drawn (`SPRITE::Mode`) */
			std::uint32_t get_spriteMode() const;
			/** @brief Sets how the player is drawn (`SPRITE::Mode`) */
			std::uint32_t set_spriteMode(std::uint32_t value);
			/** @brief Hides the player while `onPlayerDraw()` of the script draws it in another frame */
			void SetDrawnByScript(bool value);
			/** @brief Returns the parameter of the sprite mode */
			std::uint8_t get_spriteParam() const;
			/** @brief Sets the parameter of the sprite mode */
			std::uint8_t set_spriteParam(std::uint8_t value);

			/** @brief Returns the current animation index */
			std::uint16_t get_curAnim() const;
			/** @brief Returns the current sprite frame index */
			std::uint32_t get_curFrame() const;
			/** @brief Returns the current frame id within the animation */
			std::uint8_t get_frameID() const;

			/** @brief Fires the bullets of a weapon whose bullets a script controls, see @ref LevelScriptLoader::FirePlayerWeapon() */
			bool FireScriptedWeapon(WeaponType weaponType);

		private:
			LevelScriptLoader* _levelScriptLoader;
			Actors::Player* _player;

			void* _timerCallback;
			std::uint32_t _timerState;
			float _timerLeft;
			bool _timerPersists;

			// `bossActive` as it was given to the script last, a change of the script stays
			bool _syncedBossActive = false;
			bool _syncedOnce = false;

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
			void FireSpreadShot(WeaponType weaponType, std::int8_t slot);

			// Gives the properties what the player is now, except `bossActive` if a script changed it
			void SyncPropertiesToBackingStore();
		};

		/**
		 * @brief Settings for a weapon type
		 *
		 * The options of a weapon of the level, so a script changes those the game itself plays by.
		 */
		using jjWEAPON = WeaponOptions;

		/** @brief Movement and ability settings for a playable character */
		class jjCHARACTER
		{
		public:
			/** @brief Air-jump ability */
			int airJump;
			/** @brief Ground-jump ability */
			int groundJump;
			/** @brief Maximum number of double jumps */
			int doubleJumpCountMax;
			/** @brief Horizontal speed of a double jump */
			float doubleJumpXSpeed;
			/** @brief Vertical speed of a double jump */
			float doubleJumpYSpeed;
			/** @brief Maximum helicopter (copter) duration */
			int helicopterDurationMax;
			/** @brief Horizontal helicopter (copter) speed */
			float helicopterXSpeed;
			/** @brief Vertical helicopter (copter) speed */
			float helicopterYSpeed;
			/** @brief Whether the character can hurt enemies on contact */
			bool canHurt;
			/** @brief Whether the character can run */
			bool canRun;
			/** @brief Whether the character cycles through morph boxes */
			bool morphBoxCycle;
		};

		/** @brief Controls which parts of a player are drawn and how */
		struct jjPLAYERDRAW
		{
			/** @brief Whether the player's name is drawn */
			bool name;
			/** @brief Whether the player's sprite is drawn */
			bool sprite;
			/** @brief Whether the sugar rush effect is drawn */
			bool sugarRush;
			/** @brief Whether the gun muzzle flash is drawn */
			bool gunFlash;
			/** @brief Whether the invincibility effect is drawn */
			bool invincibility;
			/** @brief Whether the motion trail is drawn */
			bool trail;
			/** @brief Whether morphing explosions are drawn */
			bool morphingExplosions;
			/** @brief Whether the airboard bouncing motion is drawn */
			bool airboardBouncingMotion;
			/** @brief Whether the airboard puff effect is drawn */
			bool airboardPuff;
			/** @brief Sprite blend mode used to draw the player */
			std::int32_t spriteMode;
			/** @brief Parameter for the sprite blend mode */
			std::uint8_t spriteParam;
			/** @brief Light type emitted by the player */
			std::int32_t lightType;
			/** @brief Brightness of the light around the player */
			std::int8_t light;
			/** @brief Layer the player is drawn on */
			std::int32_t layer;
			/** @brief Current sprite frame index */
			std::uint32_t curFrame;
			/** @brief Rotation angle applied when drawing */
			std::int32_t angle;
			/** @brief Horizontal draw offset */
			float xOffset;
			/** @brief Vertical draw offset */
			float yOffset;
			/** @brief Horizontal draw scale */
			float xScale;
			/** @brief Vertical draw scale */
			float yScale;
			/** @brief Capture-the-flag flag state */
			std::int32_t flag;
			/** @brief The player this draw configuration belongs to */
			jjPLAYER* _player;

			/** @brief Returns whether the given shield effect is drawn */
			bool get_shield(std::int32_t shield) const;
			/** @brief Sets whether the given shield effect is drawn */
			bool set_shield(std::int32_t shield, bool enable);
			/** @brief Returns the player this draw configuration belongs to */
			jjPLAYER* get_player() const;
		};
	}
}

namespace Jazz2::Scripting
{
	using namespace Legacy;

	/** @brief Returns the total number of players */
	std::int32_t get_jjPlayerCount();
	/** @brief Returns the number of local players */
	std::int32_t get_jjLocalPlayerCount();

	/** @brief Returns the primary local player */
	jjPLAYER* get_jjP();
	/** @brief Returns the player at the given index */
	jjPLAYER* get_jjPlayers(std::uint8_t index);
	/** @brief Returns the local player at the given index */
	jjPLAYER* get_jjLocalPlayers(std::uint8_t index);

	/** @brief Returns the weapon settings at the given index */
	jjWEAPON* get_jjWEAPON(int index);

	/** @brief Returns the character settings at the given index */
	jjCHARACTER* get_jjCHARACTER(int index);
}

#endif
