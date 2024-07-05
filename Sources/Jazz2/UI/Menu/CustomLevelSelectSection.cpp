#include "CustomLevelSelectSection.h"
#include "StartGameOptionsSection.h"
#include "MainMenu.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"
#include "../../../nCine/Base/FrameTimer.h"

#if defined(WITH_MULTIPLAYER)
#	include "CreateServerOptionsSection.h"
#endif

#include <IO/DeflateStream.h>
#include <IO/MemoryStream.h>

using namespace Death::IO;
using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	CustomLevelSelectSection::CustomLevelSelectSection(bool multiplayer)
		: _multiplayer(multiplayer), _selectedIndex(0), _animation(0.0f), _y(0.0f), _height(0.0f), _pressedCount(0), _noiseCooldown(0.0f)
	{
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		_indexingThread.Run([](void* arg) {
			auto* section = static_cast<CustomLevelSelectSection*>(arg);
			section->AddCustomLevels();
		}, this);
#else
		AddCustomLevels();
#endif
	}

	CustomLevelSelectSection::~CustomLevelSelectSection()
	{
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		// Indicate that indexing thread should stop
		_selectedIndex = -1;
		_indexingThread.Join();
#endif
	}

	Recti CustomLevelSelectSection::GetClipRectangle(const Recti& contentBounds)
	{
		return Recti(contentBounds.X, contentBounds.Y + TopLine - 1, contentBounds.W, contentBounds.H - TopLine - BottomLine + 2);
	}

	void CustomLevelSelectSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_animation = 0.0f;
	}

	void CustomLevelSelectSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}
		if (_noiseCooldown > 0.0f) {
			_noiseCooldown -= timeMult;
		}

		if (_root->ActionHit(PlayerActions::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		} else if (!_items.empty()) {
			if (_root->ActionHit(PlayerActions::Fire)) {
				ExecuteSelected();
			} else if (_items.size() > 1) {
				if (_root->ActionPressed(PlayerActions::Up)) {
					if (_animation >= 1.0f - (_pressedCount * 0.096f) || _root->ActionHit(PlayerActions::Up)) {
						if (_noiseCooldown <= 0.0f) {
							_noiseCooldown = 10.0f;
							_root->PlaySfx("MenuSelect"_s, 0.5f);
						}
						_animation = 0.0f;

						if (_selectedIndex > 0) {
							_selectedIndex--;
						} else {
							_selectedIndex = (std::int32_t)(_items.size() - 1);
						}
						EnsureVisibleSelected();
						_pressedCount = std::min(_pressedCount + 6, 10);
					}
				} else if (_root->ActionPressed(PlayerActions::Down)) {
					if (_animation >= 1.0f - (_pressedCount * 0.096f) || _root->ActionHit(PlayerActions::Down)) {
						if (_noiseCooldown <= 0.0f) {
							_noiseCooldown = 10.0f;
							_root->PlaySfx("MenuSelect"_s, 0.5f);
						}
						_animation = 0.0f;

						if (_selectedIndex < _items.size() - 1) {
							_selectedIndex++;
						} else {
							_selectedIndex = 0;
						}
						EnsureVisibleSelected();
						_pressedCount = std::min(_pressedCount + 6, 10);
					}
				} else {
					_pressedCount = 0;
				}
			}

			_touchTime += timeMult;
		}
	}

	void CustomLevelSelectSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(_("Play Custom Levels"), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void CustomLevelSelectSection::OnDrawClipped(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;
		std::int32_t charOffset = 0;

		if (_items.empty()) {
			_root->DrawStringShadow(_("No custom level found!"), charOffset, centerX, contentBounds.Y + contentBounds.H * 0.33f, IMenuContainer::FontLayer,
				Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f, 0.4f, 0.6f, 0.6f, 0.8f, 0.88f);
			return;
		}

		float availableHeight = (bottomLine - topLine);
		constexpr float spacing = 20.0f;

		_y = (availableHeight - _height < 0.0f ? std::clamp(_y, availableHeight - _height, 0.0f) : 0.0f);

		Vector2f center = Vector2f(centerX, topLine + 12.0f + _y);
		float column1 = contentBounds.X + (contentBounds.W >= 460 ? (contentBounds.W * 0.25f) : 20.0f);
		float column2 = contentBounds.X + (contentBounds.W >= 460 ? (contentBounds.W * 0.52f) : (contentBounds.W * 0.44f));

		std::size_t itemsCount = _items.size();
		for (std::int32_t i = 0; i < itemsCount; i++) {
			_items[i].Y = center.Y;

			if (center.Y > topLine - ItemHeight && center.Y < bottomLine + ItemHeight) {
				if (_selectedIndex == i) {
					float xMultiplier = _items[i].DisplayName.size() * 0.5f;
					float easing = IMenuContainer::EaseOutElastic(_animation);
					float x = column1 + xMultiplier - easing * xMultiplier;
					float size = 0.7f + easing * 0.12f;

					_root->DrawStringShadow(_items[i].LevelName, charOffset, x, center.Y, IMenuContainer::FontLayer + 10,
						Alignment::Left, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				} else {
					_root->DrawStringShadow(_items[i].LevelName, charOffset, column1, center.Y, IMenuContainer::FontLayer,
						Alignment::Left, Font::DefaultColor, 0.7f);
				}

				_root->DrawStringShadow(_items[i].DisplayName, charOffset, column2, center.Y, IMenuContainer::FontLayer + 10 - 2,
					Alignment::Left, Font::DefaultColor, 0.7f);
			}

			center.Y += spacing;
		}

		_height = center.Y - (topLine + _y);

		if (_items[0].Y < TopLine + ItemHeight / 2) {
			_root->DrawElement(MenuGlow, 0, center.X, topLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
		if (_items[itemsCount - 1].Y > bottomLine - ItemHeight / 2) {
			_root->DrawElement(MenuGlow, 0, center.X, bottomLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
	}

	void CustomLevelSelectSection::OnTouchEvent(const TouchEvent& event, const Vector2i& viewSize)
	{
		switch (event.type) {
			case TouchEventType::Down: {
				std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
					if (y < 80.0f) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_root->LeaveSection();
						return;
					}

					_touchStart = Vector2f(event.pointers[pointerIndex].x * (float)viewSize.X, event.pointers[pointerIndex].y * (float)viewSize.Y);
					_touchLast = _touchStart;
					_touchTime = 0.0f;
				}
				break;
			}
			case TouchEventType::Move: {
				if (_touchStart != Vector2f::Zero) {
					std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
					if (pointerIndex != -1) {
						Vector2f touchMove = Vector2f(event.pointers[pointerIndex].x * (float)viewSize.X, event.pointers[pointerIndex].y * (float)viewSize.Y);
						_y += touchMove.Y - _touchLast.Y;
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

				float halfW = viewSize.X * 0.5f;
				std::size_t itemsCount = _items.size();
				for (std::int32_t i = 0; i < itemsCount; i++) {
					if (std::abs(_touchLast.X - halfW) < 150.0f && std::abs(_touchLast.Y - _items[i].Y) < 22.0f) {
						if (_selectedIndex == i) {
							ExecuteSelected();
						} else {
							_root->PlaySfx("MenuSelect"_s, 0.5f);
							_animation = 0.0f;
							_selectedIndex = i;
							EnsureVisibleSelected();
						}
						break;
					}
				}
				break;
			}
		}
	}

	void CustomLevelSelectSection::ExecuteSelected()
	{
		if (_items.empty()) {
			return;
		}

		auto& selectedItem = _items[_selectedIndex];
		_root->PlaySfx("MenuSelect"_s, 0.6f);

#if defined(WITH_MULTIPLAYER)
		if (_multiplayer) {
			_root->SwitchToSection<CreateServerOptionsSection>("unknown"_s, selectedItem.LevelName, nullptr);
			return;
		}
#endif
		_root->SwitchToSection<StartGameOptionsSection>("unknown"_s, selectedItem.LevelName, nullptr);
	}

	void CustomLevelSelectSection::EnsureVisibleSelected()
	{
		Recti contentBounds = _root->GetContentBounds();
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		if (_items[_selectedIndex].Y < topLine + ItemHeight * 0.5f) {
			_y += (topLine + ItemHeight * 0.5f - _items[_selectedIndex].Y);
		} else if (_items[_selectedIndex].Y > bottomLine - ItemHeight * 0.5f) {
			_y += (bottomLine - ItemHeight * 0.5f - _items[_selectedIndex].Y);
		}
	}

	void CustomLevelSelectSection::AddCustomLevels()
	{
		auto& resolver = ContentResolver::Get();

		// Search both "Content/Episodes/" and "Cache/Episodes/"
		for (auto item : fs::Directory(fs::CombinePath({ resolver.GetContentPath(), "Episodes"_s, "unknown"_s }), fs::EnumerationOptions::SkipDirectories)) {
			if (_selectedIndex < 0) {
				return;
			}
			AddLevel(item);
		}

		for (auto item : fs::Directory(fs::CombinePath({ resolver.GetCachePath(), "Episodes"_s, "unknown"_s }), fs::EnumerationOptions::SkipDirectories)) {
			if (_selectedIndex < 0) {
				return;
			}
			AddLevel(item);
		}

		sort(_items.begin(), _items.end(), [](const ItemData& a, const ItemData& b) -> bool {
			return (a.LevelName < b.LevelName);
		});
	}

	void CustomLevelSelectSection::AddLevel(const StringView levelFile)
	{
		if (fs::GetExtension(levelFile) != "j2l"_s) {
			return;
		}

		auto s = fs::Open(levelFile, FileAccess::Read);
		RETURN_ASSERT_MSG(s->IsValid(), "Cannot open file for reading");

		std::uint64_t signature = s->ReadValue<std::uint64_t>();
		std::uint8_t fileType = s->ReadValue<std::uint8_t>();
		RETURN_ASSERT_MSG(signature == 0x2095A59FF0BFBBEF && fileType == ContentResolver::LevelFile, "File has invalid signature");

		std::uint16_t flags = s->ReadValue<std::uint16_t>();

#if !defined(DEATH_DEBUG)
		// Don't show hidden levels in Release build if unlock cheat is not active, but show all levels in Debug build
		if ((flags & /*Hidden*/0x08) != 0 && !PreferencesCache::AllowCheatsUnlock) {
			return;
		}
#endif

		// Read compressed data
		std::int32_t compressedSize = s->ReadValue<std::int32_t>();

		DeflateStream uc(*s, compressedSize);

		// Read metadata
		std::uint8_t nameSize = uc.ReadValue<std::uint8_t>();
		String name(NoInit, nameSize);
		uc.Read(name.data(), nameSize);

		auto& level = _items.emplace_back();
		level.LevelName = fs::GetFileNameWithoutExtension(levelFile);
		level.DisplayName = std::move(name);
	}
}