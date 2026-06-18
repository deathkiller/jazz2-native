#include "HighscoresSection.h"
#include "MenuResources.h"
#include "../Font.h"
#include "../DiscordRpcClient.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"

#if defined(DEATH_TARGET_ANDROID)
#	include "../../../nCine/Backends/Android/AndroidApplication.h"
#	include "../../../nCine/Backends/Android/AndroidJniHelper.h"
#endif

#include <algorithm>
#include <cmath>
#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <IO/Compression/DeflateStream.h>
#include <Utf8.h>

using namespace Death::IO::Compression;
using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	// Row height matching the original section (ItemHeight * 5 / 8)
	static constexpr float RowHeight = 40 * 5 / 8;	// 25

	HighscoresSection::HighscoresSection()
		: _selectedSeries(0), _notValidPos(-1), _notValidSeries(-1), _pendingFocus(0), _textCursor(0), _carretAnim(0.0f),
			_animation(0.0f), _waitForInput(false), _list(nullptr)
#if defined(DEATH_TARGET_ANDROID)
			, _recalcVisibleBoundsTimeLeft(30.0f)
#endif
	{
		DeserializeFromFile();
		FillDefaultsIfEmpty();

#if defined(DEATH_TARGET_ANDROID)
		_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();
		_initialVisibleSize.X = _currentVisibleBounds.W;
		_initialVisibleSize.Y = _currentVisibleBounds.H;
#endif
	}

	HighscoresSection::HighscoresSection(std::int32_t seriesIndex, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, std::uint64_t elapsedMilliseconds, const PlayerCarryOver& itemToAdd)
		: HighscoresSection()
	{
		if (seriesIndex >= 0 && seriesIndex < (std::int32_t)SeriesName::Count) {
			_selectedSeries = seriesIndex;

			std::uint64_t userId = 0;
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			if (PreferencesCache::EnableDiscordIntegration && DiscordRpcClient::Get().IsSupported()) {
				userId = DiscordRpcClient::Get().GetUserId();
			}
#endif

			auto playerName = PreferencesCache::GetEffectivePlayerName();
			if (playerName.empty()) {
				playerName = "Me"_s;
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

	void HighscoresSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		RebuildList();
		if (_pendingFocus > 0) {
			_list->SetSelectedIndex(_pendingFocus, true);
		}
	}

	void HighscoresSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}
		if (_content != nullptr) {
			_content->OnUpdate(timeMult);
		}

		if (_waitForInput) {
#if defined(DEATH_TARGET_ANDROID)
			_recalcVisibleBoundsTimeLeft -= timeMult;
			if (_recalcVisibleBoundsTimeLeft <= 0.0f) {
				_recalcVisibleBoundsTimeLeft = 60.0f;
				_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();
			}
#endif
			std::int32_t row = GetSelectedRow();
			if (_root->ActionHit(PlayerAction::ChangeWeapon) && theApplication().CanShowScreenKeyboard()) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				theApplication().ToggleScreenKeyboard();
				RecalcLayoutForScreenKeyboard();
			} else if (_root->ActionHit(PlayerAction::Menu) || _root->ActionHit(PlayerAction::Run)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				FinishNameEntry();
			} else if (_root->ActionHit(PlayerAction::Fire)) {
				if (row >= 0 && !_series[_selectedSeries].Items[row].PlayerName.empty()) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					FinishNameEntry();
				}
			}

			_carretAnim += timeMult;
			return;
		}

		if (_root->ActionHit(PlayerAction::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		}

		bool navUp, navDown, navSound;
		UpdateNavigation(timeMult, navUp, navDown, navSound);

		std::int32_t row = GetSelectedRow();
		if (navUp) {
			if (!_series[_selectedSeries].Items.empty()) {
				if (navSound) _root->PlaySfx("MenuSelect"_s, 0.5f);
				_animation = 0.0f;
				std::int32_t count = (std::int32_t)_series[_selectedSeries].Items.size();
				std::int32_t newRow = (row > 0 ? row - 1 : count - 1);
				_list->SetSelectedIndex(newRow, false);
			}
		} else if (navDown) {
			if (!_series[_selectedSeries].Items.empty()) {
				if (navSound) _root->PlaySfx("MenuSelect"_s, 0.5f);
				_animation = 0.0f;
				std::int32_t count = (std::int32_t)_series[_selectedSeries].Items.size();
				std::int32_t newRow = (row < count - 1 ? row + 1 : 0);
				_list->SetSelectedIndex(newRow, false);
			}
		} else if (_root->ActionHit(PlayerAction::Left)) {
			SwitchSeries(-1);
		} else if (_root->ActionHit(PlayerAction::Right)) {
			SwitchSeries(1);
		}
	}

	void HighscoresSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + 31.0f;
		float bottomLine = contentBounds.Y + contentBounds.H - 42.0f;

		_root->DrawMenuFrame(centerX, topLine, bottomLine);

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

			_root->DrawStringShadow(_f("Highscores for \f[c:#d0705d]{}\f[/c]", episodeName), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
				Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
		}

		_root->DrawStringShadow("<"_s, charOffset, centerX - 70.0f - 100.0f, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Right, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
		_root->DrawStringShadow(">"_s, charOffset, centerX + 80.0f + 100.0f, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Right, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
	}

	void HighscoresSection::OnDrawOverlay(Canvas* canvas)
	{
		if (_waitForInput && theApplication().CanShowScreenKeyboard()) {
			auto contentBounds = _root->GetContentBounds();
			float titleY = contentBounds.Y - (canvas->ViewSize.Y >= 300 ? 30.0f : 12.0f) - 2.0f;

			_root->DrawElement(MenuGlow, 0, 72.0f, titleY - 2.0f, IMenuContainer::MainLayer - 20, Alignment::Center,
				Colorf(1.0f, 1.0f, 1.0f, 0.16f), 4.0f, 8.0f, true, true);

			_root->DrawElement(ShowKeyboard, -1, 72.0f, titleY - 2.0f + 2.0f, IMenuContainer::MainLayer - 10, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.2f));
			_root->DrawElement(ShowKeyboard, -1, 72.0f, titleY - 2.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf::White);

#if defined(DEATH_TARGET_ANDROID)
			if (_currentVisibleBounds.W < _initialVisibleSize.X || _currentVisibleBounds.H < _initialVisibleSize.Y) {
				Vector2i viewSize = _root->GetViewSize();
				if (_currentVisibleBounds.Y * viewSize.Y / _initialVisibleSize.Y < 32.0f) {
					_root->DrawSolid(0.0f, 0.0f, IMenuContainer::MainLayer - 10, Alignment::TopLeft, Vector2f(viewSize.X, viewSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.6f));

					std::int32_t row = GetSelectedRow();
					if (row >= 0) {
						auto& entry = _series[_selectedSeries].Items[row];

						std::int32_t charOffset = 0;
						_root->DrawStringShadow(entry.PlayerName, charOffset, 120.0f, titleY, IMenuContainer::MainLayer,
							Alignment::Left, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 1.0f);

						Vector2f textToCursorSize = _root->MeasureString(entry.PlayerName.prefix(_textCursor), 1.0f);
						_root->DrawSolid(120.0f + textToCursorSize.X + 1.0f, titleY - 1.0f, IMenuContainer::MainLayer + 10, Alignment::Left, Vector2f(1.0f, 14.0f),
							Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_carretAnim * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
					}
				}
			}
#endif
		}
	}

	void HighscoresSection::OnKeyPressed(const nCine::KeyboardEvent& event)
	{
		if (!_waitForInput) {
			return;
		}

		std::int32_t row = GetSelectedRow();
		if (row < 0) {
			return;
		}
		auto& playerName = _series[_selectedSeries].Items[row].PlayerName;

		switch (event.sym) {
			case Keys::Escape: {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				FinishNameEntry();
				break;
			}
			case Keys::Return: {
				if (!playerName.empty()) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					FinishNameEntry();
				}
				break;
			}
			case Keys::Backspace: {
				if (_textCursor > 0) {
					auto [_, prevPos] = Utf8::PrevChar(playerName, _textCursor);
					playerName = playerName.prefix(prevPos) + playerName.exceptPrefix(_textCursor);
					_textCursor = prevPos;
					_carretAnim = 0.0f;
				}
				break;
			}
			case Keys::Delete: {
				if (_textCursor < playerName.size()) {
					auto [_, nextPos] = Utf8::NextChar(playerName, _textCursor);
					playerName = playerName.prefix(_textCursor) + playerName.exceptPrefix(nextPos);
					_carretAnim = 0.0f;
				}
				break;
			}
			case Keys::Left: {
				if (_textCursor > 0) {
					auto [c, prevPos] = Utf8::PrevChar(playerName, _textCursor);
					_textCursor = prevPos;
					_carretAnim = 0.0f;
				}
				break;
			}
			case Keys::Right: {
				if (_textCursor < playerName.size()) {
					auto [c, nextPos] = Utf8::NextChar(playerName, _textCursor);
					_textCursor = nextPos;
					_carretAnim = 0.0f;
				}
				break;
			}
		}
	}

	void HighscoresSection::OnTextInput(const nCine::TextInputEvent& event)
	{
		if (!_waitForInput) {
			return;
		}

		std::int32_t row = GetSelectedRow();
		if (row < 0) {
			return;
		}
		auto& playerName = _series[_selectedSeries].Items[row].PlayerName;
		if (playerName.size() + event.length <= MaxPlayerNameLength) {
			playerName = playerName.prefix(_textCursor)
				+ StringView(event.text, event.length)
				+ playerName.exceptPrefix(_textCursor);
			_textCursor += event.length;
			_carretAnim = 0.0f;
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

	void HighscoresSection::DrawScoreRow(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset, bool isSelected, std::int32_t row)
	{
		auto& entry = _series[_selectedSeries].Items[row];
		float centerX = canvas->ViewSize.X * 0.5f;
		float nameX = centerX * 0.36f;
		float itemY = bounds.Y + bounds.H * 0.5f;
		char stringBuffer[64];

		if (isSelected) {
			root->DrawElement(MenuGlow, 0, centerX, itemY, IMenuContainer::MainLayer - 200, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.1f), 26.0f, 5.0f, true, true);
		}

		std::int32_t pos = row + 1;
		bool isNotValid = (_notValidPos == pos && _notValidSeries == _selectedSeries);
		if (_notValidPos > 0 && _notValidPos < pos && _notValidSeries == _selectedSeries) {
			pos--;
		}
		if (!isNotValid && pos <= MaxItems) {
			std::size_t length = formatInto(stringBuffer, "{}.", pos);
			root->DrawStringShadow({ stringBuffer, length }, charOffset, nameX - 16.0f, itemY, IMenuContainer::MainLayer - 100, Alignment::Right,
				(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f);
		}

		bool cheatsUsed = (entry.Flags & HighscoreFlags::CheatsUsed) == HighscoreFlags::CheatsUsed;

		Colorf nameColor;
		if (isSelected && _waitForInput) {
			nameColor = Colorf(0.62f, 0.44f, 0.34f, 0.5f);
		} else if (isSelected) {
			nameColor = (cheatsUsed ? Colorf(0.6f, 0.43f, 0.43f, 0.5f) : Colorf(0.48f, 0.48f, 0.48f, 0.5f));
		} else {
			nameColor = (cheatsUsed ? Colorf(0.48f, 0.38f, 0.34f, 0.5f) : Font::DefaultColor);
		}

		root->DrawStringShadow(entry.PlayerName, charOffset, nameX, itemY, IMenuContainer::MainLayer - 100, Alignment::Left, nameColor, 0.8f);

		if (entry.Lives <= 0) {
			Vector2f nameSize = root->MeasureString(entry.PlayerName, 0.8f);
			root->DrawElement(RestInPeace, -1, nameX + nameSize.X + 12.0f, itemY - 2.0f, IMenuContainer::MainLayer - 100, Alignment::Left, Colorf::White, 1.0f, 1.0f);
		}

		u32tos(entry.Score, stringBuffer);
		root->DrawStringShadow(stringBuffer, charOffset, centerX * 1.06f, itemY, IMenuContainer::MainLayer - 100, Alignment::Right,
			(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.92f);

		root->DrawElement(PickupGemRed, -1, centerX * 1.18f - 6.0f, itemY, IMenuContainer::MainLayer - 100, Alignment::Right, Colorf(1.0f, 1.0f, 1.0f, 0.8f), 0.46f, 0.46f);

		std::size_t length = formatInto(stringBuffer, "{} · {} · {} · {}", entry.Gems[0], entry.Gems[1], entry.Gems[2], entry.Gems[3]);
		root->DrawStringShadow({ stringBuffer, length }, charOffset, centerX * 1.18f, itemY, IMenuContainer::MainLayer - 100, Alignment::Left,
			(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f);

		std::int64_t elapsedSeconds = (entry.ElapsedMilliseconds / 1000);
		if (elapsedSeconds > 0) {
			root->DrawElement(PickupStopwatch, -1, centerX * 1.58f - 6.0f, itemY, IMenuContainer::MainLayer - 100, Alignment::Right, Colorf::White, 0.55f, 0.55f);

			std::int32_t elapsedHours = (std::int32_t)(elapsedSeconds / 3600);
			std::int32_t elapsedMinutes = (std::int32_t)(elapsedSeconds / 60);
			elapsedSeconds -= (elapsedMinutes * 60);
			elapsedMinutes -= (elapsedHours * 60);
			std::size_t length2 = formatInto(stringBuffer, "{}:{:.2}:{:.2}", elapsedHours, elapsedMinutes, elapsedSeconds);
			root->DrawStringShadow({ stringBuffer, length2 }, charOffset, centerX * 1.72f, itemY, IMenuContainer::MainLayer - 100, Alignment::Right,
				(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		}

		if (isSelected && _waitForInput) {
			Vector2f textToCursorSize = root->MeasureString(entry.PlayerName.prefix(_textCursor), 0.8f);
			root->DrawSolid(nameX + textToCursorSize.X + 1.0f, itemY - 1.0f, IMenuContainer::MainLayer - 80, Alignment::Center, Vector2f(1.0f, 12.0f),
				Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_carretAnim * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
		}
	}

	void HighscoresSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		if (_waitForInput) {
			// While entering a name, only the on-screen keyboard button and the top bar (which commits) respond
			if (event.type == TouchEventType::Down) {
				std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					float x = event.pointers[pointerIndex].x;
					float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
					if (x < 0.2f && y < 80.0f && theApplication().CanShowScreenKeyboard()) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						theApplication().ShowScreenKeyboard();
						RecalcLayoutForScreenKeyboard();
						return;
					}
					if (y < 80.0f) {
						OnBackPressed();
						return;
					}
				}
			}
			return;
		}

		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
				if (y >= 80.0f) {
					Recti contentBounds = _root->GetContentBounds();
					float topLine = contentBounds.Y + 31.0f;
					if (std::abs(x - 0.5f) > (y > topLine ? 0.35f : 0.1f)) {
						SwitchSeries(x < 0.5f ? -1 : 1);
						return;
					}
				}
			}
		}

		WidgetSection::OnTouchEvent(event, viewSize);
	}

	void HighscoresSection::OnBackPressed()
	{
		if (_waitForInput) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			FinishNameEntry();
			return;
		}

		WidgetSection::OnBackPressed();
	}

	void HighscoresSection::SwitchSeries(std::int32_t direction)
	{
		_root->PlaySfx("MenuSelect"_s, 0.5f);
		_animation = 0.0f;
		_selectedSeries += direction;
		if (_selectedSeries < 0) {
			_selectedSeries = (std::int32_t)SeriesName::Count - 1;
		} else if (_selectedSeries >= (std::int32_t)SeriesName::Count) {
			_selectedSeries = 0;
		}
		RebuildList();
	}

	void HighscoresSection::FinishNameEntry()
	{
		_waitForInput = false;
		std::int32_t row = GetSelectedRow();
		if (row >= 0 && !_series[_selectedSeries].Items[row].PlayerName.empty()) {
			SerializeToFile();
		}
		theApplication().HideScreenKeyboard();
		RecalcLayoutForScreenKeyboard();
	}

	std::int32_t HighscoresSection::GetSelectedRow() const
	{
		return (_list != nullptr ? _list->GetSelectedIndex() : -1);
	}

	void HighscoresSection::RebuildList()
	{
		auto list = std::make_unique<ScrollView>();
		auto& entries = _series[_selectedSeries].Items;
		for (std::int32_t i = 0; i < (std::int32_t)entries.size(); i++) {
			std::int32_t row = i;
			auto* item = list->Add<CanvasWidget>(RowHeight);
			item->OnDrawContent = [this, row](IMenuContainer* r, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset, bool selected, float animation) {
				DrawScoreRow(r, canvas, bounds, charOffset, selected, row);
			};
		}
		_list = list.get();

		SetContent(std::move(list));
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
			std::uint64_t signature = s->ReadValueAsLE<std::uint64_t>();
			std::uint8_t fileType = s->ReadValue<std::uint8_t>();
			std::uint16_t version = s->ReadValueAsLE<std::uint16_t>();
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
			s->WriteValueAsLE<std::uint64_t>(0x2095A59FF0BFBBEF);	// Signature
			s->WriteValue<std::uint8_t>(ContentResolver::HighscoresFile);
			s->WriteValueAsLE<std::uint16_t>(FileVersion);

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

		if ((newItem->Flags & HighscoreFlags::CheatsUsed) == HighscoreFlags::CheatsUsed && PreferencesCache::OverwriteEpisodeEnd != EpisodeEndOverwriteMode::Always) {
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

		_pendingFocus = index;
	}

	void HighscoresSection::RecalcLayoutForScreenKeyboard()
	{
#if defined(DEATH_TARGET_ANDROID)
		_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();

		if (_recalcVisibleBoundsTimeLeft > 30.0f) {
			_recalcVisibleBoundsTimeLeft = 30.0f;
		}
#endif
	}
}
