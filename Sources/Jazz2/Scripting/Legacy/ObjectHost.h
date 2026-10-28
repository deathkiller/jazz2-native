#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Definitions.h"
#include "../../Actors/ActorBase.h"

namespace Jazz2::Scripting::Legacy
{
	/** @brief Animations the template of the objects of an event has in JJ2+ before a script changes them */
	struct PresetAnimation
	{
		/** @brief Event */
		std::uint8_t EventID;
		/** @brief Set of `curAnim`, `mCOUNT` if there's none */
		anim Set;
		/** @brief Animation of `curAnim` in its set */
		std::uint8_t Animation;
		/** @brief Set of `killAnim`, `mCOUNT` if there's none */
		anim KillSet;
		/** @brief Animation of `killAnim` in its set */
		std::uint8_t KillAnimation;
	};

	/** @brief Returns the animations of the template of the objects of an event in JJ2+, `nullptr` if it has none */
	const PresetAnimation* FindPresetAnimation(std::uint8_t eventId);

	/**
		@brief Destructible scenery as an object of the game, for a level script that drives it

		Scenery is a part of its tile, which breaks by itself (see @ref Tiles::TileMap::SetTileEventFlags()), the
		scripts of JJ2+ drive it as an object of `BEHAVIOR::DESTRUCTSCENERY` though - e.g. to give it a light. The
		object is made only where a script gave the template of the event what only an object can have (see
		@ref ScriptObjectHost::NeedsHost()), so a level that doesn't pays nothing for it. It neither collides nor is
		drawn, its own behavior only tells the script what the tile does, as measured in JJ2+ - it sleeps
		(`STATE::SLEEP`) until the tile breaks (`STATE::DONE`), collapsing scenery is in `STATE::EXTRA` for the one
		update it collapses in.
	*/
	class ScriptSceneryObject : public Actors::ActorBase
	{
		DEATH_RUNTIME_OBJECT(Actors::ActorBase);

	public:
		ScriptSceneryObject();

		/** @brief Returns `true` for the update the tile collapses in, until the next one */
		bool IsCollapsing() const {
			return (_phase == Phase::Collapsing);
		}
		/** @brief Returns `true` once the tile is broken */
		bool IsBroken() const {
			return (_phase == Phase::Broken);
		}

	protected:
		Task<bool> OnActivatedAsync(const Actors::ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		enum class Phase : std::uint8_t {
			Whole,
			Collapsing,
			Broken
		};

		Phase _phase;

		void UpdatePhase(bool activating);
	};

	/**
		@brief Object of an event the game has no object of (@ref EventType::PassthroughEvent)

		JJ2+ makes an object of every event, a script drives some of them (ThreedRealms spawns its cars from one) - this
		one is made only where the template of the event has a behavior of a script, which moves and draws it.
	*/
	class ScriptEventObject : public Actors::ActorBase
	{
		DEATH_RUNTIME_OBJECT(Actors::ActorBase);

	protected:
		Task<bool> OnActivatedAsync(const Actors::ActorActivationDetails& details) override;
		bool OnDraw(RenderQueue& renderQueue) override;
	};

	/**
		@brief Adds the light a `jjOBJ` has by its `lightType` and `light` at a position of the level

		The lights are shaped after the ones JJ2+ draws (as measured), with the falloff the lights of the game have.
		@p gameTick drives the ones that change over time.
	*/
	void EmitObjectLight(const jjOBJ& obj, Vector2f pos, std::int32_t gameTick, SmallVectorImpl<LightEmitter>& lights);

	/**
		@brief Returns `true` if the pixels of the frame a `jjOBJ` shows overlap a rectangle of the level

		The frame is flipped by the direction of the object, as it's drawn.
	*/
	bool DoesObjectTouch(LevelScriptLoader* levelScripts, const jjOBJ& obj, const AABBf& box);

