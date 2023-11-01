#include "SimpleMessageSection.h"
#include "MenuResources.h"

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	SimpleMessageSection::SimpleMessageSection(const StringView& message)
		: _message(message)
	{
	}

	SimpleMessageSection::SimpleMessageSection(String&& message)
		: _message(std::move(message))
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

		constexpr float topLine = 160.f;
		_root->DrawElement(MenuDim, center.X, topLine - 2.0f, IMenuContainer::BackgroundLayer,
			Alignment::Top, Colorf::Black, Vector2f(680.0f, 200.0f), Vector4f(1.0f, 0.0f, 0.7f, 0.0f));
		_root->DrawElement(MenuLine, 0, center.X, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(_message, charOffset, center.X, topLine - 40.0f, IMenuContainer::FontLayer,
			Alignment::Top, Font::DefaultColor, 0.9f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);
	}

	void SimpleMessageSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
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