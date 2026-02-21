#include "PauseSection.h"
#include "OptionsSection.h"
#include "InGameMenu.h"

#include "../../../nCine/I18n.h"

#include <Utf8.h>

namespace Jazz2::UI::Menu
{
	PauseSection::PauseSection()
		: _selectedIndex(0), _animation(0.0f)
	{
	}

	void PauseSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

#if defined(WITH_MULTIPLAYER)
		bool isLocalSession = true, isSpectating = false;
		if (auto inGameMenu = runtime_cast<InGameMenu>(_root)) {
			isLocalSession = inGameMenu->IsLocalSession();
			isSpectating = inGameMenu->IsSpectating();
		}
#endif

		_items.clear();

		// TRANSLATORS: Menu item in main menu
		_items.emplace_back(Item::Resume, _("Resume"));

#if defined(WITH_MULTIPLAYER)
		if (!isLocalSession) {
			// TRANSLATORS: Menu item in main menu
			_items.emplace_back(Item::Spectate, isSpectating ? _("Join Game") : _("Spectate"));
		}
#endif

		// TRANSLATORS: Menu item in main menu
		_items.emplace_back(Item::Options, _("Options"));

#if defined(WITH_MULTIPLAYER)
		// TRANSLATORS: Menu item in main menu
		_items.emplace_back(Item::Exit, isLocalSession ? _("Save & Exit") : _("Disconnect & Exit"));
#else
		// TRANSLATORS: Menu item in main menu
		_items.emplace_back(Item::Exit, _("Save & Exit"));
#endif

		_animation = 0.0f;
	}

	void PauseSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.02f, 1.0f);
		}

		if (_root->ActionHit(PlayerAction::Fire)) {
			ExecuteSelected();
		} else if (_root->ActionHit(PlayerAction::Menu)) {
			if (auto inGameMenu = runtime_cast<InGameMenu>(_root)) {
				inGameMenu->ResumeGame();
			}
		} else if (_root->ActionHit(PlayerAction::Up)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex > 0) {
				_selectedIndex--;
			} else {
				_selectedIndex = (std::int32_t)_items.size() - 1;
			}
		} else if (_root->ActionHit(PlayerAction::Down)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex < (std::int32_t)_items.size() - 1) {
				_selectedIndex++;
			} else {
				_selectedIndex = 0;
			}
		}
	}

	void PauseSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		Vector2f center = Vector2f(contentBounds.X + contentBounds.W * 0.5f, contentBounds.Y + contentBounds.H * 0.36f * (1.0f - 0.1f * (std::int32_t)_items.size()));
		std::int32_t charOffset = 0;

		for (int32_t i = 0; i < (std::int32_t)_items.size(); i++) {
			_items[i].TouchY = center.Y;

			if (_selectedIndex == i) {
				float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

				_root->DrawStringGlow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer + 10,
					Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
			} else {
				_root->DrawStringShadow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer,
					Alignment::Center, Font::DefaultColor, 0.9f);
			}

			center.Y += (contentBounds.H >= 250 ? 34.0f : 26.0f) + 36.0f * (1.0f - 0.18f * (std::int32_t)_items.size());
		}
	}

	void PauseSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;

				for (std::int32_t i = 0; i < (std::int32_t)_items.size(); i++) {
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

	void PauseSection::ExecuteSelected()
	{
		switch (_items[_selectedIndex].Type) {
			case Item::Resume: {
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				if (auto inGameMenu = runtime_cast<InGameMenu>(_root)) {
					inGameMenu->ResumeGame();
				}
				break;
			}
#if defined(WITH_MULTIPLAYER)
			case Item::Spectate: {
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				if (auto inGameMenu = runtime_cast<InGameMenu>(_root)) {
					inGameMenu->ToggleSpectate();
				}
				break;
			}
#endif
			case Item::Options: {
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				_root->SwitchToSection<OptionsSection>();
				break;
			}
			case Item::Exit: {
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				if (auto inGameMenu = runtime_cast<InGameMenu>(_root)) {
					inGameMenu->GoToMainMenu();
				}
				break;
			}
		}
	}
}