	/**
		@brief Drives an object of the game by a behavior of a level script, as JJ2+ does with the objects whose template
		(`jjObjectPresets`) has a behavior of the script

		The behavior replaces the per-frame update of the object. Most behaviors of scripts wrap the one the object has in
		the game (`obj.behave(BEHAVIOR::PICKUP); ...`), and a behavior of the game run that way is the object's own update.
		Before the behavior the `jjOBJ` gets the position, the speed, the direction, the energy, the state and the frame of
		the object, after it what the behavior changed goes back to the object. The object is drawn only if the behavior
		asks for it, by running a behavior of the game with drawing or by `jjOBJ::draw()`, as in JJ2+.

		The frame is that of the animation of the original the object shows (see @ref AnimationTables::FindAnimationOfGraphics()).
		An animation a script gives the object (by `curAnim` of the object or of its template) is shown in its place, its
		frame of the same position, so a behavior of the game still animates it - and a frame a script chooses is drawn
		as it is, not by the object of the game.

		Collisions with players and bullets are handled by the script as JJ2+ does with `scriptedCollisions`, and as
		the `playerHandling` and `bulletHandling` a script set say (see @ref OnCollision()).
	*/
	class ScriptObjectHost : public Actors::IActorUpdateOverride
	{
	public:
		/**
		 * @brief How much further right and down an object of the game is than the `jjOBJ` of it
		 *
		 * The objects of events are at the 15th pixel of their tile in JJ2+ (`xOrg` of the second tile is 47), the objects
		 * of the game at the 16th - scripts compare the positions with the ones of JJ2+.
		 */
		static constexpr float PositionOffset = 1.0f;

		/** @brief Creates a new instance driving an object of the game, takes a reference of the `jjOBJ` */
		ScriptObjectHost(LevelScriptLoader* levelScripts, jjOBJ* obj, Actors::ActorBase* actor);
		~ScriptObjectHost() override;

		ScriptObjectHost(const ScriptObjectHost&) = delete;
		ScriptObjectHost& operator=(const ScriptObjectHost&) = delete;

		void OnUpdate(Actors::ActorBase* actor, float timeMult) override;
		bool IsDrawn() const override;
		/**
		 * @brief Lets the script handle a collision of the object with a player or a bullet
		 *
		 * With `scriptedCollisions`, a player touching a pickup and a player attacking or a bullet hitting anything else
		 * calls `onObjectHit()` of the behavior or the global ones (a `false` of the method lets the game handle it). A
		 * `playerHandling` of `PARTICLE` makes the object untouchable by players and bullets (whatever its `bulletHandling`
		 * is), a `bulletHandling` of `IGNOREBULLET` lets bullets fly through it and `DESTROYBULLET` stops them without harm.
		 */
		bool OnCollision(Actors::ActorBase* actor, Actors::ActorBase* other) override;
		/**
		 * @brief Makes a solid object as big as the pixels of the frame of an animation a script gave it
		 *
		 * The objects of JJ2+ are solid by their frames, so a crate drawn bigger stands on the ground by its frame.
		 */
		bool OnUpdateHitbox(Actors::ActorBase* actor) override;
		/** @brief Adds the light a script gave the object, see @ref EmitObjectLight() */
		void OnEmitLights(Actors::ActorBase* actor, SmallVectorImpl<LightEmitter>& lights) override;
		/**
		 * @brief Lets a crate the behavior drives break as in JJ2+
		 *
		 * The object of the game breaks (and gives what's in it), but the object of the script stays - in `STATE::ACTION`,
		 * as JJ2+ has a crate that was hit, so `BEHAVIOR::CRATE` breaks it into pieces of `OBJECT::SHARD` and the behavior
		 * can keep what remains of it (see @ref LevelScriptLoader::BreakObjects()). Anything else perishes as in the game,
		 * unless a behavior of the script brings it back (puppet hands).
		 */
		bool OnPerish(Actors::ActorBase* actor, Actors::ActorBase* collider) override;
		/** @brief Returns `true` once a crate breaks as in JJ2+, its pieces are objects then (see @ref OnPerish()), or for an object showing an animation of a script */
		bool OwnsDebris() const override;
		/**
		 * @brief Returns `true` for an object the level illuminates, which lights as JJ2+ has it (`LIGHT::LASER`)
		 *
		 * The game's own light of an illuminated object brightens its surroundings beyond full light, JJ2+ doesn't.
		 */
		bool OwnsLights() const override;

