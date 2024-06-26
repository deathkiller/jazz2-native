#pragma once

#include "MenuSection.h"
#include "../FormattedTextBlock.h"

namespace Jazz2::UI::Menu
{
	class AboutSection : public MenuSection
	{
	public:
		AboutSection();

		Recti GetClipRectangle(const Recti& contentBounds) override;

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		static constexpr std::int32_t TopLine = 2;
		static constexpr std::int32_t BottomLine = 42;
		static constexpr float AutoScrollRate = 0.4f;
		static constexpr float MaxScrollRate = 8.0f;

		FormattedTextBlock _textBlock;
		Vector2i _touchLast;
		float _scrollOffset;
		float _scrollRate;
		bool _autoScroll;
		bool _rewind;

		void AddTranslator(const StringView languageFile, char*& result, std::size_t& resultLength);
	};
}