#pragma once

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
}