		/**
		 * @brief Makes the object a platform for this tick, as `jjOBJ::bePlatform()`
		 *
		 * As measured in JJ2+, a player stands 24 pixels above the object over the width of its frame (or @p width) and goes
		 * along with it by how far it moved since @p xOld and @p yOld, it's one only while the behavior makes it one.
		 */
		void BePlatform(float xOld, float yOld, std::int32_t width);
		/** @brief Lets go of the players standing on the object, as `jjOBJ::clearPlatform()` */
		void ClearPlatform();
		/** @brief Runs the object's own update, as `jjOBJ::behave()` with a behavior of the game */
		void RunOwnBehavior(bool draw);
		/**
		 * @brief Runs a behavior of the game, as `jjOBJ::behave()`
		 *
		 * The behavior of the object of another event makes the object of the game that object from the next update of the
		 * scripts (see @ref LevelScriptLoader::SwapActor()), the object's own update runs until then.
		 */
		void RunGameBehavior(std::uint32_t behavior, bool draw);
		/** @brief Runs `BEHAVIOR::SWINGINGVINE` of JJ2+, the object is the end of the vine */
		void RunSwingingVine(bool draw);
		/** @brief Puts a swinging vine object at the end of the vine, as JJ2 has it, from its `var[1]` and `var[2]` */
		static void SetVineEnd(jjOBJ& obj);
		/** @brief Draws a swinging vine, by the vine of the game or by the frame after `curFrame` */
		void DrawVine(bool draw);
		/** @brief Spawns the object of an event where an object of the game is, as made of the same event, `nullptr` if it has none */
		static std::shared_ptr<Actors::ActorBase> SpawnInPlace(LevelScriptLoader* levelScripts, Actors::ActorBase& actor, std::uint8_t eventId);
		/**
		 * @brief Runs `BEHAVIOR::MONITOR` of JJ2+ on the object of the game, which is pushed as it is
		 *
		 * The object falls when there's no ground right below its frame, it's in `STATE::FALL` then and in `STATE::SLEEP`
		 * when it rests on the ground.
		 */
		void RunMonitor(bool draw);
		/**
		 * @brief Returns `true` for a bullet a player fired of a weapon whose bullets a script controls
		 *
		 * See @ref LevelScriptLoader::FirePlayerWeapon(), such a bullet flies as `BEHAVIOR::BULLET` of JJ2+ (see @ref RunBullet()).
		 */
		bool IsScriptedBullet() const;
		/**
		 * @brief Runs `BEHAVIOR::BULLET` of JJ2+ on a bullet of a weapon whose bullets a script controls
		 *
		 * It flies by its speeds and accelerations until its `counterEnd` or a wall, then plays its `killAnim` and is gone.
		 * The shot of the game neither moves nor is drawn then.
		 */
		void RunBullet(bool draw);
		/**
		 * @brief Sets up the bullet of a weapon whose bullets a script controls as JJ2+ fires it, see @ref IsScriptedBullet()
		 *
		 * @p spreadSlot is -1 for the bullet angled up, 0 for the straight one and 1 for the one angled down, @p gunspotOffsetY
		 * how far below the player the gunspot of the pose it was fired in was.
		 */
		static void InitializeScriptedBullet(jjOBJ& obj, const jjOBJ& preset, Actors::ActorBase& actor, std::int8_t spreadSlot, float gunspotOffsetY);
		/**
			@brief Makes the object of the game follow a behavior that moves the object itself (`BEHAVIOR::WALKINGENEMY`)

			It's put where the `jjOBJ` is, without gravity of its own and without the speeds, which are the behavior's.
		*/
		void FollowScriptMovement();
		/** @brief Draws the object in this frame, as `jjOBJ::draw()` */
		void Draw();
		/** @brief Removes the object, the event it comes from isn't spawned again */
		void Delete();
		/** @brief Removes the object, the event it comes from spawns it again when it's seen again */
		void Deactivate();

