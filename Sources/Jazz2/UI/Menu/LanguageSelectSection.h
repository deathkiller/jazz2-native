#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	struct LanguageData {
		String FileName;
		String DisplayName;
	};
#endif

	class LanguageSelectSection : public ScrollableMenuSection<LanguageData>
	{
	public:
		LanguageSelectSection();

		void OnDraw(Canvas* canvas) override;

	protected:
		void OnExecuteSelected() override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;

	private:
		void AddLanguage(const StringView languageFile);
	};
}