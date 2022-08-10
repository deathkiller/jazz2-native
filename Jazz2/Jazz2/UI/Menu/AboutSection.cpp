#include "AboutSection.h"

namespace Jazz2::UI::Menu
{
	AboutSection::AboutSection()
	{
	}

	void AboutSection::OnUpdate(float timeMult)
	{
		if (_root->ActionHit(PlayerActions::Menu)) {
			_root->PlaySfx("MenuSelect", 0.5f);
			_root->LeaveSection();
		}
	}

	void AboutSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		
		Vector2f pos = Vector2f(viewSize.X * 0.5f, viewSize.Y * 0.5f);
		pos.Y = std::max(150.0f, pos.Y * 0.75f);

		//_root->DrawElement("MenuDim"_s, 0, pos.X, pos.Y + 60.0f - 2.0f, IMenuContainer::MainLayer, Alignment::Top, Colorf::White, 55.0f, 14.2f, Rectf(0.0f, 0.3f, 1.0f, 0.7f));

		pos.X *= 0.35f;

		int charOffset = 0;

		_root->DrawStringShadow("Reimplementation of the game Jazz Jackrabbit 2 released in 1998. Supports various\nversions of the game (Shareware Demo, Holiday Hare '98, The Secret Files and\nChristmas Chronicles). Also, it partially supports some features of JJ2+ extension."_s, charOffset, pos.X, pos.Y - 22.0f,
			Alignment::Left, Font::DefaultColor, 0.7f, 0.4f, 0.6f, 0.6f, 0.8f, 0.9f, 1.2f);

		_root->DrawStringShadow("Created By"_s, charOffset, pos.X, pos.Y + 20.0f,
			Alignment::Left, Font::DefaultColor, 0.85f, 0.4f, 0.6f, 0.6f, 0.8f, 0.9f);
		_root->DrawStringShadow("Dan R."_s, charOffset, pos.X + 25.0f, pos.Y + 20.0f + 20.0f,
			Alignment::Left, Font::DefaultColor, 1.0f, 0.4f, 0.75f, 0.75f, 0.8f, 0.9f);

		_root->DrawStringShadow("<http://deat.tk/jazz2/>"_s, charOffset, pos.X + 25.0f + 70.0f, pos.Y + 20.0f + 20.0f,
			Alignment::Left, Font::DefaultColor, 0.7f, 0.4f, 0.6f, 0.6f, 0.8f, 0.9f);

		float y = pos.Y + 80.0f;

		_root->DrawElement("MenuLine"_s, 0, viewSize.X * 0.5f, pos.Y + 60.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		pos.Y = viewSize.Y - 100.0f;

		_root->DrawElement("MenuLine"_s, 1, viewSize.X * 0.5f, pos.Y + 60.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
	}

	void AboutSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			int pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
				if (y < 80.0f) {
					_root->PlaySfx("MenuSelect", 0.5f);
					_root->LeaveSection();
				}
			}
		}
	}
}