		/**
		 * @brief Gives the `jjOBJ` the properties of the object of the game
		 *
		 * The frame only @p withAnimation - an object a behavior of the script drives animates only when its own behavior
		 * runs, the behavior of the script counts the frames itself otherwise.
		 */
		static void PullFromActor(LevelScriptLoader* levelScripts, jjOBJ& obj, Actors::ActorBase& actor, bool withAnimation = true);
		/**
		 * @brief Gives the object of the game the properties a script changed since @ref PullFromActor()
		 *
		 * With @p deferKill, `STATE::KILL` doesn't kill it yet, its behavior is to see it first.
		 */
		static void PushToActor(jjOBJ& obj, Actors::ActorBase& actor, bool deferKill = false);
		/** @brief Takes the animation a script gave the object since @ref PullFromActor() as the one it shows */
		static void UpdateSkin(jjOBJ& obj);
		/** @brief Returns `true` if a script gave an object (or its template) what only a host can do, a behavior, an animation, collisions or a light of its own */
		static bool NeedsHost(const jjOBJ& obj);
		/** @brief Returns how much lower `yPos` of a walker with an animation a script gave it is than the object of the game, so its frame stands on the ground by its coldspot */
		static float GetSkinOffsetY(LevelScriptLoader* levelScripts, const jjOBJ& obj, const Actors::ActorBase& actor);
		/** @brief Returns the vertical position of an object of the game, of a floating collectible the one it rests at */
		static float GetRestingY(const Actors::ActorBase& actor);
		/** @brief Returns where the object of the game was spawned */
		static Vector2f GetSpawnPos(const Actors::ActorBase& actor);
		/** @brief Returns the tile of the event the object of the game comes from */
		static Vector2i GetOriginTile(const Actors::ActorBase& actor);
		/** @brief Removes an object of the game, the event it comes from isn't spawned again */
		static void DeleteActor(Actors::ActorBase& actor);
		/** @brief Removes an object of the game, the event it comes from spawns it again when it's seen again */
		static void DeactivateActor(Actors::ActorBase& actor);
		/**
		 * @brief Runs a behavior of the script on a `jjOBJ`, `false` if it isn't one
		 *
		 * A behavior of a class also draws by its `onDraw()` unless @p draw is `false`, as JJ2+ calls it.
		 */
		static bool CallScriptBehavior(LevelScriptLoader* levelScripts, jjOBJ& obj, const jjBEHAVIOR& behavior, bool draw = true);
		/**
		 * @brief Calls `onObjectHit()` of the behavior of a `jjOBJ`, and the global ones if it has none or it returns `false`
		 *
		 * Returns `true` if the method of the behavior returned `true` or a global one was called.
		 */
		static bool CallObjectHit(LevelScriptLoader* levelScripts, jjOBJ& obj, jjOBJ* bullet, jjPLAYER* player, std::int32_t force);
		/** @brief Draws the current frame of a `jjOBJ` where it is in the level (@p offsetY lower), as `jjOBJ::draw()` */
		static void DrawFrame(LevelScriptLoader* levelScripts, const jjOBJ& obj, float offsetY = 0.0f);
		/** @brief Returns `true` if an object of a `direction` (`SPRITE::Direction`) is drawn upside down */
		static bool IsUpsideDown(std::int8_t direction);
		/**
		 * @brief Turns an object of the game upside down, as JJ2+ has the objects of a template whose `direction` is `SPRITE::FLIPV`
		 *
		 * It's drawn flipped vertically, a solid object (a crate, a bird cage) hangs from the ceiling above it instead of
		 * standing on the ground, its hitbox flipped with it.
		 */
		static void TurnUpsideDown(Actors::ActorBase& actor);

	private:
		class UpsideDownOverride;

		// Hangs a solid object turned upside down from the ceiling above it by its hitbox, see TurnUpsideDown()
		static void HangFromCeiling(Actors::ActorBase& actor);
		// Runs the update of the object of the game itself
		static void RunNativeUpdate(Actors::ActorBase& actor, float timeMult);
		// Makes the hitbox of the object of the game as it does itself and flips it upside down about the position, as the
		// frame is drawn
		static void UpdateUpsideDownHitbox(Actors::ActorBase& actor);

