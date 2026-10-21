#include "EditorSessionPrivate.h"

#if defined(WITH_EDITOR)

// Playing the level being edited, of EditorSession

namespace Jazz2::Editor
{
	bool EditorSession::IsPlaying() const
	{
		return !_playTest->IsEditing();
	}

	PlayTestController& EditorSession::GetPlayTest()
	{
		return *_playTest;
	}

	const PlayPath& EditorSession::GetLastPlayPath() const
	{
		return _playTest->GetLastPlayPath();
	}

	const PlayPathHistory& EditorSession::GetPlayPathHistory() const
	{
		return _playTest->GetPathHistory();
	}

	bool EditorSession::ArePanelsShown() const
	{
		return _playTest->ArePanelsShown();
	}

	PlayTransition& EditorSession::GetPlayTransition()
	{
		return _playTest->GetTransition();
	}

	bool EditorSession::ComputeReachability(SmallVectorImpl<std::int32_t>& distances)
	{
		// With the moves of any character, as Check Level asks whether the level can be finished at all
		return _playOverlays->ComputeReachability(distances, 0, false);
	}

	void EditorSession::GoToMainMenu(bool afterIntro)
	{
		if (!_playTest->IsEditing()) {
			// A play-test returns to the editor instead
			_playTest->ReturnFromGame(_x("editor", "Back in the editor"), false);
			return;
		}
		_root->GoToMainMenu(afterIntro);
	}

	void EditorSession::ChangeLevel(LevelInitialization&& levelInit)
	{
		// The level being edited is never left for another one, the play-test ends
		if (!_playTest->IsEditing()) {
			String levelName = levelInit.LevelName;
			bool gameOver = levelName.hasSuffix(":gameover"_s);
			if (gameOver || levelName.empty() || levelName.contains(':')) {
				_playTest->ReturnFromGame(gameOver ? _x("editor", "Game over") : _x("editor", "The level was finished"), !gameOver);
				return;
			}
			// A round of multiplayer ends by playing the same level again, the play-test ends with the round instead (see
			// PlayTestMpLevelHandler::HandleLevelChange())
			if (levelInit.LocalMultiplayerGameMode != 0 && StringUtils::equalsIgnoreCase(levelName, _files->GetLevelName())) {
				_playTest->ReturnFromGame(_x("editor", "The round is over"), false);
				return;
			}
			// The level that follows can be edited or played on, as the episode goes, with what the player has now
			auto next = std::make_shared<LevelInitialization>(Death::move(levelInit));
			_playTest->ReturnFromGame(_x("editor", "The level was finished"), true, [this, next, levelName]() {
				OfferNextLevel(next, levelName);
			});
		}
	}

	String EditorSession::FindNextLevelFile(StringView levelName)
	{
		// Next to the level being edited first, as its episode is there, then in the content of the game
		auto parts = levelName.partition('/');
		StringView fileName = (parts[2].empty() ? parts[0] : parts[2]);
		StringView filePath = _files->GetFilePath();
		if (!filePath.empty()) {
			String nearby = fs::CombinePath(fs::GetDirectoryName(filePath), String(fileName + ".j2l"_s));
			if (fs::IsReadableFile(nearby)) {
				return nearby;
			}
		}
		// As the game looks for it, see Level Properties
		SmallVector<UiFileRoot, 4> roots;
		GetFileRoots(false, roots);
		SmallVector<String, 4> episodeRoots;
		for (const UiFileRoot& root : roots) {
			episodeRoots.push_back(root.Path);
		}
		return FindLevelFile(levelName, _files->GetLevelName(), episodeRoots);
	}

	void EditorSession::OfferNextLevel(const std::shared_ptr<LevelInitialization>& next, StringView levelName)
	{
		if (_surface == nullptr || !_playTest->IsEditing()) {
			return;
		}
		String path = FindNextLevelFile(levelName);
		if (path.empty()) {
			ShowMessage(_fx("editor", "The level \"{}\" that follows wasn't found", levelName));
			return;
		}
		UiMessageDialog::Show(*_surface, _x("editor", "Level Finished"), _fx("editor", "The level that follows is \"{}\".", levelName),
			// TRANSLATORS: Buttons of the dialog after a play-tested level was finished - open the following level in the editor, leave the editor and play it, or stay in this level
			{ _x("editor", "Edit It"), _x("editor", "Play On"), _x("editor", "Stay Here") }, [this, next, path](std::int32_t button) {
			if (button == 0) {
				OpenFile(path);
			} else if (button == 1) {
				// The game goes on with the next level, as the episode would, the editor is left for it
				_files->ConfirmDiscard([this, next]() {
					_root->ChangeLevel(Death::move(*next));
				});
			}
		});
	}

