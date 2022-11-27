#include "SimpleMessageSection.h"

namespace Jazz2::UI::Menu
{
	SimpleMessageSection::SimpleMessageSection(Message message)
		:
		_message(message)
	{
	}

	void SimpleMessageSection::OnUpdate(float timeMult)
	{
		if (_root->ActionHit(PlayerActions::Menu) || _root->ActionHit(PlayerActions::Fire)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		}
	}

	void SimpleMessageSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		Vector2f center = Vector2f(viewSize.X * 0.5f, viewSize.Y * 0.5f);

		constexpr float topLine = 131.0f;
		_root->DrawElement("MenuDim"_s, center.X, topLine - 2.0f, IMenuContainer::BackgroundLayer,
			Alignment::Top, Colorf::Black, Vector2f(680.0f, 200.0f), Vector4f(1.0f, 0.0f, 0.7f, 0.0f));
		_root->DrawElement("MenuLine"_s, 0, center.X, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		int charOffset = 0;
		_root->DrawStringShadow("Error"_s, charOffset, center.X, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		switch (_message) {
			case Message::CannotLoadLevel:
				_root->DrawStringShadow("\f[c:0x704a4a]Cannot load specified level!\f[c]\n\nMake sure all necessary files\nare accessible and try it again."_s, charOffset, center.X, topLine + 40.0f, IMenuContainer::FontLayer,
					Alignment::Top, Font::DefaultColor, 0.9f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);
				break;
		}
	}

	void SimpleMessageSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			int pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
				if (y < 80.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
				}
			}
		}
	}
}