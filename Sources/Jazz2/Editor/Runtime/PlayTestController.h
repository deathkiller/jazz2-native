#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PlayPathHistory.h"
#include "PlayTest.h"
#include "PlayTestDebug.h"
#include "PlayTestTypes.h"
#include "PlayTransition.h"
#include "../Document/DocumentChanges.h"
#include "../Document/LevelDocument.h"
#include "../../IRootController.h"

#include <memory>

namespace Jazz2::Editor
{
	class EditorLevelHandler;

	/**
		@brief Plays the level being edited, and returns to editing it

		Goes through @ref PlayTestMode::Edit, @ref PlayTestMode::Entering --- the view of the editor flies to where the game
		will show the player --- and @ref PlayTestMode::Play, where the game is revealed in a ring growing over the last
		image of the editor, back to editing in @ref PlayTestMode::Returning, where the editor is revealed in a ring over the
		last image of the game. It owns the play-tests: the one played, the one kept to be resumed, and the one left whose
		last image the ring back still shows.

		A kept play-test goes on exactly as it was left if the level is still as it was made from, otherwise the new one
		starts where the player was, with what the player had, and what was collected or destroyed stays so in the cells
		that weren't edited, see @ref PlayStartMode::Resume. The music is handed from the editor to the game and back,
		opened up and muffled again over a while.
	*/
	class PlayTestController
	{
	public:
		/** @brief Creates a new instance */
		explicit PlayTestController(IPlayTestHost& host);
		~PlayTestController();

		PlayTestController(const PlayTestController&) = delete;
		PlayTestController& operator=(const PlayTestController&) = delete;

		/** @brief Returns the state */
		PlayTestMode GetMode() const {
			return _mode;
		}
		/** @brief Returns `true` while the level is edited, also while the ring back into the editor grows */
		bool IsEditing() const {
			return (_mode == PlayTestMode::Edit || _mode == PlayTestMode::Returning);
		}
		/** @brief Returns `true` while the view flies into the level */
		bool IsEntering() const {
			return (_mode == PlayTestMode::Entering);
		}
		/** @brief Returns `true` while the level is played */
		bool IsInGame() const {
			return (_mode == PlayTestMode::Play);
		}
		/** @brief Returns `true` if the panels of the editor are shown, while it's used and while the ring into the game grows over them */
		bool ArePanelsShown() const {
			return (_mode != PlayTestMode::Play || (_transition.IsActive() && _transition.HasInside()));
		}

		/** @brief Returns the play-test played or kept, or `nullptr` */
		IPlayTest* GetPlayTest() const {
			return _playHandler.get();
		}
		/** @brief Returns the play-test left, while the ring back into the editor shows its last image, or `nullptr` */
		IPlayTest* GetClosingPlayTest() const {
			return _closingPlayHandler.get();
		}
		/** @brief Returns the level handler of the game while the level is played, or `nullptr` */
		LevelHandler* GetGameHandler() const;
		/** @brief Returns the ring into the level and back */
		PlayTransition& GetTransition() {
			return _transition;
		}
		/** @brief Returns the path the player took in the last play-test */
		const PlayPath& GetLastPlayPath() const {
			return _lastPlayPath;
		}
		/** @brief Returns the paths of the last play-tests, kept in the level */
		const PlayPathHistory& GetPathHistory() const {
			return _pathHistory;
		}
		/** @brief Reads the paths of the last play-tests from the level, the last one becomes the path shown */
		void ReadPathHistory(const LevelDocument& document);
		/** @brief Forgets the paths of the play-tests */
		void ClearPathHistory();
		/** @brief Returns where the camera of the editor was when the level started to be played */
		Vector2f GetEditCameraCenter() const {
			return _editCameraCenter;
		}
		/** @brief Returns the scale of the camera of the editor when the level started to be played */
		float GetEditCameraScale() const {
			return _editCameraScale;
		}

		/**
		 * @brief Starts playing the level, the view of the editor flies into it first
		 *
		 * @p options say how it's played, with a spawn position already for @ref PlayStartMode::Here.
		 */
		void Enter(PlayStartMode mode, const PlayTestOptions& options);
		/** @brief Stops playing, or the flight into the level, @p keep keeps the play-test to resume later */
		void Stop(bool keep);
		/**
		 * @brief Returns to editing after the frame of the game, which a play-test can't be destroyed inside of
		 *
		 * With @p exited the path of the player ends by leaving the level. @p afterwards is called once the level is
		 * edited again.
		 */
		void ReturnFromGame(StringView message, bool exited, Function<void()>&& afterwards = {});
		/** @brief Forgets all play-tests and their path, as before another level is edited */
		void Reset();
		/** @brief Releases the play-tests and the rings at once, without returning to the editor, as when it's closed */
		void Release();

