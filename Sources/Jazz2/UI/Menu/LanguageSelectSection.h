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

	/**
		@brief Language selection menu section
		
		Lists the available interface languages found in the content and cache directories and lets the player choose
		one.
	*/
	class LanguageSelectSection : public ScrollableMenuSection<LanguageData>
	{
	public:
		/** @brief Creates a new instance */
		LanguageSelectSection();

		void OnDraw(Canvas* canvas) override;

	protected:
		void OnExecuteSelected() override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;

	private:
		void AddLanguage(const StringView languageFile, HashMap<String, bool>& foundLanguages, bool fromCache);
	};
}