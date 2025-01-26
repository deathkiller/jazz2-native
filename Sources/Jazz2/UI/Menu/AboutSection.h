#pragma once

#include "MenuSection.h"
#include "../FormattedTextBlock.h"

namespace Jazz2::UI::Menu
{
	/** @brief About menu section */
	class AboutSection : public MenuSection
	{
	public:
		AboutSection();

		Recti GetClipRectangle(const Recti& contentBounds) override;

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
		static constexpr std::int32_t TopLine = 2;
		static constexpr std::int32_t BottomLine = 42;
		static constexpr float AutoScrollRate = 0.4f;
		static constexpr float MaxScrollRate = 8.0f;

		FormattedTextBlock _textBlock;
		FormattedTextBlock _textBlockHeaderOnly;
		Vector2f _touchStart;
		Vector2f _touchLast;
		float _maxScrollOffset;
		float _touchTime;
		float _touchSpeed;
		float _scrollOffset;
		float _scrollRate;
		std::int8_t _touchDirection;
		bool _autoScroll;
		bool _rewind;

		void AddTranslator(StringView languageFile, char*& result, std::size_t& resultLength);
	};
}