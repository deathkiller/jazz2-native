#include "HighscoresSection.h"
#include "MenuResources.h"
#include "../DiscordRpcClient.h"

#include "../../../nCine/Application.h"

#include <Utf8.h>
#include <Containers/StringConcatenable.h>
#include <IO/Compression/DeflateStream.h>

#if defined(DEATH_TARGET_ANDROID)
#	include "../../../nCine/Backends/Android/AndroidApplication.h"
#	include "../../../nCine/Backends/Android/AndroidJniHelper.h"
#endif

using namespace Death::IO::Compression;
using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	HighscoresSection::HighscoresSection()
		: _selectedSeries(0), _notValidPos(-1), _notValidSeries(-1), _textCursor(0), _carretAnim(0.0f), _waitForInput(false)
#if defined(DEATH_TARGET_ANDROID)
			, _recalcVisibleBoundsTimeLeft(30.0f)
#endif
	{
		DeserializeFromFile();
		FillDefaultsIfEmpty();
		RefreshList();

#if defined(DEATH_TARGET_ANDROID)
		_currentVisibleBounds = AndroidJniWrap_Activity::getVisibleBounds();
		_initialVisibleSize.X = _currentVisibleBounds.W;
		_initialVisibleSize.Y = _currentVisibleBounds.H;
#endif
	}

	HighscoresSection::HighscoresSection(std::int32_t seriesIndex, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, std::uint64_t elapsedMilliseconds, const PlayerCarryOver& itemToAdd)
		: HighscoresSection()
	{
		if (seriesIndex >= 0 && seriesIndex < (std::int32_t)SeriesName::Count) {
			_selectedSeries = seriesIndex;

			String playerName;
			std::uint64_t userId = 0;
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			if (PreferencesCache::EnableDiscordIntegration && DiscordRpcClient::Get().IsSupported()) {
				playerName = DiscordRpcClient::Get().GetUserDisplayName();
				userId = DiscordRpcClient::Get().GetUserId();
			}
#endif
			if (playerName.empty()) {
				playerName = PreferencesCache::PlayerName;
				if (playerName.empty()) {
					playerName = theApplication().GetUserName();
					if (playerName.empty()) {
						playerName = "Me"_s;
					}
				}
			}
			if (playerName.size() > MaxPlayerNameLength) {
				auto [_, prevChar] = Utf8::PrevChar(playerName, MaxPlayerNameLength);
				playerName = playerName.prefix(prevChar);
			}

			HighscoreFlags flags = HighscoreFlags::None;
			if (isReforged) flags |= HighscoreFlags::IsReforged;
			if (cheatsUsed) flags |= HighscoreFlags::CheatsUsed;

			// TODO: PlayerId is unused
			AddItemAndFocus(HighscoreItem { std::move(playerName), userId, flags, itemToAdd.Type, difficulty, itemToAdd.Lives, itemToAdd.Score,
				{ itemToAdd.Gems[0], itemToAdd.Gems[1], itemToAdd.Gems[2], itemToAdd.Gems[3] }, DateTime::UtcNow().ToUnixMilliseconds(), elapsedMilliseconds });
		}
	}

	void HighscoresSection::OnUpdate(float timeMult)
	{
		// Move the variable to stack to fix leaving the section
		bool waitingForInput = _waitForInput;

		ScrollableMenuSection::OnUpdate(timeMult);

		if (waitingForInput) {
#if defined(DEATH_TARGET_ANDROID)
			_recalcVisibleBoundsTimeLeft -= timeMult;
			if (_recalcVisibleBoundsTimeLeft <= 0.0f) {
				_recalcVisibleBoundsTimeLeft = 60.0f;
				_currentVisibleBounds = AndroidJniWrap_Activity::getVisibleBounds();
			}

			if (_root->ActionHit(PlayerAction::ChangeWeapon)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				auto& app = static_cast<AndroidApplication&>(theApplication());
				app.ToggleSoftInput();
			} else
#endif
			if (_root->ActionHit(PlayerAction::Menu) || _root->ActionHit(PlayerAction::Run)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_waitForInput = false;
				auto& selectedItem = _items[_selectedIndex];
				if (!selectedItem.Item->PlayerName.empty()) {
					SerializeToFile();
				}
			} else if (_root->ActionHit(PlayerAction::Fire)) {
				auto& selectedItem = _items[_selectedIndex];
				if (!selectedItem.Item->PlayerName.empty()) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_waitForInput = false;
					SerializeToFile();
				}
			}

			EnsureVisibleSelected();

			_carretAnim += timeMult;
		}
	}

	void HighscoresSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;

		SeriesName selectedSeries = (SeriesName)_selectedSeries;
		if (selectedSeries == SeriesName::BaseGame) {
			_root->DrawStringShadow(_("Highscores for \f[c:#d0705d]Base game\f[/c]"), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
				Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
		} else {
			const char* episodeName;
			switch ((SeriesName)_selectedSeries) {
				case SeriesName::SharewareDemo: episodeName = "Shareware Demo"; break;
				case SeriesName::TheSecretFiles: episodeName = "The Secret Files"; break;
				default: episodeName = "Unknown"; break;
			}

			_root->DrawStringShadow(_f("Highscores for \f[c:#d0705d]%s\f[/c]", episodeName), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
				Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
		}

		_root->DrawStringShadow("<"_s, charOffset, centerX - 70.0f - 100.0f, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Right, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
		_root->DrawStringShadow(">"_s, charOffset, centerX + 80.0f + 100.0f, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Right, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
	}

	void HighscoresSection::OnDrawOverlay(Canvas* canvas)
	{
#if defined(DEATH_TARGET_ANDROID)
		if (_waitForInput) {
			_root->DrawElement(ShowKeyboard, -1, 36.0f, 24.0f, IMenuContainer::MainLayer, Alignment::TopLeft, Colorf::White);

			if (_currentVisibleBounds.W < _initialVisibleSize.X || _currentVisibleBounds.H < _initialVisibleSize.Y) {
				Vector2i viewSize = _root->GetViewSize();
				if (_currentVisibleBounds.Y * viewSize.Y / _initialVisibleSize.Y < 32.0f) {
					_root->DrawSolid(0.0f, 0.0f, IMenuContainer::MainLayer - 10, Alignment::TopLeft, Vector2f(viewSize.X, viewSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.6f));

					auto& selectedItem = _items[_selectedIndex];

					std::int32_t charOffset = 0;
					_root->DrawStringShadow(selectedItem.Item->PlayerName, charOffset, 120.0f, 52.0f, IMenuContainer::MainLayer,
						Alignment::Left, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 1.0f);

					Vector2f textToCursorSize = _root->MeasureString(selectedItem.Item->PlayerName.prefix(_textCursor), 1.0f);
					_root->DrawSolid(120.0f + textToCursorSize.X + 1.0f, 52.0f - 1.0f, IMenuContainer::MainLayer + 10, Alignment::Left, Vector2f(1.0f, 14.0f),
						Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_carretAnim * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
				}
			}
		}
#endif
	}

	void HighscoresSection::OnKeyPressed(const KeyboardEvent& event)
	{
		if (_waitForInput) {
			switch (event.sym) {
				case Keys::Escape: {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_waitForInput = false;
					auto& selectedItem = _items[_selectedIndex];
					if (!selectedItem.Item->PlayerName.empty()) {
						SerializeToFile();
					}
					break;
				}
				case Keys::Return: {
					auto& selectedItem = _items[_selectedIndex];
					if (!selectedItem.Item->PlayerName.empty()) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_waitForInput = false;
						SerializeToFile();
					}
					break;
				}
				case Keys::Backspace: {
					auto& selectedItem = _items[_selectedIndex];
					if (_textCursor > 0) {
						auto [_, prevPos] = Utf8::PrevChar(selectedItem.Item->PlayerName, _textCursor);
						selectedItem.Item->PlayerName = selectedItem.Item->PlayerName.prefix(prevPos) + selectedItem.Item->PlayerName.exceptPrefix(_textCursor);
						_textCursor = prevPos;
						_carretAnim = 0.0f;
					}
					break;
				}
				case Keys::Delete: {
					auto& selectedItem = _items[_selectedIndex];
					if (_textCursor < selectedItem.Item->PlayerName.size()) {
						auto [_, nextPos] = Utf8::NextChar(selectedItem.Item->PlayerName, _textCursor);
						selectedItem.Item->PlayerName = selectedItem.Item->PlayerName.prefix(_textCursor) + selectedItem.Item->PlayerName.exceptPrefix(nextPos);
						_carretAnim = 0.0f;
					}
					break;
				}
				case Keys::Left: {
					auto& selectedItem = _items[_selectedIndex];
					if (_textCursor > 0) {
						auto [c, prevPos] = Utf8::PrevChar(selectedItem.Item->PlayerName, _textCursor);
						_textCursor = prevPos;
						_carretAnim = 0.0f;
					}
					break;
				}
				case Keys::Right: {
					auto& selectedItem = _items[_selectedIndex];
					if (_textCursor < selectedItem.Item->PlayerName.size()) {
						auto [c, nextPos] = Utf8::NextChar(selectedItem.Item->PlayerName, _textCursor);
						_textCursor = nextPos;
						_carretAnim = 0.0f;
					}
					break;
				}
			}
		}
	}

	void HighscoresSection::OnTextInput(const nCine::TextInputEvent& event)
	{
		if (_waitForInput) {
			auto& selectedItem = _items[_selectedIndex];
			if (selectedItem.Item->PlayerName.size() + event.length <= MaxPlayerNameLength) {
				selectedItem.Item->PlayerName = selectedItem.Item->PlayerName.prefix(_textCursor)
					+ StringView(event.text, event.length)
					+ selectedItem.Item->PlayerName.exceptPrefix(_textCursor);
				_textCursor += event.length;
				_carretAnim = 0.0f;
			}
		}
	}

	NavigationFlags HighscoresSection::GetNavigationFlags() const
	{
		return (_waitForInput ? NavigationFlags::AllowGamepads : NavigationFlags::AllowAll);
	}

	std::int32_t HighscoresSection::TryGetSeriesIndex(StringView episodeName, bool playerDied)
	{
		if (episodeName == "monk"_s || (playerDied && (episodeName == "prince"_s || episodeName == "rescue"_s || episodeName == "flash"_s))) {
			return (std::int32_t)SeriesName::BaseGame;
		} else if (episodeName == "share"_s) {
			return (std::int32_t)SeriesName::SharewareDemo;
		} else if (episodeName == "secretf"_s) {
			return (std::int32_t)SeriesName::TheSecretFiles;
		} else {
			return -1;
		}
	}

	void HighscoresSection::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		item.Height = ItemHeight * 5 / 8;
	}

	void HighscoresSection::OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;
		float nameX = centerX * 0.36f;
		char stringBuffer[64];

		if (isSelected) {
			_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer - 200, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.1f), 26.0f, 5.0f, true, true);
		}

		std::int32_t pos = (std::int32_t)(&item - &_items[0] + 1);
		bool isNotValid = (_notValidPos == pos && _notValidSeries == _selectedSeries);
		if (_notValidPos > 0 && _notValidPos < pos && _notValidSeries == _selectedSeries) {
			pos--;
		}
		if (!isNotValid && pos <= MaxItems) {
			formatString(stringBuffer, sizeof(stringBuffer), "%i.", pos);
			_root->DrawStringShadow(stringBuffer, charOffset, nameX - 16.0f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Right,
				(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f);
		}

		bool cheatsUsed = (item.Item->Flags & HighscoreFlags::CheatsUsed) == HighscoreFlags::CheatsUsed;

		Colorf nameColor;
		if (isSelected && _waitForInput) {
			nameColor = Colorf(0.62f, 0.44f, 0.34f, 0.5f);
		} else if (isSelected) {
			nameColor = (cheatsUsed ? Colorf(0.6f, 0.43f, 0.43f, 0.5f) : Colorf(0.48f, 0.48f, 0.48f, 0.5f));
		} else {
			nameColor = (cheatsUsed ? Colorf(0.48f, 0.38f, 0.34f, 0.5f) : Font::DefaultColor);
		}

		_root->DrawStringShadow(item.Item->PlayerName, charOffset, nameX, item.Y, IMenuContainer::MainLayer - 100, Alignment::Left, nameColor, 0.8f);

		if (item.Item->Lives <= 0) {
			Vector2f nameSize = _root->MeasureString(item.Item->PlayerName, 0.8f);
			_root->DrawElement(RestInPeace, -1, nameX + nameSize.X + 12.0f, item.Y - 2.0f, IMenuContainer::MainLayer - 100, Alignment::Left, Colorf::White, 1.0f, 1.0f);
		}

		u32tos(item.Item->Score, stringBuffer);
		_root->DrawStringShadow(stringBuffer, charOffset, centerX * 1.06f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Right,
			(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.92f);

		_root->DrawElement(PickupGemRed, -1, centerX * 1.18f - 6.0f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Right, Colorf(1.0f, 1.0f, 1.0f, 0.8f), 0.46f, 0.46f);

		formatString(stringBuffer, sizeof(stringBuffer), "%i · %i · %i · %i", item.Item->Gems[0], item.Item->Gems[1], item.Item->Gems[2], item.Item->Gems[3]);
		_root->DrawStringShadow(stringBuffer, charOffset, centerX * 1.18f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Left,
			(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f,0.0f, 0.0f, 0.0f, 0.0f, 0.8f);

		std::int64_t elapsedSeconds = (item.Item->ElapsedMilliseconds / 1000);
		if (elapsedSeconds > 0) {
			_root->DrawElement(PickupStopwatch, -1, centerX * 1.58f - 6.0f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Right, Colorf::White, 0.55f, 0.55f);

			std::int32_t elapsedHours = (elapsedSeconds / 3600);
			std::int32_t elapsedMinutes = (elapsedSeconds / 60);
			elapsedSeconds -= (elapsedMinutes * 60);
			elapsedMinutes -= (elapsedHours * 60);
			formatString(stringBuffer, sizeof(stringBuffer), "%i:%02i:%02i", elapsedHours, elapsedMinutes, elapsedSeconds);
			_root->DrawStringShadow(stringBuffer, charOffset, centerX * 1.72f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Right,
				(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		}

		if (isSelected && _waitForInput) {
			Vector2f textToCursorSize = _root->MeasureString(item.Item->PlayerName.prefix(_textCursor), 0.8f);
			_root->DrawSolid(nameX + textToCursorSize.X + 1.0f, item.Y - 1.0f, IMenuContainer::MainLayer - 80, Alignment::Center, Vector2f(1.0f, 12.0f),
				Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_carretAnim * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
		}
	}

	void HighscoresSection::OnHandleInput()
	{
		if (_waitForInput) {
			return;
		}

		if (_root->ActionHit(PlayerAction::Menu)) {
			OnBackPressed();
		} else if (_root->ActionHit(PlayerAction::Fire)) {
			OnExecuteSelected();
		} else if (_root->ActionHit(PlayerAction::Up)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex > 0) {
				_selectedIndex--;
			} else {
				_selectedIndex = (std::int32_t)(_items.size() - 1);
			}
			EnsureVisibleSelected();
			OnSelectionChanged(_items[_selectedIndex]);
		} else if (_root->ActionHit(PlayerAction::Down)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex < (std::int32_t)(_items.size() - 1)) {
				_selectedIndex++;
			} else {
				_selectedIndex = 0;
			}
			EnsureVisibleSelected();
			OnSelectionChanged(_items[_selectedIndex]);
		} else if (_root->ActionHit(PlayerAction::Left)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			_selectedSeries--;
			if (_selectedSeries < 0) {
				_selectedSeries = (std::int32_t)SeriesName::Count - 1;
			}
			RefreshList();
			if (!_items.empty()) {
				if (_selectedIndex >= _items.size()) {
					_selectedIndex = _items.size() - 1;
				}
				EnsureVisibleSelected();
				OnSelectionChanged(_items[_selectedIndex]);
			}
		} else if (_root->ActionHit(PlayerAction::Right)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			_selectedSeries++;
			if (_selectedSeries >= (std::int32_t)SeriesName::Count) {
				_selectedSeries = 0;
			}
			RefreshList();
			if (!_items.empty()) {
				if (_selectedIndex >= _items.size()) {
					_selectedIndex = _items.size() - 1;
				}
				EnsureVisibleSelected();
				OnSelectionChanged(_items[_selectedIndex]);
			}
		}
	}

	void HighscoresSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
#if defined(DEATH_TARGET_ANDROID)
				if (x < 0.2f && y < 80.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					auto& app = static_cast<AndroidApplication&>(theApplication());
					app.ToggleSoftInput();
					return;
				} else
#endif
				if (y >= 80.0f) {
					Recti contentBounds = _root->GetContentBounds();
					float topLine = contentBounds.Y + TopLine;
					if (std::abs(x - 0.5f) > (y > topLine ? 0.35f : 0.1f)) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_animation = 0.0f;

						if (x < 0.5f) {
							_selectedSeries--;
							if (_selectedSeries < 0) {
								_selectedSeries = (std::int32_t)SeriesName::Count - 1;
							}
						} else {
							_selectedSeries++;
							if (_selectedSeries >= (std::int32_t)SeriesName::Count) {
								_selectedSeries = 0;
							}
						}

						RefreshList();
						if (!_items.empty()) {
							if (_selectedIndex >= _items.size()) {
								_selectedIndex = _items.size() - 1;
							}
							EnsureVisibleSelected();
							OnSelectionChanged(_items[_selectedIndex]);
						}
						return;
					}
				}
			}
		}

		ScrollableMenuSection::OnTouchEvent(event, viewSize);
	}

	void HighscoresSection::OnTouchUp(std::int32_t newIndex, Vector2i viewSize, Vector2i touchPos)
	{
		if (!_waitForInput) {
			ScrollableMenuSection::OnTouchUp(newIndex, viewSize, touchPos);
		}
	}

	void HighscoresSection::OnExecuteSelected()
	{
	}

	void HighscoresSection::OnBackPressed()
	{
		if (_waitForInput) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_waitForInput = false;
			auto& selectedItem = _items[_selectedIndex];
			if (!selectedItem.Item->PlayerName.empty()) {
				SerializeToFile();
			}
			return;
		}

		ScrollableMenuSection::OnBackPressed();
	}

	void HighscoresSection::FillDefaultsIfEmpty()
	{
		auto& baseGame = _series[(std::int32_t)SeriesName::BaseGame];
		if (baseGame.Items.empty()) {
			baseGame.Items.push_back(HighscoreItem { "Dan"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 16, 600000, { 400, 40, 10, 0 } });
			baseGame.Items.push_back(HighscoreItem { "Tina"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 12, 500000, { 300, 35, 8, 0 } });
			baseGame.Items.push_back(HighscoreItem { "Paul"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 10, 400000, { 260, 30, 8, 0 } });
			baseGame.Items.push_back(HighscoreItem { "Monica"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 4, 300000, { 200, 25, 6, 0 } });
			baseGame.Items.push_back(HighscoreItem { "Eve"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 1, 200000, { 120, 20, 5, 0 } });
			baseGame.Items.push_back(HighscoreItem { "William"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 1, 100000, { 80, 16, 5, 0 } });
			baseGame.Items.push_back(HighscoreItem { "Scarlett"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 1, 50000, { 40, 10, 4, 0 } });
			baseGame.Items.push_back(HighscoreItem { "Thomas"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 1, 40000, { 20, 8, 4, 0 } });
			baseGame.Items.push_back(HighscoreItem { "James"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 1, 30000, { 12, 4, 2, 0 } });
			baseGame.Items.push_back(HighscoreItem { "Oliver"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 1, 14000, { 6, 2, 0, 0 } });
		}

		auto& sharewareDemo = _series[(std::int32_t)SeriesName::SharewareDemo];
		if (sharewareDemo.Items.empty()) {
			sharewareDemo.Items.push_back(HighscoreItem { "Monica"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 4, 50000, { 50, 1, 1, 0 } });
			sharewareDemo.Items.push_back(HighscoreItem { "Dan"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 4, 45000, { 45, 1, 1, 0 } });
			sharewareDemo.Items.push_back(HighscoreItem { "Eve"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 3, 40000, { 40, 1, 1, 0 } });
			sharewareDemo.Items.push_back(HighscoreItem { "Paul"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 2, 35000, { 35, 1, 1, 0 } });
			sharewareDemo.Items.push_back(HighscoreItem { "Tina"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 1, 30000, { 30, 1, 0, 0 } });
			sharewareDemo.Items.push_back(HighscoreItem { "Scarlett"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 1, 25000, { 25, 1, 0, 0 } });
			sharewareDemo.Items.push_back(HighscoreItem { "Matthew"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 1, 20000, { 20, 1, 0, 0 } });
			sharewareDemo.Items.push_back(HighscoreItem { "Andrew"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 1, 15000, { 12, 1, 0, 0 } });
			sharewareDemo.Items.push_back(HighscoreItem { "Violet"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 1, 10000, { 6, 0, 0, 0 } });
			sharewareDemo.Items.push_back(HighscoreItem { "Patrick"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 1, 5000, { 2, 0, 0, 0 } });
		}

		auto& theSecretFiles = _series[(std::int32_t)SeriesName::TheSecretFiles];
		if (theSecretFiles.Items.empty()) {
			theSecretFiles.Items.push_back(HighscoreItem { "Dan"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 10, 400000, { 350, 80, 15, 1 } });
			theSecretFiles.Items.push_back(HighscoreItem { "Tina"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 6, 350000, { 300, 50, 15, 0 } });
			theSecretFiles.Items.push_back(HighscoreItem { "Eve"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 4, 300000, { 240, 40, 12, 0 } });
			theSecretFiles.Items.push_back(HighscoreItem { "Monica"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 2, 250000, { 180, 35, 10, 0 } });
			theSecretFiles.Items.push_back(HighscoreItem { "Paul"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 1, 200000, { 140, 30, 8, 0 } });
			theSecretFiles.Items.push_back(HighscoreItem { "Christopher"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 1, 150000, { 100, 25, 6, 0 } });
			theSecretFiles.Items.push_back(HighscoreItem { "Andrew"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 1, 100000, { 60, 18, 4, 0 } });
			theSecretFiles.Items.push_back(HighscoreItem { "Victoria"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 1, 50000, { 30, 12, 2, 0 } });
			theSecretFiles.Items.push_back(HighscoreItem { "Thomas"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 1, 25000, { 16, 6, 0, 0 } });
			theSecretFiles.Items.push_back(HighscoreItem { "Alexander"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 1, 10000, { 8, 2, 0, 0 } });
		}

	}

	void HighscoresSection::DeserializeFromFile()
	{
		auto configDir = PreferencesCache::GetDirectory();
		auto s = fs::Open(fs::CombinePath(configDir, FileName), FileAccess::Read);
		if (*s) {
			std::uint64_t signature = s->ReadValue<std::uint64_t>();
			std::uint8_t fileType = s->ReadValue<std::uint8_t>();
			std::uint16_t version = s->ReadValue<std::uint16_t>();
			if (signature == 0x2095A59FF0BFBBEF && fileType == ContentResolver::HighscoresFile && version <= FileVersion) {
				DeflateStream uc(*s);
				std::uint32_t seriesCount = uc.ReadVariableUint32();
				if (seriesCount > (std::uint32_t)SeriesName::Count) {
					seriesCount = (std::uint32_t)SeriesName::Count;
				}

				for (std::uint32_t i = 0; i < seriesCount; i++) {
					std::uint32_t itemCount = uc.ReadVariableUint32();
					for (std::uint32_t j = 0; j < itemCount; j++) {
						HighscoreItem item;

						std::uint8_t playerNameLength = uc.ReadValue<std::uint8_t>();
						item.PlayerName = String(NoInit, playerNameLength);
						uc.Read(item.PlayerName.data(), playerNameLength);

						item.PlayerId = uc.ReadVariableUint64();
						item.Flags = (HighscoreFlags)uc.ReadVariableUint32();
						item.Type = (PlayerType)uc.ReadValue<std::uint8_t>();
						item.Difficulty = (GameDifficulty)uc.ReadValue<std::uint8_t>();
						item.Lives = uc.ReadVariableInt32();
						item.Score = uc.ReadVariableInt32();
						item.Gems[0] = uc.ReadVariableInt32();
						item.Gems[1] = uc.ReadVariableInt32();
						item.Gems[2] = uc.ReadVariableInt32();
						item.Gems[3] = uc.ReadVariableInt32();
						item.CreatedDate = uc.ReadVariableInt64();
						item.ElapsedMilliseconds = uc.ReadVariableUint64();

						_series[i].Items.emplace_back(std::move(item));
					}
				}
			}
		}
	}

	void HighscoresSection::SerializeToFile()
	{
		if (_notValidPos >= 0) {
			// Don't save list with invalid items
			return;
		}

		auto configDir = PreferencesCache::GetDirectory();
		auto s = fs::Open(fs::CombinePath(configDir, FileName), FileAccess::Write);
		if (*s) {
			s->WriteValue<std::uint64_t>(0x2095A59FF0BFBBEF);	// Signature
			s->WriteValue<std::uint8_t>(ContentResolver::HighscoresFile);
			s->WriteValue<std::uint16_t>(FileVersion);

			DeflateWriter co(*s);
			co.WriteVariableUint32((std::uint32_t)SeriesName::Count);

			for (std::uint32_t i = 0; i < (std::uint32_t)SeriesName::Count; i++) {
				auto& items = _series[i].Items;

				std::uint32_t itemCount = (std::uint32_t)items.size();
				if (itemCount > MaxItems) {
					itemCount = MaxItems;
				}
				co.WriteVariableUint32(itemCount);

				for (std::uint32_t j = 0; j < itemCount; j++) {
					auto& item = items[j];

					std::uint8_t playerNameLength = (std::uint8_t)item.PlayerName.size();
					co.WriteValue<std::uint8_t>(playerNameLength);
					co.Write(item.PlayerName.data(), playerNameLength);

					co.WriteVariableUint64(item.PlayerId);
					co.WriteVariableUint32((std::uint8_t)item.Flags);
					co.WriteValue<std::uint8_t>((std::uint8_t)item.Type);
					co.WriteValue<std::uint8_t>((std::uint8_t)item.Difficulty);
					co.WriteVariableInt32(item.Lives);
					co.WriteVariableInt32(item.Score);
					co.WriteVariableInt32(item.Gems[0]);
					co.WriteVariableInt32(item.Gems[1]);
					co.WriteVariableInt32(item.Gems[2]);
					co.WriteVariableInt32(item.Gems[3]);
					co.WriteVariableInt64(item.CreatedDate);
					co.WriteVariableUint64(item.ElapsedMilliseconds);
				}
			}
		}
	}

	void HighscoresSection::AddItemAndFocus(HighscoreItem&& item)
	{
		auto& items = _series[_selectedSeries].Items;
		auto nearestItem = std::lower_bound(items.begin(), items.end(), item, [](const HighscoreItem& a, const HighscoreItem& b) {
			return a.Score > b.Score;
		});

		auto* newItem = items.insert(nearestItem, std::move(item));
		std::int32_t index = (std::int32_t)(newItem - &items[0]);

		if ((item.Flags & HighscoreFlags::CheatsUsed) == HighscoreFlags::CheatsUsed && PreferencesCache::OverwriteEpisodeEnd != EpisodeEndOverwriteMode::Always) {
			_notValidPos = index + 1;
			_notValidSeries = _selectedSeries;
		} else {
			// Keep limited number of items
			while (items.size() > MaxItems && newItem != &items.back()) {
				items.pop_back();
			}

			_textCursor = newItem->PlayerName.size();
			_waitForInput = true;
		}

		RefreshList();
		_selectedIndex = index;
	}

	void HighscoresSection::RefreshList()
	{
		_selectedIndex = 0;
		_items.clear();

		auto& entries = _series[_selectedSeries].Items;
		for (auto& entry : entries) {
			_items.emplace_back(&entry);
		}
	}
}