		/**
		 * @brief Tells about changes of the level being edited
		 *
		 * Changes of only tiles and events of the grid are given to a kept play-test when it's resumed, instead of
		 * starting it again, see @ref PlayStartMode::Resume.
		 */
		void NoteDocumentChanges(const DocumentChanges& changes);
		/** @brief Makes the kept play-test start again when it's resumed, as after its script changed */
		void MarkPlayTestOutdated();

		/** @brief Returns the debug toggles of play-tests */
		const PlayTestDebugOptions& GetDebugOptions() const {
			return _debugOptions;
		}
		/** @brief Sets the debug toggles of play-tests, applied to the one played now and to every one played later */
		void SetDebugOptions(const PlayTestDebugOptions& options);
		/** @brief Advances the time of the level played by a single update, it stands still then */
		void StepFrame();
		/**
		 * @brief Moves the first player to where a position on the screen shows in the level played
		 *
		 * @p screenPos and @p screenSize are in pixels of the drawable. Returns `false` if the level isn't played or
		 * there's no place for the player near there.
		 */
		bool TeleportPlayer(Vector2f screenPos, Vector2f screenSize);

		/** @brief Advances the music opening up or being muffled again */
		void UpdateMusicFade(float timeMult);
		/** @brief Advances the rings, the play-test left is released once the ring back is done */
		void UpdateTransition(float timeMult);
		/** @brief Plays the level once the view flew into it and a frame showing it was drawn, call it after the frame of the editor */
		void UpdateEntering();

	private:
		IPlayTestHost& _host;
		PlayTestMode _mode;
		// Alone or in local splitscreen, see PlayTestLevelHandler and PlayTestMpLevelHandler
		std::shared_ptr<IPlayTest> _playHandler;
		// A play-test that was left, kept only for its last frame while the ring back into the editor is shown
		std::shared_ptr<IPlayTest> _closingPlayHandler;
		// Revision of the level the kept play-test was made from, see Finish()
		std::uint32_t _playRevision;
		PlayTestOptions _playOptions;
		// How the play-test kept was started
		PlayTestOptions _playedOptions;
		bool _resumePlay;
		// The new play-test takes what it can from the kept one, which stays until the new one replaced it
		bool _restorePlayState;
		bool _enteringFrameShown;
		Vector2f _editCameraCenter;
		float _editCameraScale;
		PlayPath _lastPlayPath;
		PlayPathHistory _pathHistory;
		// Each play-test made gets a number, so its path replaces its own in the history, see PlayPathHistory::Update()
		std::uint32_t _playTestCount;
		std::uint32_t _playTestNumber;
		PlayTransition _transition;
		// The music opens up when the level is played and is muffled again in the editor, over a while
		float _musicFadeFrom;
		float _musicFadeTo;
		float _musicFadeTime;
		// The sprite layer and the events the kept play-test was made from, so it's known what was edited since
		SmallVector<TileRef, 0> _suspendedSpriteTiles;
		Vector2i _suspendedSpriteSize;
		SmallVector<DocumentEvent, 0> _suspendedEvents;
		SmallVector<Formats::OffGridEventRecord, 0> _suspendedOffGridEvents;
		// The chunks the game reads from the level the kept play-test was made from, as the translations of its texts
		SmallVector<Formats::ChunkRecord, 0> _suspendedGameChunks;
		// Where the player was, where memory is too short to keep the play-test
		Vector2f _resumePosition;
		bool _hasResumePosition;
		// See SetDebugOptions(), given to every play-test
		PlayTestDebugOptions _debugOptions;
		// What changed since the kept play-test was made, see NoteDocumentChanges()
		DocumentChanges _changesSincePlay;

		// Returns `true` if the kept play-test was made from the level as it is now
		bool IsPlayTestCurrent() const;
		// Switches to the play-test once the view flew into the level
		void Finish();
		void StartMusicFade(float from, float to);
		// Remembers the sprite layer and the events a play-test is made from, see RestorePlayState()
		void RememberSuspendedLevel();
		// Takes what the player had and what was collected or destroyed from a play-test into a new one, cells of the
		// sprite layer and events edited since the old play-test was made start as new
		void RestorePlayState(IPlayTest& from, IPlayTest& to);
		// The pause menu of a play-test can leave the editor
		void SetPlayTestExit(IPlayTest& handler);
		// Gives the kept play-test the tiles and the events changed since it was made, returns `false` if the changes
		// can't be given to a level being played and it has to start again
		bool PushChanges();
	};
}

#endif
