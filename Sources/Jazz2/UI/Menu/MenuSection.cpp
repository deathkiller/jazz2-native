#include "MenuSection.h"

using namespace Jazz2::Input;

namespace Jazz2::UI::Menu
{
	MenuSection::MenuSection()
		: _root(nullptr) {}

	MenuSection::~MenuSection() {}

	Recti MenuSection::GetClipRectangle(const Recti& contentBounds) {
		return {};
	}

	void MenuSection::OnShow(IMenuContainer* root) {
		_root = root;
	}

	void MenuSection::OnHide() {
		_root = nullptr;
	}

	NavigationFlags MenuSection::GetNavigationFlags() const {
		return NavigationFlags::AllowAll;
	}

	TransitionInfo MenuSection::GetTransition() const {
		return TransitionInfo{};
	}

	void MenuSection::UpdateNavigation(float timeMult, bool& outUp, bool& outDown, bool& outPlaySound)
	{
		// Longer delay before the first auto-repeat, then a steady (deliberately slow) interval between repeats
		constexpr float InitialDelay = 26.0f;
		constexpr float RepeatInterval = 10.0f;

		outUp = false;
		outDown = false;
		outPlaySound = false;

		bool upHeld = _root->ActionPressed(PlayerAction::Up);
		bool downHeld = _root->ActionPressed(PlayerAction::Down);
		if (!upHeld && !downHeld) {
			_navRepeat = 0.0f;
			return;
		}

		bool fire;
		if (_root->ActionHit(PlayerAction::Up) || _root->ActionHit(PlayerAction::Down)) {
			// Initial press: move once immediately, then wait the longer delay before auto-repeating
			fire = true;
			_navRepeat = InitialDelay;
		} else {
			_navRepeat -= timeMult;
			fire = (_navRepeat <= 0.0f);
			if (fire) {
				_navRepeat = RepeatInterval;
			}
		}

		if (fire) {
			if (upHeld) {
				outUp = true;
			} else {
				outDown = true;
			}
			outPlaySound = true;
		}
	}
}