#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../GameDifficulty.h"
#include "../../PlayerType.h"
#include "../../../nCine/Primitives/Colorf.h"
#include "../../../nCine/Primitives/Vector2.h"

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

#include <memory>

using namespace nCine;

namespace Jazz2
{
	class IRootController;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::StringView;
#endif

	class EditorLevelHandler;
	class LevelDocument;

	/** @brief Where a play-test starts */
	enum class PlayStartMode : std::uint8_t
	{
		Resume,			/**< Where the last play-test was left, as it was if the level wasn't changed since */
		Here,			/**< At the pointer, or at the center of the view */
		FromStart		/**< At the start of the level */
	};

	/** @brief State of the play-tests of the level editor, see @ref PlayTestController */
	enum class PlayTestMode : std::uint8_t
	{
		Edit,			/**< The level is edited */
		Entering,		/**< The view flies into the level, it's played once it's there */
		Play,			/**< The level is played */
		Returning		/**< The level is edited again, while the ring back into the editor grows */
	};

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
		/** @brief How long the level was played, in frames at 60 FPS */
		float Duration = 0.0f;

		/** @brief Clears the path */
		void Clear();
		/** @brief Returns the number of points of all segments */
		std::int32_t GetPointCount() const;
		/** @brief Returns how many times the player died */
		std::int32_t GetDeathCount() const;
		/** @brief Returns `true` if the level was left by its exit */
		bool HasExited() const;
		/** @brief Adds where a player is now, called every frame */
		void Record(Vector2f pos, float speed, float timeMult);
		/** @brief Ends the segment being recorded */
		void BreakSegment(Break reason);

	private:
		float _time = 0.0f;
		// Points of all segments, counted again before the oldest are dropped, the segments are public
		std::int32_t _pointCount = 0;
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
		/** @brief Whether Reforged Gameplay is enabled */
		bool Reforged = true;
		/** @brief Whether players may climb ledges */
		bool LedgeClimb = true;
	};

	/**
		@brief What a @ref PlayTestController needs of the level editor that hosts it
	*/
	class IPlayTestHost
	{
	public:
		virtual ~IPlayTestHost() = default;

		/** @brief Returns the level being edited */
		virtual LevelDocument& GetDocument() = 0;
		/** @brief Returns the revision of the level being edited, see @ref CommandHistory::GetRevision() */
		virtual std::uint32_t GetDocumentRevision() const = 0;
		/** @brief Returns the name of the level as episode/level */
		virtual StringView GetLevelName() const = 0;
		/** @brief Returns the path of the level file, may be empty */
		virtual StringView GetFilePath() const = 0;
		/** @brief Returns the level handler that shows the level being edited, or `nullptr` */
		virtual EditorLevelHandler* GetEditHandler() const = 0;
		/** @brief Returns the root controller the level handlers of play-tests get, it ends the play-test when they leave the level */
		virtual IRootController& GetPlayTestRoot() = 0;
		/** @brief Returns a weak reference to the host, callbacks run later only while it's alive */
		virtual std::weak_ptr<void> GetPlayTestHostReference() = 0;
		/** @brief Sets up the view again, the game or the editor is shown now */
		virtual void OnPlayTestViewChanged() = 0;
		/**
		 * @brief Called once the level is edited again, the changes made meanwhile are shown then
		 *
		 * Returns `true` if the level shown was loaded again for them, its view is set up then, as by
		 * @ref OnPlayTestViewChanged().
		 */
		virtual bool OnPlayTestReturned() = 0;
		/** @brief Returns the color of the rings into the level and back */
		virtual Colorf GetAccentColor() = 0;
		/** @brief Shows a short message */
		virtual void ShowMessage(StringView text) = 0;
		/** @brief Leaves the editor, as chosen in the pause menu of a play-test */
		virtual void ExitEditor() = 0;
	};
}

#endif
