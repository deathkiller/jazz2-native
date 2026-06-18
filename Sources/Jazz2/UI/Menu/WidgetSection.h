#pragma once

#include "MenuSection.h"
#include "MenuWidgets.h"

#include <memory>

namespace Jazz2::UI::Menu
{
	/**
		@brief Base class of a declarative menu section

		Hosts a single root @ref Widget (typically a @ref StackLayout) and an optional title, and implements the section
		update, draw, clip and touch callbacks by driving that widget tree. Subclasses only build their content in
		@ref MenuSection::OnShow via @ref SetTitle and @ref SetContent, replacing the per-section frame/title drawing,
		item layout, navigation and touch handling with a few declarative calls.
	*/
	class WidgetSection : public MenuSection
	{
	public:
		WidgetSection();

		Recti GetClipRectangle(const Recti& contentBounds) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;
		void OnKeyPressed(const nCine::KeyboardEvent& event) override;
		void OnTextInput(const nCine::TextInputEvent& event) override;
		NavigationFlags GetNavigationFlags() const override;

	protected:
		/** @brief Sets the section title drawn above the top divider line */
		void SetTitle(StringView title) {
			_title = title;
		}
		/** @brief Sets the root widget that fills the clipped content area */
		void SetContent(std::unique_ptr<Widget> content) {
			_content = std::move(content);
		}
		/** @brief Returns the root widget, or `nullptr` */
		Widget* GetContent() const {
			return _content.get();
		}
		/** @brief Called when the back button is pressed; leaves the section by default */
		virtual void OnBackPressed();

#ifndef DOXYGEN_GENERATING_OUTPUT
		String _title;
		std::unique_ptr<Widget> _content;
#endif
	};
}
