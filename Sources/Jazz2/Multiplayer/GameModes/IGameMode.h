#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpPlayerState.h"
#include "../MpGameMode.h"
#include "../../PlayerType.h"
#include "../../UI/Alignment.h"
#include "../../../nCine/Primitives/Vector2.h"
#include "../../../nCine/Primitives/Rect.h"
#include "../../../nCine/Primitives/Colorf.h"

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Multiplayer
{
	struct ServerConfiguration;

	/** @brief What should happen when a player reaches the level exit, as decided by the game mode */
	enum class LevelExitAction
	{
		Ignore,						/**< Ignore the exit (competitive modes without a level-exit win condition) */
		ChangeToNextLevel,			/**< Proceed to the next level (Cooperation) */
		EndGameForInitiator,		/**< End the round with the initiator as the winner (Treasure Hunt with enough treasure) */
		WarpToStartIncrementLap		/**< Warp the initiator back to the start and count a lap (Race) */
	};

	/** @brief Result of @ref IGameMode::DecideRespawn */
	struct RespawnDecision
	{
		/** @brief Whether the player may respawn now */
		bool CanRespawn;
		/** @brief Respawn at the player's own checkpoint (Cooperation) instead of @ref SpawnPos */
		bool UseCheckpoint;
		/** @brief Spawn position used when @ref UseCheckpoint is `false` */
		Vector2f SpawnPos;
		/** @brief Spawn invulnerability in seconds (`0` = none) */
		float InvulnerableSecs;
	};

	/** @brief Result of @ref IGameMode::CheckGameEnds */
	struct GameEndResult
	{
		/** @brief Whether the round should end now */
		bool ShouldEnd;
		/** @brief Whether @ref WinnerTeam decides the winner instead of @ref Winner */
		bool WithTeam;
		/** @brief Winning player (may be `nullptr`) when @ref WithTeam is `false` */
		Actors::Player* Winner;
		/** @brief Winning team when @ref WithTeam is `true` */
		std::uint8_t WinnerTeam;

		/** @brief Returns a result meaning "the round does not end" */
		static GameEndResult None() {
			return { false, false, nullptr, 0 };
		}
	};

	/**
		@brief Services a game mode needs from its host level handler

		Implemented by whichever level handler hosts an @ref IGameMode (the online @ref MpLevelHandler or a
		local splitscreen handler). It abstracts away whether the session is networked: the mode reads and
		writes per-player state through @ref GetPlayerState and never touches peers, packets or sockets. The
		online host observes the state the mode produces and synchronizes it to clients; the local host just
		stores it.
	*/
	class IGameModeContext
	{
	public:
		virtual ~IGameModeContext() { }

		/** @brief Returns the active server configuration (win targets, elimination, invulnerability, ...) */
		virtual const ServerConfiguration& GetServerConfiguration() const = 0;
		/** @brief Returns all currently active players (matches @ref ILevelHandler::GetPlayers) */
		virtual ArrayView<Actors::Player* const> GetPlayers() const = 0;
		/** @brief Returns the mutable mode state of the specified player */
		virtual MpPlayerState& GetPlayerState(Actors::Player* player) = 0;
		/** @brief Returns whether the specified player is currently spectating */
		virtual bool IsSpectating(Actors::Player* player) const = 0;
		/** @brief Returns the number of teams (team-based modes only) */
		virtual std::uint8_t GetTeamCount() const = 0;
		/** @brief Returns the aggregate round score of the specified team (team-based modes only) */
		virtual std::uint32_t GetTeamScore(std::uint8_t team) = 0;
		/** @brief Returns the number of Capture The Flag flag states (one per team), or `0` when not in CTF */
		virtual std::uint8_t GetCtfFlagStateCount() const = 0;
		/** @brief Returns the flag state of the specified team in Capture The Flag (`0` = home, `1` = taken, `2` = dropped) */
		virtual std::uint8_t GetCtfFlagState(std::uint8_t team) const = 0;
		/** @brief Returns a spawn position appropriate for the specified player (its type and assigned team) */
		virtual Vector2f GetSpawnPoint(Actors::Player* player) = 0;
		/** @brief Returns the elapsed frames since the level started */
		virtual float GetElapsedFrames() const = 0;
		/** @brief Ends the round with the specified winning player (may be `nullptr` for a draw) */
		virtual void EndGame(Actors::Player* winner) = 0;
		/** @brief Ends the round with the specified winning team */
		virtual void EndGameWithTeam(std::uint8_t team) = 0;
	};

	/** @brief Selects which HUD font a game mode draws text with */
	enum class GameModeFontType
	{
		Small,		/**< Small font */
		Medium		/**< Medium font */
	};

	/** @brief A semantic HUD icon a game mode can draw; the HUD maps it to a concrete sprite */
	enum class GameModeHudIcon
	{
		Food,		/**< Food / score pickup icon */
		GemRed,		/**< Red gem (treasure not yet sufficient) */
		GemGreen	/**< Green gem (enough treasure collected) */
	};

	/**
		@brief Drawing surface a game mode uses to render its own part of the HUD

		Implemented by the HUD (the online @ref UI::Multiplayer::MpHUD or a local splitscreen HUD). It exposes only
		the primitives a mode needs for its score area, so a mode owns its HUD without depending on the concrete HUD
		type or its sprite constants. The text and icon primitives draw a drop shadow plus the main content in one
		call (the shadow is offset down by @p shadowOffsetY).
	*/
	class IGameModeHUD
	{
	public:
		virtual ~IGameModeHUD() { }

		/**
			@brief Draws a text string (shadow + main) with the small or medium HUD font

			@p angleOffset, @p variance and @p speed drive the optional wavy-text animation (all `0` = static text,
			which is what most HUD text uses).
		*/
		virtual void DrawHudText(GameModeFontType font, StringView text, float x, float y, float shadowOffsetY,
			UI::Alignment alignment, const Colorf& color, float scale, float charSpacing,
			float angleOffset = 0.0f, float variance = 0.0f, float speed = 0.0f) = 0;
		/** @brief Draws a semantic HUD icon (shadow + main) */
		virtual void DrawHudIcon(GameModeHudIcon icon, float x, float y, float shadowOffsetY,
			UI::Alignment alignment, const Colorf& color, float scaleX, float scaleY) = 0;
		/** @brief Draws the shared leaderboard ("position in round") for the player's split-screen view */
		virtual void DrawPositionInRound(const Rectf& view, Actors::Player* player) = 0;
		/** @brief Draws the race-track minimap for the player's split-screen view */
		virtual void DrawMinimap(const Rectf& view, Actors::Player* player) = 0;
	};

	/**
		@brief Rules of a single multiplayer game mode

		Encapsulates everything that differs between game modes --- win conditions, scoring, respawn rules,
		team assignment and what happens at the level exit --- so the same rules can run on top of either the
		networked @ref MpLevelHandler or a local splitscreen handler. A mode only ever talks to its host
		through @ref IGameModeContext; it never references networking, peers or the concrete handler type.
	*/
	class IGameMode
	{
	public:
		virtual ~IGameMode() { }

		/** @brief Returns the game mode identifier */
		virtual MpGameMode GetMode() const = 0;
		/** @brief Returns whether players are split into teams */
		virtual bool IsTeamBased() const = 0;
		/** @brief Returns whether the pre-game wait and countdown are skipped (Cooperation starts immediately) */
		virtual bool SkipsPreGameCountdown() const = 0;
		/** @brief Returns whether players have unlimited health in this mode */
		virtual bool HasUnlimitedHealth() const = 0;
		/** @brief Returns whether a lower round score ranks higher (Race finishes first) */
		virtual bool LowerScoreWins() const = 0;

		/**
			@brief Returns whether this mode computes its own win condition (@ref CheckGameEnds)

			Returns `true` once the mode owns its win-condition check. While `false` --- used during the staged
			migration --- the host runs its legacy win-condition logic for this mode instead. Level-exit
			(@ref OnLevelExitReached), respawn (@ref DecideRespawn) and the HUD (@ref OnDrawHUD) are always driven by the
			mode regardless of this flag.
		*/
		virtual bool OwnsRoundLogic() const {
			return true;
		}

		/** @brief Called when the round starts (after the countdown) to reset/initialize mode state */
		virtual void OnRoundStarted(IGameModeContext& ctx) = 0;
		/** @brief Called every host tick while the round is running */
		virtual void OnUpdate(IGameModeContext& ctx, float timeMult) = 0;

		/** @brief Called after a player (re)spawns */
		virtual void OnPlayerSpawned(IGameModeContext& ctx, Actors::Player* player) = 0;
		/** @brief Called when a player dies, for mode-specific reactions (e.g. dropping a carried flag); kill/death counts are maintained by the host. @p killer may be `nullptr` (environmental death) */
		virtual void OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer) = 0;
		/** @brief Decides whether and where the specified player should respawn */
		virtual RespawnDecision DecideRespawn(IGameModeContext& ctx, Actors::Player* player) = 0;

		/** @brief Called when a player's coin count changes */
		virtual void OnCoinsCollected(IGameModeContext& ctx, Actors::Player* player, std::int32_t prevCount, std::int32_t newCount) = 0;
		/** @brief Called when a player's gem count changes */
		virtual void OnGemsCollected(IGameModeContext& ctx, Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount) = 0;

		/** @brief Decides what happens when the specified player reaches the level exit */
		virtual LevelExitAction OnLevelExitReached(IGameModeContext& ctx, Actors::Player* player) = 0;
		/** @brief Checks whether the round should end now, and who won */
		virtual GameEndResult CheckGameEnds(IGameModeContext& ctx) = 0;

		/** @brief (Re)assigns players to teams (free-for-all modes assign one team per player) */
		virtual void AssignTeams(IGameModeContext& ctx) = 0;
		/** @brief Returns the ranking metric of the specified player (kills / laps / treasure / ...) */
		virtual std::uint32_t GetRoundScore(IGameModeContext& ctx, Actors::Player* player) = 0;

		/** @brief Draws the mode-specific part of the HUD for the given player's split-screen view */
		virtual void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) = 0;
	};
}

#endif
