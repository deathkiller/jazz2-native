#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpPlayerState.h"
#include "../MpGameMode.h"
#include "../ServerMessage.h"
#include "../../EventType.h"
#include "../../PlayerType.h"
#include "../../UI/Alignment.h"
#include "../../../nCine/Primitives/Vector2.h"
#include "../../../nCine/Primitives/Rect.h"
#include "../../../nCine/Primitives/Colorf.h"

#include <Common.h>
#include <Containers/ArrayView.h>
#include <Containers/Pair.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Actors
{
	class ActorBase;
	class Player;
}

namespace Jazz2::Multiplayer
{
	struct ServerConfiguration;
}

namespace Jazz2::Multiplayer::GameModes
{
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
		/** @brief Whether the round goes into overtime instead of ending (only from @ref IGameMode::OnTimeOut) */
		bool StartOvertime;

		/** @brief Returns a result meaning "the round does not end" */
		static GameEndResult None() {
			return { false, false, nullptr, 0, false };
		}
		/** @brief Returns a result meaning "the round ends in a draw" */
		static GameEndResult Draw() {
			return { true, false, nullptr, 0, false };
		}
		/** @brief Returns a result meaning "the round ends with the specified player as the winner" */
		static GameEndResult PlayerWins(Actors::Player* winner) {
			return { true, false, winner, 0, false };
		}
		/** @brief Returns a result meaning "the round ends with the specified team as the winner" */
		static GameEndResult TeamWins(std::uint8_t team) {
			return { true, true, nullptr, team, false };
		}
		/** @brief Returns a result meaning "the round goes into overtime" */
		static GameEndResult Overtime() {
			return { false, false, nullptr, 0, true };
		}
	};

	/** @brief Role of a player in a game mode, a bitwise combination is stored in @ref MpPlayerState::ModeFlags */
	enum class PlayerModeFlags : std::uint8_t
	{
		None = 0,				/**< None */
		It = 0x01,				/**< The player who is "it" in Roast Tag */
		Zombie = 0x02,			/**< Infected player in Pestilence */
		Jailed = 0x04,			/**< Player locked in the jail in Jailbreak */
		Eliminated = 0x08,		/**< Player out of the round for good */
		CarriesFlag = 0x10,		/**< Player carrying the flag in Flag Run */
		BottomFeeder = 0x20		/**< Player with the fewest points in Roast Tag, anyone can roast them */
	};

	DEATH_ENUM_FLAGS(PlayerModeFlags);

	/** @brief Type of @ref GameModeObject */
	enum class GameModeObjectType : std::uint8_t
	{
		ControlPoint,	/**< Point to hold in Domination */
		Jail,			/**< Jail of a team in Jailbreak */
		Flag,			/**< Neutral flag in Flag Run */
		Base			/**< Base of a team where something is delivered (Flag Run, Head Hunters) */
	};

	/**
		@brief Object in the level owned by a game mode

		The server keeps the list, the game mode changes it and asks the host to synchronize it, see
		@ref IGameModeContext::SyncGameModeObjects(). Both sides show each object with a local actor driven by this
		state, it's never remoted as an actor.
	*/
	struct GameModeObject
	{
		/** @brief Identifier unique within the round, so an object keeps its actor while the list changes */
		std::uint32_t Id;
		/** @brief Type */
		GameModeObjectType Type;
		/** @brief Owning team, or @ref NeutralTeam */
		std::uint8_t Team;
		/** @brief Team currently capturing the object, or @ref NeutralTeam */
		std::uint8_t CapturingTeam;
		/** @brief Capture progress from 0 to 255 */
		std::uint8_t Progress;
		/** @brief Facing direction, 1 for right, -1 for left */
		std::int8_t Direction;
		/** @brief Position */
		Vector2f Pos;
		/** @brief Player index of the carrier (@ref GameModeObjectType::Flag), or `UINT32_MAX` */
		std::uint32_t CarrierPlayerIndex;
		/** @brief Name, e.g. of a control point */
		String Name;

		/** @brief Value of @ref Team that means no team */
		static constexpr std::uint8_t NeutralTeam = 0xFF;
	};

	/** @brief Control point or capture point of the level, see @ref IGameModeContext::FindCapturePoints() */
	struct CapturePointInfo
	{
		/** @brief Position, the middle of the tile */
		Vector2f Pos;
		/** @brief Facing direction, 1 for right, -1 for left */
		std::int32_t Direction;
		/** @brief Name from the help string of a Text event, empty otherwise */
		String Name;
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
		/** @brief Returns the synchronized second number of the team (see @ref IGameMode::TryGetTeamExtra), also on clients */
		virtual bool GetSyncedTeamExtra(std::uint8_t team, std::uint32_t& value) const = 0;
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

		/** @brief Returns the display name of the specified player */
		virtual StringView GetPlayerName(Actors::Player* player) = 0;
		/** @brief Returns the player with the specified player index, or `nullptr` if it's not in the round */
		virtual Actors::Player* FindPlayerByIndex(std::uint32_t playerIndex) = 0;
		/** @brief Returns the home base of the specified team (CTF base event, or a spawn point of the team) */
		virtual Vector2f GetTeamBasePosition(std::uint8_t team) = 0;
		/** @brief Shows a message to all players, each in their own language */
		virtual void ShowAlertToAllPlayers(const ServerMessage& message) = 0;
		/** @brief Writes a line to the console of all players, each in their own language */
		virtual void SendMessageToAllPlayers(const ServerMessage& message) = 0;
		/** @brief Shows a step of the big countdown in the middle of the screen of all players, `0` shows "Go!" */
		virtual void ShowCountdownToAllPlayers(std::int32_t secsLeft) = 0;
		/** @brief Synchronizes the mode fields of @ref MpPlayerState of the specified player to all peers and re-applies its color */
		virtual void SyncPlayerModeState(Actors::Player* player) = 0;
		/** @brief Synchronizes the team scores to all peers */
		virtual void SyncTeamScores() = 0;
		/** @brief Updates the standings and ends the round if the win condition is met, call it after a score changes */
		virtual void CheckGameEnds() = 0;
		/** @brief Returns objects in the level owned by the game mode, see @ref SyncGameModeObjects() */
		virtual SmallVectorImpl<GameModeObject>& GetGameModeObjects() = 0;
		/** @brief Returns a new identifier for @ref GameModeObject::Id */
		virtual std::uint32_t CreateGameModeObjectId() = 0;
		/** @brief Synchronizes @ref GetGameModeObjects() to all peers, call it after any change */
		virtual void SyncGameModeObjects() = 0;
		/** @brief Collects the positions of all event tiles of the specified type in the level, with their first two parameters (the second one in the high byte) */
		virtual void FindEventPositions(EventType eventType, SmallVectorImpl<Pair<Vector2f, std::uint16_t>>& positions) = 0;
		/**
			@brief Collects the events marked as easy-only or hard-only, which are control points and capture points

			Ordered from top to bottom and then from left to right. An easy one faces right (direction 1), a hard one left
			(direction -1). A Text event carries the help string it points to as the name.
		*/
		virtual void FindCapturePoints(SmallVectorImpl<CapturePointInfo>& points) = 0;
		/** @brief Collects the positions of all multiplayer spawn points */
		virtual void FindSpawnPoints(SmallVectorImpl<Vector2f>& positions) = 0;
		/** @brief Collects the tiles and the 20-bit parameters of all original JJ2 events of the specified ID the game keeps unconverted */
		virtual void FindOriginalEvents(std::uint8_t jj2EventId, SmallVectorImpl<Pair<Vector2i, std::uint32_t>>& events) = 0;
		/** @brief Warps the player to the specified position */
		virtual void WarpPlayer(Actors::Player* player, Vector2f pos) = 0;
		/** @brief Kills the player, it counts as a death by the environment */
		virtual void KillPlayer(Actors::Player* player) = 0;
		/** @brief Returns the number of gems the player holds, regardless of their color */
		virtual std::int32_t GetPlayerGemCount(Actors::Player* player) = 0;
		/** @brief Takes all gems from the player */
		virtual void ClearPlayerGems(Actors::Player* player) = 0;
		/** @brief Gives red gems to the player */
		virtual void AddPlayerGems(Actors::Player* player, std::int32_t count) = 0;
		/** @brief Scatters the specified number of red gems around the position, @p owner (if any) can't pick them up */
		virtual void SpawnGems(Vector2f pos, std::int32_t count, Actors::Player* owner) = 0;
		/** @brief Counts the players of the team with the role (any role if `flag` is `None`), also on clients */
		virtual std::int32_t CountPlayersWithRole(PlayerModeFlags flag, std::uint8_t team) = 0;
		/** @brief Finds the position of a player with the role, also on clients, returns `false` if there's none */
		virtual bool FindPlayerWithRole(PlayerModeFlags flag, Vector2f& pos) = 0;
		/** @brief Sets the state of a trigger */
		virtual void SetTrigger(std::uint8_t triggerId, bool newState) = 0;
		/** @brief Returns `true` while the round is in overtime */
		virtual bool IsOvertime() const = 0;
		/** @brief Returns `true` if the round may go into overtime now (enabled and not in overtime already) */
		virtual bool CanStartOvertime() const = 0;
		/** @brief Returns the score that wins the overtime, or `0` if it isn't capped */
		virtual std::uint32_t GetOvertimeScoreCap() const = 0;
		/** @brief Sets the score that wins the overtime and announces it */
		virtual void SetOvertimeScoreCap(std::uint32_t score) = 0;
		/** @brief Moves the player to spectate mode until the next round, or brings it back */
		virtual void SetPlayerOut(Actors::Player* player, bool out) = 0;
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
		/** @brief Draws an arrow near the player pointing to a position in the level, nothing if it's on the screen */
		virtual void DrawTargetArrow(const Rectf& view, Actors::Player* player, Vector2f targetPos, const Colorf& color) = 0;
	};

	/**
		@brief Rules of a single multiplayer game mode

		Encapsulates everything that differs between game modes --- win conditions, scoring, respawn rules,
		team assignment and what happens at the level exit --- so the same rules can run on top of either the
		networked @ref MpLevelHandler or a local splitscreen handler. A mode only ever talks to its host
		through @ref IGameModeContext; it never references networking, peers or the concrete handler type. Modes derive
		from @ref GameModeBase, which implements the optional parts.
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
		virtual bool OwnsRoundLogic() const = 0;

		/** @brief Called when the round starts (after the countdown) to reset/initialize mode state */
		virtual void OnRoundStarted(IGameModeContext& ctx) = 0;
		/** @brief Called every host tick while the round is running */
		virtual void OnUpdate(IGameModeContext& ctx, float timeMult) = 0;

		/** @brief Called after a player (re)spawns */
		virtual void OnPlayerSpawned(IGameModeContext& ctx, Actors::Player* player) = 0;
		/** @brief Called when a player dies, for mode-specific reactions (e.g. dropping a carried flag); kill/death counts are maintained by the host. @p killer may be `nullptr` (environmental death) */
		virtual void OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer) = 0;
		/** @brief Called when a living player changed the team, right before the player respawns on the new team */
		virtual void OnPlayerTeamChanged(IGameModeContext& ctx, Actors::Player* player) = 0;
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

		/**
			@brief Returns whether @p attacker may hurt @p victim

			Called on top of the friendly-fire rule of the host, so a mode only restricts it further.
		*/
		virtual bool CanDamagePlayer(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker) = 0;
		/** @brief Returns `true` if players @p a and @p b push each other when they touch */
		virtual bool CanPlayersPush(IGameModeContext& ctx, Actors::Player* a, Actors::Player* b) = 0;
		/** @brief Called before a pit nobody caused roasts the player, returns `true` to keep it alive at 1 heart instead */
		virtual bool PreventsHazardDeath(IGameModeContext& ctx, Actors::Player* player) = 0;
		/** @brief Returns `true` if a shot of @p shooter (may be `nullptr`) can freeze @p actor, it's called also on clients */
		virtual bool CanFreezeActor(IGameModeContext& ctx, Actors::ActorBase* actor, Actors::Player* shooter) = 0;
		/** @brief Returns `true` if the damage amplifier turns on in the overtime of this mode */
		virtual bool HasDamageAmplifierInOvertime() const = 0;
		/** @brief Processes an admin command of the mode (without the leading slash), returns `true` if it was one */
		virtual bool ProcessCommand(IGameModeContext& ctx, StringView command) = 0;
		/** @brief Called when the round goes into overtime */
		virtual void OnOvertimeStarted(IGameModeContext& ctx) = 0;
		/** @brief Returns the damage @p attacker deals to @p victim by one hit of @p amount */
		virtual std::int32_t GetDamage(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker, std::int32_t amount) = 0;
		/** @brief Called when @p attacker hurt @p victim, also when the hit roasted it (after @ref OnPlayerKilled then) */
		virtual void OnPlayerHit(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker) = 0;
		/** @brief Returns whether a kill of @p victim by @p attacker counts as a kill of the attacker and of its team */
		virtual bool CountsAsKill(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker) = 0;
		/** @brief Returns whether the specified player is out of the round for good */
		virtual bool IsEliminated(IGameModeContext& ctx, Actors::Player* player) = 0;
		/** @brief Returns the score of the specified team if the mode keeps it on its own, otherwise `false` */
		virtual bool TryGetTeamScore(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& score) = 0;
		/** @brief Returns a second number of the specified team the HUD shows (synchronized), otherwise `false` */
		virtual bool TryGetTeamExtra(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& value) = 0;
		/** @brief Decides the round when its time runs out, if @ref GameEndResult::ShouldEnd is `false`, the host decides by the score */
		virtual GameEndResult OnTimeOut(IGameModeContext& ctx) = 0;
		/** @brief Returns the ranking metric stored in the specified state, a player with more ranks higher */
		virtual std::uint32_t GetStateScore(const MpPlayerState& state) const = 0;
		/** @brief Returns label of the mode-specific column of the scoreboard, which shows @ref GetStateScore(), or empty */
		virtual StringView GetScoreboardLabel() const = 0;
		/** @brief Returns fur color forced on the player by its role in the mode, or `0` to keep it */
		virtual std::uint32_t GetForcedFurColor(std::uint8_t modeFlags) const = 0;
		/** @brief Returns a short label shown next to the name of a player with the specified role, or empty */
		virtual StringView GetRoleLabel(std::uint8_t modeFlags) const = 0;
	};
}

#endif
