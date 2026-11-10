#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "GameModeBase.h"

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Common base of the game modes that score points of their own

		Competitive defaults: no level-exit rule, respawn at a fresh spawn point with spawn invulnerability, the score
		is @ref MpPlayerState::ModePoints and the configured @ref ServerConfiguration::TotalKills is the score to reach.
		When the time runs out, the best player or team wins, a tie is a draw.
	*/
	class CustomGameModeBase : public GameModeBase
	{
	public:
		bool SkipsPreGameCountdown() const override {
			return false;
		}
		bool HasUnlimitedHealth() const override {
			return false;
		}
		bool LowerScoreWins() const override {
			return false;
		}

		void OnRoundStarted(IGameModeContext& ctx) override { }
		void OnUpdate(IGameModeContext& ctx, float timeMult) override { }
		void OnPlayerSpawned(IGameModeContext& ctx, Actors::Player* player) override { }
		void OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer) override { }
		RespawnDecision DecideRespawn(IGameModeContext& ctx, Actors::Player* player) override;
		void OnCoinsCollected(IGameModeContext& ctx, Actors::Player* player, std::int32_t prevCount, std::int32_t newCount) override { }
		void OnGemsCollected(IGameModeContext& ctx, Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount) override { }
		LevelExitAction OnLevelExitReached(IGameModeContext& ctx, Actors::Player* player) override {
			return LevelExitAction::Ignore;
		}
		GameEndResult CheckGameEnds(IGameModeContext& ctx) override;
		void AssignTeams(IGameModeContext& ctx) override { }
		std::uint32_t GetRoundScore(IGameModeContext& ctx, Actors::Player* player) override {
			return GetStateScore(ctx.GetPlayerState(player));
		}
		std::uint32_t GetStateScore(const MpPlayerState& state) const override {
			return state.ModePoints;
		}
		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
		GameEndResult OnTimeOut(IGameModeContext& ctx) override;
		void OnOvertimeStarted(IGameModeContext& ctx) override;

	protected:
		/** @brief How the overtime of a mode works */
		enum class OvertimeRule {
			None,			/**< No overtime */
			ScoreCap,		/**< First to the best score + 2 (never above the max score) wins */
			NextLead,		/**< First to the best score + 1 (never above the max score) wins */
			TiedLeaders		/**< Only if several are tied for the best score, the round then ends by its own rules */
		};

		/** @brief Returns how the overtime works in the mode */
		virtual OvertimeRule GetOvertimeRule() const {
			return (IsTeamBased() ? OvertimeRule::NextLead : OvertimeRule::ScoreCap);
		}
		/** @brief Returns the score the overtime looks at, the round score by default */
		virtual std::uint32_t GetLeaderScore(IGameModeContext& ctx, Actors::Player* player) {
			return GetRoundScore(ctx, player);
		}
		/** @brief Returns `true` if the round should go into overtime when its time runs out */
		bool ShouldStartOvertime(IGameModeContext& ctx);
		/** @brief Ends the round in favor of the best player or team, a tie is a draw */
		GameEndResult DecideByScore(IGameModeContext& ctx);
		/** @brief Returns the score that wins, the max score or the cap of the overtime */
		std::uint32_t GetScoreTarget(IGameModeContext& ctx);
		/** @brief Returns the best score of a player or team, and whether more of them have it */
		std::uint32_t GetBestScore(IGameModeContext& ctx, bool& tied);

		/** @brief Returns `true` if the player takes part in the round right now (not spectating, not out) */
		bool IsActive(IGameModeContext& ctx, Actors::Player* player);
		/** @brief Returns `true` if the player has the role */
		static bool HasFlag(IGameModeContext& ctx, Actors::Player* player, PlayerModeFlags flag);
		/** @brief Sets or clears the role of the player, returns `true` if it changed */
		static bool SetFlag(IGameModeContext& ctx, Actors::Player* player, PlayerModeFlags flag, bool value);
		/** @brief Clears the role of the player with the specified index (if any) and synchronizes it if it changed */
		static void ClearFlag(IGameModeContext& ctx, std::uint32_t playerIndex, PlayerModeFlags flag);
		/** @brief Adds points to the player, never below zero */
		static void AddPoints(IGameModeContext& ctx, Actors::Player* player, std::int32_t points);
		/** @brief Returns the player that reached @ref ServerConfiguration::TotalKills first, or `nullptr` */
		Actors::Player* FindPlayerAtTarget(IGameModeContext& ctx);
		/** @brief Returns the team that reached @ref ServerConfiguration::TotalKills first, or `-1` */
		std::int32_t FindTeamAtTarget(IGameModeContext& ctx);
		/** @brief Returns the player with the best score, or `nullptr` on a tie */
		Actors::Player* FindBestPlayer(IGameModeContext& ctx);
		/** @brief Returns the team with the best score, or `-1` on a tie */
		std::int32_t FindBestTeam(IGameModeContext& ctx);

		/**
			@brief Adds a game mode object of the specified type for the base of every team

			The positions are stored to @p basePos, which must have room for @ref MaxTeamCount items.
		*/
		static void AddTeamBases(IGameModeContext& ctx, GameModeObjectType type, Vector2f* basePos);
		/** @brief Adds a game mode object with a new identifier, nobody capturing or carrying it */
		static GameModeObject& AddObject(IGameModeContext& ctx, GameModeObjectType type, std::uint8_t team, Vector2f pos);

		/** @brief Draws the score line of the HUD with the label */
		void DrawScoreLine(IGameModeContext& ctx, IGameModeHUD& hud, const Rectf& view, StringView label, std::uint32_t value, bool withTarget);
		/** @brief Draws a status line below the score line */
		void DrawStatusLine(IGameModeHUD& hud, const Rectf& view, StringView text, const Colorf& color);
		/** @brief Draws a short text under the score of the team in the team score header, as the flag state in Capture The Flag */
		void DrawUnderTeamScore(IGameModeContext& ctx, IGameModeHUD& hud, const Rectf& view, std::uint8_t team, StringView text);
	};
}

#endif
