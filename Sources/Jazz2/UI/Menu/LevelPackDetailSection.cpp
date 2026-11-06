#include "LevelPackDetailSection.h"

#if defined(WITH_LEVEL_PACKS)

#include "LevelPacksSection.h"
#include "StartGameOptionsSection.h"
#include "MenuResources.h"
#include "../Font.h"
#include "../../LevelPacks/LevelPackLibrary.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/I18n.h"

#if defined(WITH_MULTIPLAYER)
#	include "CreateServerOptionsSection.h"
#	include "CreateLocalGameOptionsSection.h"
#endif

#include <Containers/StringConcatenable.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	LevelPackDetailSection::LevelPackDetailSection(std::shared_ptr<const LevelPacks::LevelPackManifest> pack, EpisodeSelectMode mode, bool privateServer)
		: _pack(Death::move(pack)), _mode(mode), _privateServer(privateServer), _confirmRemove(false), _selectedIndex(0), _animation(0.0f),
			_y(0.0f), _height(0.0f), _availableHeight(0.0f), _noiseCooldown(0.0f), _pressedCount(0), _touchTime(0.0f)
	{
		for (const auto& episode : _pack->Episodes) {
			ItemData& item = _items.emplace_back();
			item.Type = ItemType::Episode;
			item.LevelName = episode.Name + "/"_s + episode.FirstLevel;
			item.DisplayName = episode.DisplayName;
		}
		for (const auto& level : _pack->Levels) {
			ItemData& item = _items.emplace_back();
			item.Type = ItemType::Level;
			item.LevelName = level.Name;
			item.DisplayName = level.DisplayName;
		}
		ItemData& remove = _items.emplace_back();
		remove.Type = ItemType::Remove;

		_preview = LevelPacksSection::LoadPreview(*_pack, _previewSize);
	}

	Recti LevelPackDetailSection::GetClipRectangle(const Recti& contentBounds)
	{
		return Recti(contentBounds.X, contentBounds.Y + TopLine - 1, contentBounds.W, contentBounds.H - TopLine - BottomLine + 2);
	}

	void LevelPackDetailSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);
		_animation = 0.0f;
	}

	void LevelPackDetailSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}
		if (_noiseCooldown > 0.0f) {
			_noiseCooldown -= timeMult;
		}
		_touchTime += timeMult;

		if (_root->ActionHit(PlayerAction::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		}
		if (_root->ActionHit(PlayerAction::Fire)) {
			ExecuteSelected();
			return;
		}

		std::int32_t offset = 0;
		PlayerAction action = PlayerAction::None;
		if (_root->ActionPressed(PlayerAction::Up)) {
			offset = -1;
			action = PlayerAction::Up;
		} else if (_root->ActionPressed(PlayerAction::Down)) {
			offset = 1;
			action = PlayerAction::Down;
		}
		if (offset != 0) {
			if (_animation >= 1.0f - (_pressedCount * 0.096f) || _root->ActionHit(action)) {
				MoveSelection(offset);
				_pressedCount = std::min<std::int32_t>(_pressedCount + 6, 10);
			}
		} else {
			_pressedCount = 0;
		}
	}

	void LevelPackDetailSection::MoveSelection(std::int32_t offset)
	{
		std::int32_t count = std::int32_t(_items.size());
		if (count <= 1) {
			return;
		}
		if (_noiseCooldown <= 0.0f) {
			_noiseCooldown = 10.0f;
			_root->PlaySfx("MenuSelect"_s, 0.5f);
		}
		_animation = 0.0f;
		_confirmRemove = false;
		_selectedIndex = (_selectedIndex + offset + count) % count;
		EnsureVisibleSelected();
	}

	void LevelPackDetailSection::EnsureVisibleSelected()
	{
		Recti contentBounds = _root->GetContentBounds();
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;
		if (_selectedIndex == 0) {
			// The details of the pack are above the first item
			_y = 0.0f;
			return;
		}
		float y = _items[_selectedIndex].Y;
		if (y < topLine + ItemHeight) {
			_y += (topLine + ItemHeight - y);
		} else if (y > bottomLine - ItemHeight) {
			_y -= (y - (bottomLine - ItemHeight));
		}
	}

	void LevelPackDetailSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawMenuFrame(centerX, topLine, bottomLine);

		std::int32_t charOffset = 0;
		_root->DrawMenuTitle(charOffset, LevelPacksSection::FitText(_root, _pack->Title, 0.9f, contentBounds.W - 40.0f), centerX, topLine);
	}

	void LevelPackDetailSection::OnDrawClipped(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;
		std::int32_t charOffset = 0;

		_availableHeight = (bottomLine - topLine);
		_y = (_availableHeight < _height ? std::clamp(_y, _availableHeight - _height, 0.0f) : 0.0f);

		// The preview on the left, what the pack is on the right of it
		float left = contentBounds.X + (contentBounds.W >= 460 ? contentBounds.W * 0.14f : 16.0f);
		float headerY = topLine + 10.0f + _y;
		constexpr float PreviewWidth = 112.0f, PreviewHeight = 63.0f;
		float textX = left;
		if (_preview != nullptr && _previewSize.X > 0 && _previewSize.Y > 0 && contentBounds.W >= 320) {
			float scale = std::min(PreviewWidth / _previewSize.X, PreviewHeight / _previewSize.Y);
			Vector2f size(std::round(_previewSize.X * scale), std::round(_previewSize.Y * scale));
			Colorf borderColor(0.0f, 0.0f, 0.0f, 1.0f);
			_root->DrawSolid(left - 1.0f, headerY - 1.0f, IMenuContainer::MainLayer - 105, Alignment::TopLeft, Vector2f(size.X + 2.0f, size.Y + 2.0f), borderColor);
			_root->DrawTexture(*_preview, left, headerY, IMenuContainer::MainLayer - 100, Alignment::TopLeft, size, Colorf::White);
			textX = left + PreviewWidth + 14.0f;
		}

		float textWidth = contentBounds.X + contentBounds.W - 16.0f - textX;
		float lineY = headerY + 6.0f;
		if (!_pack->Authors.empty()) {
			// TRANSLATORS: Authors of a level pack
			_root->DrawStringShadow(LevelPacksSection::FitText(_root, _f("By {}", _pack->Authors), 0.8f, textWidth), charOffset, textX, lineY,
				IMenuContainer::FontLayer, Alignment::Left, Font::DefaultColor, 0.8f);
			lineY += 14.0f;
		}
		std::int32_t levelCount = std::int32_t(_pack->Levels.size());
		std::int32_t episodeCount = std::int32_t(_pack->Episodes.size());
		// TRANSLATORS: Details of a level pack
		String details = _fn("{} level", "{} levels", levelCount, levelCount);
		if (episodeCount > 0) {
			// TRANSLATORS: Details of a level pack
			details = details + ", "_s + _fn("{} episode", "{} episodes", episodeCount, episodeCount);
		}
		details = details + ", "_s + _f("{:.1f} MB", double(_pack->GetTotalSize()) / (1024.0 * 1024.0));
		_root->DrawStringShadow(LevelPacksSection::FitText(_root, details, 0.7f, textWidth), charOffset, textX, lineY,
			IMenuContainer::FontLayer, Alignment::Left, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.7f);
		lineY += 13.0f;
		if (!_pack->MissingAssets.empty()) {
			// TRANSLATORS: Warning in the details of a level pack, some files its levels need aren't there
			String missing = _f("Missing: {}", _pack->MissingAssets[0]);
			if (_pack->MissingAssets.size() > 1) {
				missing = _f("Missing: {} and {} more", _pack->MissingAssets[0], std::int32_t(_pack->MissingAssets.size() - 1));
			}
			_root->DrawStringShadow(LevelPacksSection::FitText(_root, missing, 0.7f, textWidth), charOffset, textX, lineY,
				IMenuContainer::FontLayer, Alignment::Left, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.7f);
			lineY += 13.0f;
		}
		if (!_pack->Description.empty()) {
			_root->DrawStringShadow(LevelPacksSection::FitText(_root, _pack->Description, 0.7f, textWidth), charOffset, textX, lineY,
				IMenuContainer::FontLayer, Alignment::Left, Font::DefaultColor, 0.7f);
		}

		// Episodes, then levels, then the removal of the pack
		float column1 = contentBounds.X + (contentBounds.W >= 460 ? (contentBounds.W * 0.2f) : 20.0f);
		float column2 = contentBounds.X + (contentBounds.W >= 460 ? (contentBounds.W * 0.56f) : (contentBounds.W * 0.5f));
		float y = topLine + HeaderHeight + _y;
		for (std::int32_t i = 0; i < std::int32_t(_items.size()); i++) {
			ItemData& item = _items[i];
			if (i > 0 && item.Type != _items[i - 1].Type) {
				y += ItemHeight * 0.5f;
			}
			item.Y = y;

			if (y > topLine - ItemHeight && y < bottomLine + ItemHeight) {
				bool isSelected = (i == _selectedIndex);
				StringView label;
				String text;
				switch (item.Type) {
					case ItemType::Episode:
						// TRANSLATORS: Item in the details of a level pack that starts an episode of it
						text = _f("Play episode \"{}\"", item.DisplayName);
						label = text;
						break;
					case ItemType::Level:
						label = item.DisplayName;
						break;
					case ItemType::Remove:
						label = (_confirmRemove
							// TRANSLATORS: Item in the details of a level pack after it was selected once, to confirm the removal
							? _("Press again to remove the pack")
							// TRANSLATORS: Item in the details of a level pack that removes it
							: _("Remove pack"));
						break;
				}

				if (item.Type == ItemType::Level) {
					String fitted = LevelPacksSection::FitText(_root, label, 0.7f, column2 - column1 - 12.0f);
					if (isSelected) {
						_root->DrawElement(MenuGlow, 0, centerX, y, IMenuContainer::MainLayer - 200, Alignment::Center,
							Colorf(1.0f, 1.0f, 1.0f, 0.2f), 26.0f, 3.0f, true, true);
						_root->DrawStringShadow(fitted, charOffset, column1, y, IMenuContainer::FontLayer + 10,
							Alignment::Left, Font::RandomColor, 0.7f + 0.12f * Easing::OutElastic(_animation), 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
					} else {
						_root->DrawStringShadow(fitted, charOffset, column1, y, IMenuContainer::FontLayer,
							Alignment::Left, Font::DefaultColor, 0.7f);
					}
					// Every level is of this pack, only its own name is shown
					StringView fileName = StringView(item.LevelName).partition('/')[2];
					_root->DrawStringShadow(LevelPacksSection::FitText(_root, fileName, 0.6f, contentBounds.X + contentBounds.W - 24.0f - column2),
						charOffset, column2, y, IMenuContainer::FontLayer - 2, Alignment::Left, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.6f);
				} else {
					Colorf color = (item.Type == ItemType::Remove ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : Font::DefaultColor);
					if (isSelected) {
						_root->DrawElement(MenuGlow, 0, centerX, y, IMenuContainer::MainLayer - 200, Alignment::Center,
							Colorf(1.0f, 1.0f, 1.0f, 0.2f), 26.0f, 3.0f, true, true);
						_root->DrawStringShadow(LevelPacksSection::FitText(_root, label, 0.8f, contentBounds.W - 40.0f), charOffset, centerX, y,
							IMenuContainer::FontLayer + 10, Alignment::Center, item.Type == ItemType::Remove ? color : Font::RandomColor,
							0.8f + 0.1f * Easing::OutElastic(_animation), 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
					} else {
						_root->DrawStringShadow(LevelPacksSection::FitText(_root, label, 0.8f, contentBounds.W - 40.0f), charOffset, centerX, y,
							IMenuContainer::FontLayer, Alignment::Center, color, 0.8f);
					}
				}
			}
			y += ItemHeight;
		}
		_height = y - (topLine + _y);

		if (_y < 0.0f) {
			_root->DrawMenuEdgeGlow(centerX, topLine);
		}
		if (_availableHeight - _y < _height) {
			_root->DrawMenuEdgeGlow(centerX, bottomLine);
		}
	}

	void LevelPackDetailSection::OnTouchEvent(const TouchEvent& event, Vector2i viewSize)
	{
		switch (event.Type) {
			case TouchEventType::Down: {
				std::int32_t pointerIndex = event.FindPointerIndex(event.ActionIndex);
				if (pointerIndex != -1) {
					float y = event.Pointers[pointerIndex].Y * (float)viewSize.Y;
					if (y < 80.0f) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_root->LeaveSection();
						return;
					}
					_touchStart = Vector2f(event.Pointers[pointerIndex].X * viewSize.X, y);
					_touchLast = _touchStart;
					_touchTime = 0.0f;
				}
				break;
			}
			case TouchEventType::Move: {
				if (_touchStart != Vector2f::Zero) {
					std::int32_t pointerIndex = event.FindPointerIndex(event.ActionIndex);
					if (pointerIndex != -1) {
						Vector2f touchMove = Vector2f(event.Pointers[pointerIndex].X * viewSize.X, event.Pointers[pointerIndex].Y * viewSize.Y);
						if (_availableHeight < _height) {
							_y += touchMove.Y - _touchLast.Y;
						}
						_touchLast = touchMove;
					}
				}
				break;
			}
			case TouchEventType::Up: {
				bool alreadyMoved = (_touchStart == Vector2f::Zero || (_touchStart - _touchLast).Length() > 10.0f || _touchTime > FrameTimer::FramesPerSecond);
				_touchStart = Vector2f::Zero;
				if (alreadyMoved) {
					return;
				}
				for (std::int32_t i = 0; i < std::int32_t(_items.size()); i++) {
					if (std::abs(_touchLast.Y - _items[i].Y) < ItemHeight * 0.5f) {
						if (_selectedIndex == i) {
							ExecuteSelected();
						} else {
							_root->PlaySfx("MenuSelect"_s, 0.5f);
							_animation = 0.0f;
							_confirmRemove = false;
							_selectedIndex = i;
							EnsureVisibleSelected();
						}
						break;
					}
				}
				break;
			}
			default:
				break;
		}
	}

	void LevelPackDetailSection::ExecuteSelected()
	{
		if (_selectedIndex < 0 || _selectedIndex >= std::int32_t(_items.size())) {
			return;
		}
		const ItemData& item = _items[_selectedIndex];
		_root->PlaySfx("MenuSelect"_s, 0.6f);

		if (item.Type == ItemType::Remove) {
			if (!_confirmRemove) {
				_confirmRemove = true;
				_animation = 0.0f;
				return;
			}
			auto result = LevelPacks::LevelPackLibrary::Get().Remove(_pack->Id);
			if (result != LevelPacks::LevelPackResult::Success) {
				LOGE("Level pack \"{}\" cannot be removed: {}", _pack->Id, LevelPacks::LevelPackLibrary::GetResultDescription(result));
				_confirmRemove = false;
				return;
			}
			_root->LeaveSection();
			return;
		}

		StartLevel(item.LevelName);
	}

	void LevelPackDetailSection::StartLevel(StringView levelName)
	{
#if defined(WITH_MULTIPLAYER)
		if (_mode == EpisodeSelectMode::LocalMultiplayer) {
			_root->SwitchToSection<CreateLocalGameOptionsSection>(levelName, nullptr);
			return;
		}
		if (_mode == EpisodeSelectMode::OnlineMultiplayer) {
			_root->SwitchToSection<CreateServerOptionsSection>(levelName, nullptr, _privateServer);
			return;
		}
#endif
		_root->SwitchToSection<StartGameOptionsSection>(levelName, nullptr);
	}
}

#endif
