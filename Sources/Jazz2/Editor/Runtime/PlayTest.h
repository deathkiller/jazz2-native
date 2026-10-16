#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../LevelHandler.h"
#include "../Document/LevelDocument.h"

#include <Containers/Function.h>

namespace Jazz2::Tiles
{
	class TileMap;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
#endif

	/**
		@brief Path the player took in a play-test, for the editor to show
	*/
	struct PlayPath
	{
		/** @brief What ended a segment of the path */
		enum class Break : std::uint8_t
		{
			None,			/**< The segment goes on */
			Died,			/**< The player died */
			Warped,			/**< The player was moved far away at once */
			Exited			/**< The level was left */
		};

		/** @brief Part of the path without a break */
		struct Segment
		{
			/** @brief Positions in the level */
			SmallVector<Vector2f, 0> Points;
			/** @brief Speed of the player at each position, in pixels a frame */
			SmallVector<float, 0> Speeds;
			/** @brief What ended the segment */
			Break End = Break::None;
		};

		/** @brief Most points kept, the oldest are dropped */
		static constexpr std::int32_t MaxPoints = 65536;

		/** @brief Segments of the path, the last one is being recorded */
		SmallVector<Segment, 0> Segments;

		/** @brief Clears the path */
		void Clear();
		/** @brief Returns the number of points of all segments */
		std::int32_t GetPointCount() const;
		/** @brief Adds where a player is now, called every frame */
		void Record(Vector2f pos, float speed, float timeMult);
		/** @brief Ends the segment being recorded */
		void BreakSegment(Break reason);

	private:
		float _time = 0.0f;
	};

	/** @brief How a play-test starts */
	struct PlayTestOptions
	{
		/** @brief Character of the first player */
		PlayerType Player = PlayerType::Jazz;
		/** @brief Difficulty */
		GameDifficulty Difficulty = GameDifficulty::Normal;
		/** @brief Whether the player starts at @ref SpawnPosition instead of the start of the level */
		bool HasSpawnPosition = false;
		/** @brief Where the player starts */
		Vector2f SpawnPosition;
		/** @brief Game mode of multiplayer in local splitscreen (value of @ref Multiplayer::MpGameMode), 0 to play alone */
		std::uint8_t GameMode = 0;
		/** @brief Players in local splitscreen, from 1 to 4 */
		std::int32_t PlayerCount = 1;
	};

	/**
		@brief A level being played from the editor, see @ref PlayTestLevelHandler
	*/
	class IPlayTest
	{
	public:
		virtual ~IPlayTest() = default;

		/** @brief Returns the level handler that plays it */
		virtual LevelHandler& GetLevelHandler() = 0;
		/** @brief Returns the viewport of the first player, or `nullptr` */
		virtual Rendering::PlayerViewport* GetViewport() const = 0;
		/** @brief Returns the first player, or `nullptr` */
		virtual Actors::Player* GetPlayer() const = 0;
		/** @brief Returns the texture the last frame was drawn into before it was scaled to the screen */
		virtual Texture* GetFrameTexture() const = 0;
		/** @brief Stops the level for a while, it's kept as it is */
		virtual void Suspend() = 0;
		/** @brief Continues the level after @ref Suspend() */
		virtual void Resume() = 0;

		/** @brief Returns the recorded path of the first player */
		PlayPath& GetPlayPath() {
			return _playPath;
		}
		/** @brief Returns `true` if the player didn't start where it was asked to, but at the start of the level */
		bool HasSpawnFallenBack() const {
			return _spawnFellBack;
		}

		/** @brief Called when the player chooses to leave the editor from the pause menu */
		Function<void()> OnExitEditor;

		/** @brief Returns a position near a given one where the player fits, `false` if there is none */
		static bool FindSpawnPositionNear(Tiles::TileMap* tileMap, Vector2f pos, float radius, Vector2f& result);
		/** @brief Writes the level to play into bytes as the game reads it */
		static bool SaveLevel(const LevelDocument& document, StringView levelName, SmallVectorImpl<std::uint8_t>& data);

	protected:
		PlayPath _playPath;
		bool _spawnFellBack = false;
	};
}

#endif
