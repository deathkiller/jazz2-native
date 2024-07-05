#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
	struct LanguageData {
		String FileName;
		String DisplayName;
	};

	class LanguageSelectSection : public ScrollableMenuSection<LanguageData>
	{
	public:
		LanguageSelectSection();

		void OnDraw(Canvas* canvas) override;

	private:
		void OnExecuteSelected() override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;

		void AddLanguage(const StringView languageFile);
	};
}