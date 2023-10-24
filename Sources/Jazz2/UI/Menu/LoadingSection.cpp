#include "LoadingSection.h"

namespace Jazz2::UI::Menu
{
	LoadingSection::LoadingSection(const StringView& message)
		: _message(message)
	{
	}

	LoadingSection::LoadingSection(String&& message)
		: _message(std::move(message))
	{

	}

	void LoadingSection::OnUpdate(float timeMult)
	{
	}

	void LoadingSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		Vector2f center = Vector2f(viewSize.X * 0.5f, viewSize.Y * 0.5f);

		constexpr float topLine = 131.0f;
		std::int32_t charOffset = 0;

		_root->DrawStringShadow(_message, charOffset, center.X, topLine + 40.0f, IMenuContainer::FontLayer,
			Alignment::Top, Font::DefaultColor, 1.2f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);
	}

	void LoadingSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
	}
}