	void EditorSession::StartPlayTest(PlayStartMode mode)
	{
		if (!_playTest->IsEditing() || _editHandler == nullptr || _editHandler->TileMap() == nullptr) {
			return;
		}
		if (_surface != nullptr && _surface->HasModal()) {
			// A dialog waits for an answer, it would stay over the game, and what it changes wouldn't be played
			return;
		}

		CancelInteractions(true);
		_tools->GetActive()->Cancel();
		_input->ReleasePointer(true);
		// The sound of the selected event isn't heard in the game, see UpdateEventSound()
		_eventPreviews.StopSound();
		if (UiSurface* surface = _surface.get()) {
			surface->ClosePopups();
			surface->SetFocus(nullptr);
		}

		PlayTestOptions options;
		options.Player = PlayerType(_settings.PlayCharacter);
		options.Difficulty = GameDifficulty(_settings.PlayDifficulty);
#if defined(WITH_MULTIPLAYER)
		options.GameMode = std::uint8_t(_settings.PlayGameMode);
		options.PlayerCount = std::clamp<std::int32_t>(_settings.PlayPlayerCount, 1, LevelInitialization::MaxPlayerCount);
#endif

		if (mode == PlayStartMode::Here) {
			// At the pointer by the keyboard, chosen from a menu or by a button the pointer is there, so in the middle
			ToolPointer pointer;
			options.HasSpawnPosition = true;
			if (_input->IsHandlingShortcut() && GetHoverPointer(pointer)) {
				options.SpawnPosition = pointer.World;
			} else {
				Rectf view = GetViewRect();
				std::int32_t spriteLayer = std::max<std::int32_t>(_document.GetSpriteLayerIndex(), 0);
				options.SpawnPosition = GetLayerView(spriteLayer).ScreenToWorld(Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f));
			}
		}

		// Where the last play-test was left is known to the controller
		_playTest->Enter(mode, options);
	}

	void EditorSession::StopPlayTest(bool keep)
	{
		_playTest->Stop(keep);
	}

	void EditorSession::ResetPlayState()
	{
		_playTest->Reset();
		_playOverlays->HideReachability();
	}

	std::uint32_t EditorSession::GetDocumentRevision() const
	{
		return (_history != nullptr ? _history->GetRevision() : 0);
	}

	IRootController& EditorSession::GetPlayTestRoot()
	{
		// What the level handlers of play-tests would do to the game as a whole is decided by the session
		return *this;
	}

	std::weak_ptr<void> EditorSession::GetPlayTestHostReference()
	{
		return weak_from_this();
	}

	void EditorSession::OnPlayTestViewChanged()
	{
		Vector2i res = theApplication().GetResolution();
		OnInitializeViewport(res.X, res.Y);
	}

	bool EditorSession::OnPlayTestReturned()
	{
		std::uint32_t buildCount = _runtimeBuildCount;
		FlushDocumentChanges();
		_files->UpdateWindowTitle();
		return (_runtimeBuildCount != buildCount);
	}

	void EditorSession::ApplyPlayTestDebugOptions()
	{
		// Kept in the settings, but whether the time stands still is only of the play-test played now
		const EditorSettings::PlayTestSettings& settings = _settings.PlayTest;
		PlayTestDebugOptions options = _playTest->GetDebugOptions();
		options.TimeScale = settings.TimeScale;
		options.ShowHitboxes = settings.ShowHitboxes;
		options.ShowEventZones = settings.ShowEventZones;
		_playTest->SetDebugOptions(options);
	}

	void EditorSession::TogglePlayTestDebug(bool EditorSettings::PlayTestSettings::*option, StringView onMessage, StringView offMessage)
	{
		bool& value = _settings.PlayTest.*option;
		value = !value;
		_settings.Save();
		ApplyPlayTestDebugOptions();
		if (_playTest->IsInGame()) {
			ShowMessage(value ? onMessage : offMessage);
		}
	}

	void EditorSession::CyclePlayTestTimeScale()
	{
		// Normal, half, a quarter, and back
		float& timeScale = _settings.PlayTest.TimeScale;
		timeScale = (timeScale > 0.75f ? 0.5f : (timeScale > 0.375f ? 0.25f : 1.0f));
		_settings.Save();
		ApplyPlayTestDebugOptions();
		if (_playTest->IsInGame()) {
			// TRANSLATORS: Status messages after changing the speed of a running play-test, {} is the speed of the game in percent
			ShowMessage(timeScale >= 1.0f ? String(_x("editor", "Normal speed")) : String(_fx("editor", "Slow motion, {}%", std::int32_t(std::round(timeScale * 100.0f)))));
		}
	}

	void EditorSession::TogglePlayTestPause()
	{
		if (!_playTest->IsInGame()) {
			return;
		}
		PlayTestDebugOptions options = _playTest->GetDebugOptions();
		options.Paused = !options.Paused;
		_playTest->SetDebugOptions(options);
		ShowMessage(options.Paused ? _x("editor", "Paused - F8 goes on frame by frame") : _x("editor", "Running again"));
	}

	void EditorSession::TeleportPlayTestPlayer()
	{
		Vector2f pointerPos;
		if (!_playTest->IsInGame() || !_input->GetPointerPos(pointerPos)) {
			return;
		}
		Vector2i res = theApplication().GetResolution();
		if (!_playTest->TeleportPlayer(pointerPos, Vector2f(float(res.X), float(res.Y)))) {
			ShowMessage(_x("editor", "There's no room for the player there"));
		}
	}
}

#endif
