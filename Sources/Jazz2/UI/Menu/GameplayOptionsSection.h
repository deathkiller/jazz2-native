#pragma once

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Gameplay options menu section

		Lists the gameplay-related settings, such as enhancements, language, continuous jump, and cheats, and leads
		into the related sub-screens. Built declaratively on top of @ref WidgetSection.
	*/
	class GameplayOptionsSection : public WidgetSection
	{
	public:
		~GameplayOptionsSection() override;

		void OnShow(IMenuContainer* root) override;

	private:
		bool _isDirty = false;
	};
}