		// Prepares the context for a function of a behavior, logs why it can't be
		static bool PrepareBehavior(asIScriptContext* ctx, asIScriptFunction* function);

		LevelScriptLoader* _levelScripts;
		jjOBJ* _obj;
		Actors::ActorBase* _actor;
		float _timeMult;
		// The next tick of the scripts the behavior runs in, -1 before the first one
		std::int32_t _nextTick;
		// The object of the game draws itself in this frame, or its frame is drawn by the script (see Draw())
		bool _drawn;
		bool _drawFrame;
		bool _ranOwnBehavior;
		bool _isRunningOwnBehavior;
		// The hitbox of the object is the one of its frame (see OnUpdateHitbox())
		bool _hasFrameHitbox;
		// The object was put on the ground by its frame and doesn't fall since (see UpdateFrameHitbox())
		bool _heldInPlace;
		// The crate broke, the object of the script goes on without the object of the game (see OnPerish())
		bool _breaking;
		// The level illuminates the object, its light is the one of the script view (see OwnsLights())
		bool _ownsLights;
		// The object of the game is to be the object of another event (see RunGameBehavior())
		bool _swapRequested = false;
		// How far the behavior moved the origin in the last tick, and the time of it the platform moved on by since (see OnUpdate())
		Vector2f _originStep;
		float _originStepTime = 0.0f;
		// The behavior hung the platform where the platform it was made by is in the last tick (see GetCreatorPlatform())
		bool _hangsFromCreator = false;
		// The tick a player touching a special object last told its behavior of it in (see OnCollision())
		std::int32_t _playerHitTick = INT32_MIN;
		// The bullet hits what its frame collides with right now, the collision isn't swallowed then (see CollideByFrame())
		bool _collidingByFrame = false;
		// The tick the behavior made the object a platform in, and how wide (0 as its frame), see BePlatform()
		static constexpr std::int32_t NoPlatform = INT32_MIN;
		std::int32_t _platformTick = NoPlatform;
		std::int16_t _platformWidth = 0;
		// The frame _frameRect and _pixelBounds are of (from its hotspot), both empty if it has no pixels
		std::uint32_t _boundsFrame;
		Recti _frameRect;
		Recti _pixelBounds;

		// Runs the behavior for one tick of the scripts, returns `false` if the object is gone
		bool UpdateTick(Actors::ActorBase* actor);
		// Makes _frameRect and _pixelBounds those of the frame the object shows, returns `false` if it has no pixels
		bool UpdateFrameBounds();
		// Returns the hitbox of the pixels of the frame the object shows, `false` if the object keeps its own (see OnUpdateHitbox())
		bool GetFrameHitbox(const Actors::ActorBase& actor, AABBf& hitbox);
		// Returns how far down the object goes onto the ground by its frame, `false` if there's none near
		bool FindFrameGround(const Actors::ActorBase& actor, float& offset);
		// Returns `true` if the pixel right below the frame of the object is in the ground, in the column of its hotspot
		bool IsFrameOnGround(const Actors::ActorBase& actor);
		// Makes the hitbox of the object the one of its frame, puts the object on the ground by its frame the first time
		void UpdateFrameHitbox(Actors::ActorBase& actor);
		// Returns where the feet of the players standing on the object are, while the behavior makes it a platform
		bool GetPlatformBox(AABBf& box);
		// Returns `true` for a bullet of a player with a behavior of the script and a frame, which collides by the pixels of it
		bool CollidesByFrame() const;
		// Hits what the frame of the bullet collides with, as JJ2 has bullets hit objects (a sword)
		void CollideByFrame();
		// Draws the object as the last tick left it, by itself or its frame
		void FinishTicks();
		// Returns the platform of the game an object made this platform, `nullptr` if this isn't one or it wasn't made so
		Actors::ActorBase* GetCreatorPlatform() const;
	};
}

#endif
