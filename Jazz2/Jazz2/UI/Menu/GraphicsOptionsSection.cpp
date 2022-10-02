#include "GraphicsOptionsSection.h"
#include "RescaleModeSection.h"
#include "../../PreferencesCache.h"
#include "../../LevelHandler.h"
#include "../../../nCine/Application.h"

namespace Jazz2::UI::Menu
{
	GraphicsOptionsSection::GraphicsOptionsSection()
		:
		_selectedIndex(0),
		_animation(0.0f),
		_isDirty(false)
	{
		_items[(int)Item::RescaleMode].Name = "Rescale Mode"_s;
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS)
		_items[(int)Item::Fullscreen].Name = "Fullscreen"_s;
#endif
		_items[(int)Item::ShowPerformanceMetrics].Name = "Performance Metrics"_s;
	}

	GraphicsOptionsSection::~GraphicsOptionsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	void GraphicsOptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_animation = 0.0f;
	}

	void GraphicsOptionsSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}

		if (_root->ActionHit(PlayerActions::Fire) || (_selectedIndex >= 1 && (_root->ActionHit(PlayerActions::Left) || _root->ActionHit(PlayerActions::Right)))) {
			ExecuteSelected();
		} else if (_root->ActionHit(PlayerActions::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		} else if (_root->ActionHit(PlayerActions::Up)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex > 0) {
				_selectedIndex--;
			} else {
				_selectedIndex = (int)Item::Count - 1;
			}
		} else if (_root->ActionHit(PlayerActions::Down)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex < (int)Item::Count - 1) {
				_selectedIndex++;
			} else {
				_selectedIndex = 0;
			}
		}
	}

	void GraphicsOptionsSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		Vector2f center = Vector2f(viewSize.X * 0.5f, viewSize.Y * 0.5f);

		constexpr float topLine = 131.0f;
		float bottomLine = viewSize.Y - 42.0f;
		_root->DrawElement("MenuDim"_s, center.X, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement("MenuLine"_s, 0, center.X, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement("MenuLine"_s, 1, center.X, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		center.Y = topLine + (bottomLine - topLine) * 0.5f / (int)Item::Count;
		int charOffset = 0;

		_root->DrawStringShadow("Graphics"_s, charOffset, center.X, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		for (int i = 0; i < (int)Item::Count; i++) {
			_items[i].TouchY = center.Y;

			if (_selectedIndex == i) {
				float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

				_root->DrawElement("MenuGlow"_s, 0, center.X, center.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (_items[i].Name.size() + 3) * 0.5f * size, 4.0f * size, true);

				_root->DrawStringShadow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer + 10,
					Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

				if (i >= 1) {
					_root->DrawStringShadow("<"_s, charOffset, center.X - 60.0f - 30.0f * size, center.Y + 22.0f, IMenuContainer::FontLayer + 20,
						Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
					_root->DrawStringShadow(">"_s, charOffset, center.X + 70.0f + 30.0f * size, center.Y + 22.0f, IMenuContainer::FontLayer + 20,
						Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
				}
			} else {
				_root->DrawStringShadow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer,
					Alignment::Center, Font::DefaultColor, 0.9f);
			}

			if (i >= 1) {
				bool enabled;
				switch (i) {
					default:
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS)
					case (int)Item::Fullscreen: enabled = PreferencesCache::EnableFullscreen; break;
#endif
					case (int)Item::ShowPerformanceMetrics: enabled = PreferencesCache::ShowPerformanceMetrics; break;
				}

				_root->DrawStringShadow(enabled ? "Enabled"_s : "Disabled"_s, charOffset, center.X, center.Y + 22.0f, IMenuContainer::FontLayer - 10,
					Alignment::Center, (_selectedIndex == i ? Colorf(0.46f, 0.46f, 0.46f, 0.5f) : Font::DefaultColor), 0.8f);
			}

			center.Y += (bottomLine - topLine) * (i >= 1 ? 0.9f : 0.7f) / (int)Item::Count;
		}
	}

	void GraphicsOptionsSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			int pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;

				if (y < 80.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
					return;
				}

				for (int i = 0; i < (int)Item::Count; i++) {
					if (std::abs(x - 0.5f) < 0.22f && std::abs(y - _items[i].TouchY) < 22.0f) {
						if (_selectedIndex == i) {
							ExecuteSelected();
						} else {
							_root->PlaySfx("MenuSelect"_s, 0.5f);
							_animation = 0.0f;
							_selectedIndex = i;
						}
						break;
					}
				}
			}
		}
	}

	void GraphicsOptionsSection::ExecuteSelected()
	{
		_root->PlaySfx("MenuSelect"_s, 0.6f);

		switch (_selectedIndex) {
			case (int)Item::RescaleMode: _root->SwitchToSection<RescaleModeSection>(); break;
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS)
			case (int)Item::Fullscreen:
				PreferencesCache::EnableFullscreen = !PreferencesCache::EnableFullscreen;
				if (PreferencesCache::EnableFullscreen) {
					theApplication().gfxDevice().setResolution(true);
					theApplication().inputManager().setCursor(IInputManager::Cursor::Hidden);
				} else {
#	if defined(DEATH_TARGET_EMSCRIPTEN)
					theApplication().gfxDevice().setResolution(false);
#	else
					theApplication().gfxDevice().setResolution(false, LevelHandler::DefaultWidth, LevelHandler::DefaultHeight);
#	endif
					theApplication().inputManager().setCursor(IInputManager::Cursor::Arrow);
				}
				_isDirty = true;
				_animation = 0.0f;
				break;
#endif
			case (int)Item::ShowPerformanceMetrics:
				PreferencesCache::ShowPerformanceMetrics = !PreferencesCache::ShowPerformanceMetrics;
				_isDirty = true;
				_animation = 0.0f;
				break;
		}
	}
}