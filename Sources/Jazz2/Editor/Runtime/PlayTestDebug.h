#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../LevelHandler.h"
#include "../../UI/Canvas.h"

namespace Jazz2::Rendering
{
	class PlayerViewport;
}

namespace Jazz2::Editor
{
	class PlayTestDebugOverlay;

	/**
		@brief Debug toggles of a level played from the level editor

		The defaults change nothing. Everything is undone once it's turned off again, see
		@ref PlayTestDebug::SetOptions().
	*/
	struct PlayTestDebugOptions
	{
		/** @brief The time of the level stands still, it goes on by single updates, see @ref PlayTestDebug::Step() */
		bool Paused = false;
		/** @brief How fast the time of the level runs, 1 for normal, such as 0.25 or 0.5 to slow it down */
		float TimeScale = 1.0f;
		/** @brief Hitboxes of the objects are drawn over the level - players, enemies, collectibles, solid objects and the rest each in a colour of its own */
		bool ShowHitboxes = false;
		/** @brief Tile events that make an area do something (modifiers, areas, triggers, warps) are drawn over the tiles in view */
		bool ShowEventZones = false;
	};

	/**
		@brief Debug tools of a level played from the level editor

		The level handlers of the play-tests have one (see @ref LevelHandler::GetPlayTestDebug()), a game doesn't, so
		nothing of it costs anything there. The time of the level runs as @ref PlayTestDebugOptions ask --- slower, or
		not at all between single steps --- and the hitboxes and the event zones are drawn over the level. It also
		moves a player by hand, and spawns the events changed in the editor again.

		The level handler calls into it at the beginning and at the end of a frame and when its viewports change, it
		reads and changes the level handler and its objects as a part of it.
	*/
	class PlayTestDebug
	{
		friend class PlayTestRootNode;
		friend class PlayTestDebugOverlay;

	public:
		/** @brief Creates a new instance for a level handler */
		explicit PlayTestDebug(LevelHandler& levelHandler);
		~PlayTestDebug();

		PlayTestDebug(const PlayTestDebug&) = delete;
		PlayTestDebug& operator=(const PlayTestDebug&) = delete;

		/**
		 * @brief Sets the debug toggles
		 *
		 * The time scale and the pause change only the time of the level, not that of the HUD and the menus.
		 */
		void SetOptions(const PlayTestDebugOptions& options);
		/** @brief Returns the debug toggles */
		const PlayTestDebugOptions& GetOptions() const {
			return _options;
		}
		/**
		 * @brief Advances the time of the level by exactly one update at the normal rate, and pauses it again
		 *
		 * Pauses the time of the level first if it isn't paused, see @ref PlayTestDebugOptions::Paused. The update
		 * happens in the next frame, while the pause menu isn't open.
		 */
		void Step();
		/**
		 * @brief Moves a local player to a position in the level, or near it
		 *
		 * The player is placed where it fits nearest to @p pos, within 10 tiles, and stands still there. Returns
		 * `false` if @p playerIndex isn't one of the players or no place for the player was found.
		 */
		bool TeleportPlayer(std::int32_t playerIndex, Vector2f pos);
		/**
		 * @brief Finds where a player fits nearest to a position, within @p radius pixels
		 *
		 * Searched in growing rings, up first, because the player falls down from there. Only the tiles are taken
		 * into account, the hitbox is that of a standing player.
		 */
		bool FindClearPlayerPosition(Vector2f pos, float radius, Vector2f& result) const;
		/**
		 * @brief Destroys the objects spawned from the events of tiles, as if the tiles went out of range
		 *
		 * The tiles are given as cells of the event map, `x + y * width`, sorted. The events are spawned again as the
		 * event map has them then, as soon as their tiles are in range of a player, so events changed in the level
		 * editor while the level is played take effect at once.
		 */
		void DespawnTileEventActors(ArrayView<const std::uint32_t> sortedCells, std::int32_t width);

		/** @{ @name Called by the level handler */

		/** @brief Applies the toggles at the beginning of a frame, returns the time of the level in it */
		float BeginFrame(float timeMult);
		/** @brief Returns the time of the level in this frame, the same as @ref BeginFrame() returned, for the normal @p timeMult */
		float GetFrameTimeMult(float timeMult) const {
			return (_timeControlled ? _frameTimeMult : timeMult);
		}
		/** @brief Returns `true` if the time of the level stands still in this frame, nothing of the level goes on then */
		bool IsFrameFrozen() const {
			return _frameFrozen;
		}
		/** @brief Creates the node every object of the level is under, it updates them with the time of the level */
		std::unique_ptr<SceneNode> CreateRootNode();
		/** @brief Puts the overlay under the node of the HUD again while something of it is shown, after the viewports changed */
		void UpdateOverlay();
		/** @brief Takes the overlay off the scene, before the level handler takes its nodes apart */
		void DetachOverlay();

		/** @} */

	private:
		LevelHandler& _levelHandler;
		PlayTestDebugOptions _options;
		// Draws the hitboxes and the event zones, created once they're shown first
		std::unique_ptr<PlayTestDebugOverlay> _overlay;
		// The time of the level doesn't run normally (it's paused or scaled), see BeginFrame()
		bool _timeControlled;
		// The time of the level in this frame, and whether it stands still in it
		float _frameTimeMult;
		bool _frameFrozen;
		// A single update was asked for while the time stands still, see Step()
		bool _stepPending;

		// The views of the players, for the overlay, the level handler has no accessor of them and only this class is a
		// friend of it
		ArrayView<const std::unique_ptr<Rendering::PlayerViewport>> GetViewports() const;
	};

	/**
		@brief Node every object of a level is under, it updates them with the time of the play-test

		The time of the level runs as by @ref PlayTestDebugOptions --- slower, or not at all between single steps. With
		the default options, it's an ordinary scene node.
	*/
	class PlayTestRootNode : public SceneNode
	{
	public:
		/** @brief Creates a new instance */
		explicit PlayTestRootNode(PlayTestDebug& debug);

		void OnUpdate(float timeMult) override;

	private:
		PlayTestDebug& _debug;
	};

	/**
		@brief Draws debug information of a play-test over the level

		The hitboxes of the objects and the tile events that make an area do something, as @ref PlayTestDebugOptions
		ask for, in every view of a player. It's attached to the overlay of the level only while something of it is
		shown, so it costs nothing otherwise.
	*/
	class PlayTestDebugOverlay : public UI::Canvas
	{
	public:
		/** @brief Creates a new instance */
		explicit PlayTestDebugOverlay(PlayTestDebug& debug);

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		// Below the HUD, the event zones below the hitboxes
		static constexpr std::uint16_t EventZoneLayer = 20;
		static constexpr std::uint16_t HitboxLayer = 30;

		PlayTestDebug& _debug;

		void DrawEventZones(const Rendering::PlayerViewport& viewport, const Rectf& bounds);
		void DrawHitboxes(const Rendering::PlayerViewport& viewport, const Rectf& bounds);
		void DrawOutline(const Rectf& rect, std::uint16_t z, const Colorf& color);
	};
}

#endif
