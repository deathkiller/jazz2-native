#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "JJ2PlusDefinitions.h"
#include "../Actors/ActorBase.h"
#include "../UI/Canvas.h"

namespace Jazz2::Scripting::Legacy
{
	/**
		@brief Sprites and rectangles a level script draws in the level (`jjDrawSprite()` and the like)

		Attached to the scene of the level, so what's drawn is in the level, at the depth of a layer. The script draws in
		its updates, everything is drawn until the next update of the script clears it, as in JJ2+ where a draw lasts
		one frame.
	*/
	class ScriptWorldCanvas : public UI::Canvas
	{
	public:
		/** @brief Frame of a sprite drawn in the level */
		struct SpriteDraw {
			/** @brief Graphics with the frame */
			Resources::GenericGraphicResource* Graphics;
			/** @brief Frame of the graphics */
			std::int32_t Cell;
			/** @brief Position of the hotspot */
			float X, Y;
			/** @brief Direction, `SPRITE::Direction` */
			std::int8_t Direction;
			/** @brief Scale, negative flips */
			float ScaleX, ScaleY;
			/** @brief Angle, 1024 is a full turn */
			std::int32_t Angle;
			/** @brief Sprite mode (`SPRITE::Mode`) and its parameter */
			std::uint32_t Mode;
			std::uint8_t Param;
			/** @brief Depth */
			std::uint16_t Z;
		};

		/** @brief Rectangle of a color of the palette drawn in the level */
		struct SolidDraw {
			/** @brief Position and size */
			float X, Y, Width, Height;
			/** @brief Index of the color in the palette */
			std::uint8_t Color;
			/** @brief Sprite mode (`SPRITE::Mode`) and its parameter */
			std::uint32_t Mode;
			std::uint8_t Param;
			/** @brief Depth */
			std::uint16_t Z;
		};

		/** @brief Draws a sprite in the level until the next @ref Clear() */
		void AddSprite(const SpriteDraw& draw) {
			_sprites.push_back(draw);
		}
		/** @brief Draws a rectangle in the level until the next @ref Clear() */
		void AddSolid(const SolidDraw& draw) {
			_solids.push_back(draw);
		}
		/** @brief Removes everything drawn */
		void Clear() {
			_sprites.clear();
			_solids.clear();
		}

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		SmallVector<SpriteDraw, 0> _sprites;
		SmallVector<SolidDraw, 0> _solids;
	};

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
		 * `playerHandling` of `PARTICLE` makes the object untouchable by players, a `bulletHandling` of `IGNOREBULLET`
		 * lets bullets fly through it and `DESTROYBULLET` stops them without harm.
		 */
		bool OnCollision(Actors::ActorBase* actor, Actors::ActorBase* other) override;

		/** @brief Runs the object's own update, as `jjOBJ::behave()` with a behavior of the game */
		void RunOwnBehavior(bool draw);
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
		/** @brief Gives the object of the game the properties a script changed since @ref PullFromActor() */
		static void PushToActor(jjOBJ& obj, Actors::ActorBase& actor);
		/** @brief Takes the animation a script gave the object since @ref PullFromActor() as the one it shows */
		static void UpdateSkin(jjOBJ& obj);
		/** @brief Returns `true` if a script gave an object (or its template) what only a host can do, a behavior, an animation or collisions of its own */
		static bool NeedsHost(const jjOBJ& obj);
		/** @brief Returns where the object of the game was spawned */
		static Vector2f GetSpawnPos(const Actors::ActorBase& actor);
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
		/** @brief Draws the current frame of a `jjOBJ` where it is in the level, as `jjOBJ::draw()` */
		static void DrawFrame(LevelScriptLoader* levelScripts, const jjOBJ& obj);

	private:
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

		// Runs the behavior for one tick of the scripts, returns `false` if the object is gone
		bool UpdateTick(Actors::ActorBase* actor);
		// Draws the object as the last tick left it, by itself or its frame
		void FinishTicks();
	};
}

#endif
