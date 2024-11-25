#include "HighscoresSection.h"
#include "MenuResources.h"

#include "../../../nCine/Application.h"

#include <Utf8.h>
#include <Containers/StringConcatenable.h>
#include <IO/DeflateStream.h>

#if defined(DEATH_TARGET_SWITCH)
#	include <switch.h>
#elif defined(DEATH_TARGET_UNIX)
#	include <pwd.h>
#endif

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	HighscoresSection::HighscoresSection()
		: _selectedSeries(0), _textCursor(0), _carretAnim(0.0f), _pressedActions(UINT32_MAX), _waitForInput(false)
	{
		DeserializeFromFile();
		FillDefaultsIfEmpty();
		RefreshList();
	}

	HighscoresSection::HighscoresSection(std::int32_t seriesIndex, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, const PlayerCarryOver& itemToAdd)
		: HighscoresSection()
	{
		if (seriesIndex >= 0 && seriesIndex < (std::int32_t)SeriesName::Count) {
			_selectedSeries = seriesIndex;
			_waitForInput = true;

			String name = TryGetDefaultName();
			if (name.size() > MaxNameLength) {
				auto [_, prevChar] = Utf8::PrevChar(name, MaxNameLength);
				name = name.prefix(prevChar);
			}

			HighscoreFlags flags = HighscoreFlags::None;
			if (isReforged) flags |= HighscoreFlags::IsReforged;
			if (cheatsUsed) flags |= HighscoreFlags::CheatsUsed;

			// TODO: PlayerId is unused
			// TODO: ElapsedMilliseconds is unused
			AddItemAndFocus(HighscoreItem { std::move(name), 0, flags, itemToAdd.Type, difficulty, itemToAdd.Lives, itemToAdd.Score,
				{ itemToAdd.Gems[0], itemToAdd.Gems[1], itemToAdd.Gems[2], itemToAdd.Gems[3] }, DateTime::UtcNow().ToUnixMilliseconds() });
		}
	}

	void HighscoresSection::OnUpdate(float timeMult)
	{
		// Move the variable to stack to fix leaving the section
		bool waitingForInput = _waitForInput;

		ScrollableMenuSection::OnUpdate(timeMult);

		if (waitingForInput) {
			UpdatePressedActions();

			if (_root->ActionHit(PlayerActions::Menu) || _root->ActionHit(PlayerActions::Run) || IsTextActionHit(TextAction::Escape)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_waitForInput = false;
				auto& selectedItem = _items[_selectedIndex];
				if (!selectedItem.Item->PlayerName.empty()) {
					SerializeToFile();
				}
			} else if (_root->ActionHit(PlayerActions::Fire) || IsTextActionHit(TextAction::Enter)) {
				auto& selectedItem = _items[_selectedIndex];
				if (!selectedItem.Item->PlayerName.empty()) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_waitForInput = false;
					SerializeToFile();
				}
			} else if (_root->ActionHit(PlayerActions::ChangeWeapon) || IsTextActionHit(TextAction::Backspace)) {
				auto& selectedItem = _items[_selectedIndex];
				if (_textCursor > 0) {
					auto [_, prevPos] = Utf8::PrevChar(selectedItem.Item->PlayerName, _textCursor);
					selectedItem.Item->PlayerName = selectedItem.Item->PlayerName.prefix(prevPos) + selectedItem.Item->PlayerName.exceptPrefix(_textCursor);
					_textCursor = prevPos;
					_carretAnim = 0.0f;
				}
			} else if (IsTextActionHit(TextAction::Delete)) {
				auto& selectedItem = _items[_selectedIndex];
				if (_textCursor < selectedItem.Item->PlayerName.size()) {
					auto [_, nextPos] = Utf8::NextChar(selectedItem.Item->PlayerName, _textCursor);
					selectedItem.Item->PlayerName = selectedItem.Item->PlayerName.prefix(_textCursor) + selectedItem.Item->PlayerName.exceptPrefix(nextPos);
					_carretAnim = 0.0f;
				}
			} else if (IsTextActionHit(TextAction::Left)) {
				auto& selectedItem = _items[_selectedIndex];
				if (_textCursor > 0) {
					auto [c, prevPos] = Utf8::PrevChar(selectedItem.Item->PlayerName, _textCursor);
					_textCursor = prevPos;
					_carretAnim = 0.0f;
				}
			} else if (IsTextActionHit(TextAction::Right)) {
				auto& selectedItem = _items[_selectedIndex];
				if (_textCursor < selectedItem.Item->PlayerName.size()) {
					auto [ c, nextPos ] = Utf8::NextChar(selectedItem.Item->PlayerName, _textCursor);
					_textCursor = nextPos;
					_carretAnim = 0.0f;
				}
			}

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

		StringView episodeName;
		switch ((SeriesName)_selectedSeries) {
			case SeriesName::BaseGame: episodeName = _("Base Game"); break;
			case SeriesName::SharewareDemo: episodeName = "Shareware Demo"_s; break;
			case SeriesName::TheSecretFiles: episodeName = "The Secret Files"_s; break;
		}

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(_f("Highscores for \f[c:#d0705d]%s\f[/c]", episodeName.data()), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		_root->DrawStringShadow("<"_s, charOffset, centerX - 70.0f - 100.0f, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Right, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
		_root->DrawStringShadow(">"_s, charOffset, centerX + 80.0f + 100.0f, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Right, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
	}

	void HighscoresSection::OnTextInput(const nCine::TextInputEvent& event)
	{
		if (_waitForInput) {
			auto& selectedItem = _items[_selectedIndex];
			if (selectedItem.Item->PlayerName.size() + event.length <= MaxNameLength) {
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
		float nameX = centerX * 0.3f;
		char stringBuffer[16];

		if (isSelected) {
			_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer - 200, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.1f), 26.0f, 5.0f, true, true);
		}

		formatString(stringBuffer, sizeof(stringBuffer), "%i.", (std::int32_t)(&item - &_items[0] + 1));
		_root->DrawStringShadow(stringBuffer, charOffset, nameX - 16.0f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Right,
			(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f);

		_root->DrawStringShadow(item.Item->PlayerName, charOffset, nameX, item.Y, IMenuContainer::MainLayer - 100, Alignment::Left,
			isSelected && _waitForInput ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : (isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f);

		u32tos(item.Item->Score, stringBuffer);
		_root->DrawStringShadow(stringBuffer, charOffset, centerX * 0.98f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Right,
			(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f);

		_root->DrawElement(PickupGemRed, -1, centerX * 1.1f - 2.0f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Right, Colorf(1.0f, 1.0f, 1.0f, 0.8f), 0.46f, 0.46f);

		u32tos(item.Item->Gems[0], stringBuffer);
		_root->DrawStringShadow(stringBuffer, charOffset, centerX * 1.1f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Left,
			(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f);

		_root->DrawElement(PickupGemGreen, -1, centerX * 1.3f - 2.0f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Right, Colorf(1.0f, 1.0f, 1.0f, 0.8f), 0.46f, 0.46f);

		u32tos(item.Item->Gems[1], stringBuffer);
		_root->DrawStringShadow(stringBuffer, charOffset, centerX * 1.3f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Left,
			(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f);

		_root->DrawElement(PickupGemBlue, -1, centerX * 1.5f - 2.0f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Right, Colorf(1.0f, 1.0f, 1.0f, 0.8f), 0.46f, 0.46f);

		u32tos(item.Item->Gems[2], stringBuffer);
		_root->DrawStringShadow(stringBuffer, charOffset, centerX * 1.5f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Left,
			(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f);

		_root->DrawElement(PickupGemPurple, -1, centerX * 1.7f - 2.0f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Right, Colorf(1.0f, 1.0f, 1.0f, 0.8f), 0.46f, 0.46f);

		u32tos(item.Item->Gems[3], stringBuffer);
		_root->DrawStringShadow(stringBuffer, charOffset, centerX * 1.7f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Left,
			(isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f);

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

		if (_root->ActionHit(PlayerActions::Menu)) {
			OnBackPressed();
		} else if (_root->ActionHit(PlayerActions::Fire)) {
			OnExecuteSelected();
		} else if (_root->ActionHit(PlayerActions::Up)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex > 0) {
				_selectedIndex--;
			} else {
				_selectedIndex = (std::int32_t)(_items.size() - 1);
			}
			EnsureVisibleSelected();
			OnSelectionChanged(_items[_selectedIndex]);
		} else if (_root->ActionHit(PlayerActions::Down)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex < (std::int32_t)(_items.size() - 1)) {
				_selectedIndex++;
			} else {
				_selectedIndex = 0;
			}
			EnsureVisibleSelected();
			OnSelectionChanged(_items[_selectedIndex]);
		} else if (_root->ActionHit(PlayerActions::Left)) {
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
		} else if (_root->ActionHit(PlayerActions::Right)) {
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

	void HighscoresSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
				if (y >= 80.0f && std::abs(x - 0.5f) > 0.35f) {
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

		ScrollableMenuSection::OnTouchEvent(event, viewSize);
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
			baseGame.Items.emplace_back(HighscoreItem { "Dan"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 16, 600000, { 400, 40, 10, 0 } });
			baseGame.Items.emplace_back(HighscoreItem { "Tina"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 12, 500000, { 300, 35, 8, 0 } });
			baseGame.Items.emplace_back(HighscoreItem { "Paul"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 10, 400000, { 260, 30, 8, 0 } });
			baseGame.Items.emplace_back(HighscoreItem { "Monica"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 4, 300000, { 200, 25, 6, 0 } });
			baseGame.Items.emplace_back(HighscoreItem { "Eve"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 0, 200000, { 120, 20, 5, 0 } });
			baseGame.Items.emplace_back(HighscoreItem { "William"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 0, 100000, { 80, 16, 5, 0 } });
			baseGame.Items.emplace_back(HighscoreItem { "Scarlett"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 0, 50000, { 40, 10, 4, 0 } });
			baseGame.Items.emplace_back(HighscoreItem { "Thomas"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 0, 40000, { 20, 8, 4, 0 } });
			baseGame.Items.emplace_back(HighscoreItem { "James"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 0, 30000, { 12, 4, 2, 0 } });
			baseGame.Items.emplace_back(HighscoreItem { "Oliver"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 0, 20000, { 6, 2, 0, 0 } });
		}

		auto& sharewareDemo = _series[(std::int32_t)SeriesName::SharewareDemo];
		if (sharewareDemo.Items.empty()) {
			sharewareDemo.Items.emplace_back(HighscoreItem { "Monica"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 4, 50000, { 50, 1, 1, 0 } });
			sharewareDemo.Items.emplace_back(HighscoreItem { "Dan"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 4, 45000, { 45, 1, 1, 0 } });
			sharewareDemo.Items.emplace_back(HighscoreItem { "Eve"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 3, 40000, { 40, 1, 1, 0 } });
			sharewareDemo.Items.emplace_back(HighscoreItem { "Paul"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 2, 35000, { 35, 1, 1, 0 } });
			sharewareDemo.Items.emplace_back(HighscoreItem { "Tina"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 1, 30000, { 30, 1, 0, 0 } });
			sharewareDemo.Items.emplace_back(HighscoreItem { "Scarlett"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 1, 25000, { 25, 1, 0, 0 } });
			sharewareDemo.Items.emplace_back(HighscoreItem { "Matthew"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 0, 20000, { 20, 1, 0, 0 } });
			sharewareDemo.Items.emplace_back(HighscoreItem { "Andrew"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 0, 15000, { 12, 1, 0, 0 } });
			sharewareDemo.Items.emplace_back(HighscoreItem { "Violet"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 0, 10000, { 6, 0, 0, 0 } });
			sharewareDemo.Items.emplace_back(HighscoreItem { "Patrick"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 0, 5000, { 2, 0, 0, 0 } });
		}

		auto& theSecretFiles = _series[(std::int32_t)SeriesName::TheSecretFiles];
		if (theSecretFiles.Items.empty()) {
			theSecretFiles.Items.emplace_back(HighscoreItem { "Dan"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 10, 400000, { 350, 80, 15, 1 } });
			theSecretFiles.Items.emplace_back(HighscoreItem { "Tina"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 6, 350000, { 300, 50, 15, 0 } });
			theSecretFiles.Items.emplace_back(HighscoreItem { "Eve"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 4, 300000, { 240, 40, 12, 0 } });
			theSecretFiles.Items.emplace_back(HighscoreItem { "Monica"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 2, 250000, { 180, 35, 10, 0 } });
			theSecretFiles.Items.emplace_back(HighscoreItem { "Paul"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 1, 200000, { 140, 30, 8, 0 } });
			theSecretFiles.Items.emplace_back(HighscoreItem { "Christopher"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 1, 150000, { 100, 25, 6, 0 } });
			theSecretFiles.Items.emplace_back(HighscoreItem { "Andrew"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 0, 100000, { 60, 18, 4, 0 } });
			theSecretFiles.Items.emplace_back(HighscoreItem { "Victoria"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Lori, GameDifficulty::Normal, 0, 50000, { 30, 12, 2, 0 } });
			theSecretFiles.Items.emplace_back(HighscoreItem { "Thomas"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Spaz, GameDifficulty::Normal, 0, 25000, { 16, 6, 0, 0 } });
			theSecretFiles.Items.emplace_back(HighscoreItem { "Alexander"_s, UINT64_MAX, HighscoreFlags::IsDefault, PlayerType::Jazz, GameDifficulty::Normal, 0, 10000, { 8, 2, 0, 0 } });
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

		// Keep limited number of items
		while (items.size() > MaxItems && newItem != &items.back()) {
			items.pop_back();
		}

		RefreshList();

		_selectedIndex = index;
		_textCursor = newItem->PlayerName.size();
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

	void HighscoresSection::UpdatePressedActions()
	{
		auto& input = theApplication().GetInputManager();
		auto& keyState = input.keyboardState();

		_pressedActions = (_pressedActions << 16);

		const KeySym KeyToTextAction[] = { KeySym::LEFT, KeySym::RIGHT, KeySym::BACKSPACE, KeySym::Delete, KeySym::RETURN, KeySym::ESCAPE };

		for (std::int32_t i = 0; i < (std::int32_t)arraySize(KeyToTextAction); i++) {
			if (keyState.isKeyDown(KeyToTextAction[i])) {
				_pressedActions |= (1 << i);
			}
		}
	}

	bool HighscoresSection::IsTextActionHit(TextAction action)
	{
		return ((_pressedActions & ((1 << (std::int32_t)action) | (1 << (16 + (std::int32_t)action)))) == (1 << (std::int32_t)action));
	}

	String HighscoresSection::TryGetDefaultName()
	{
		// TODO: Discord integration

#if defined(DEATH_TARGET_ANDROID)
		// TODO: Get user name on Android
#elif defined(DEATH_TARGET_SWITCH)
		AccountUid uid;
		AccountProfile profile;
		if (R_SUCCEEDED(accountInitialize(AccountServiceType_Application))) {
			String userName;
			if (R_SUCCEEDED(accountGetPreselectedUser(&uid)) && R_SUCCEEDED(accountGetProfile(&profile, uid))) {
				AccountProfileBase profileBase;
				AccountUserData userData;
				accountProfileGet(&profile, &userData, &profileBase);
				String userName = profileBase.nickname;
				accountProfileClose(&profile);
			}
			accountExit();

			if (!userName.empty()) {
				return userName;
			}
		}
#elif defined(DEATH_TARGET_WINDOWS_RT)
		// TODO: Get user name on UWP/Xbox
#elif defined(DEATH_TARGET_WINDOWS)
		wchar_t userName[64];
		DWORD userNameLength = (DWORD)arraySize(userName);
		if (::GetUserName(userName, &userNameLength) && userNameLength > 0) {
			return Utf8::FromUtf16(userName);
		}
#elif defined(DEATH_TARGET_APPLE)
		StringView userName = ::getenv("USER");
		if (!userName.empty()) {
			return userName;
		}
#elif defined(DEATH_TARGET_UNIX)
		struct passwd* pw = ::getpwuid(::getuid());
		if (pw != nullptr) {
			StringView userName = pw->pw_gecos;	// Display name
			if (!userName.empty()) {
				return userName;
			}
			userName = pw->pw_name;	// Plain name
			if (!userName.empty()) {
				return userName;
			}
		}

		StringView userName = ::getenv("USER");
		if (!userName.empty()) {
			return userName;
		}
#endif
		return "Me"_s;
	}
}