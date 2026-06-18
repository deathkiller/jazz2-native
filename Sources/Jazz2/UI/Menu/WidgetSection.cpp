#include "WidgetSection.h"

#include <algorithm>

namespace Jazz2::UI::Menu
{
	// Standard divider line insets (matching the scrollable list sections)
	static constexpr std::int32_t WidgetTopLine = 31;
	static constexpr std::int32_t WidgetBottomLine = 42;

	WidgetSection::WidgetSection()
	{
	}

	Recti WidgetSection::GetClipRectangle(const Recti& contentBounds)
	{
		return Recti(contentBounds.X, contentBounds.Y + WidgetTopLine - 1, contentBounds.W,
			contentBounds.H - WidgetTopLine - WidgetBottomLine + 2);
	}

	void WidgetSection::OnUpdate(float timeMult)
	{
		if (_root->ActionHit(PlayerAction::Menu)) {
			OnBackPressed();
			return;
		}

		if (_content != nullptr) {
			_content->OnUpdate(timeMult);

			bool navUp, navDown, navSound;
			UpdateNavigation(timeMult, navUp, navDown, navSound);

			WidgetInput input;
			input.Up = navUp;
			input.Down = navDown;
			input.Left = _root->ActionHit(PlayerAction::Left);
			input.Right = _root->ActionHit(PlayerAction::Right);
			input.Fire = _root->ActionHit(PlayerAction::Fire);
			input.LeftHeld = _root->ActionPressed(PlayerAction::Left);
			input.RightHeld = _root->ActionPressed(PlayerAction::Right);
			input.PlayNavSound = navSound;
			// Navigate last: activating an item may switch or leave the section (destroying this), so no member
			// state must be touched afterwards
			_content->OnNavigate(input, _root);
		}
	}

	void WidgetSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + WidgetTopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - WidgetBottomLine;

		_root->DrawMenuFrame(centerX, topLine, bottomLine);

		if (!_title.empty()) {
			std::int32_t charOffset = 0;
			_root->DrawMenuTitle(charOffset, _title, centerX, topLine);
		}
	}

	void WidgetSection::OnDrawClipped(Canvas* canvas)
	{
		if (_content == nullptr) {
			return;
		}

		Recti clip = GetClipRectangle(_root->GetContentBounds());
		std::int32_t charOffset = 0;
		_content->Draw(_root, canvas, Rectf((float)clip.X, (float)clip.Y, (float)clip.W, (float)clip.H), charOffset);
	}

	void WidgetSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float y = event.pointers[pointerIndex].y * viewSize.Y;
				if (y < 80.0f) {
					OnBackPressed();
					return;
				}
			}
		}

		if (_content != nullptr) {
			_content->OnTouchEvent(event, viewSize, _root);
		}
	}

	void WidgetSection::OnKeyPressed(const nCine::KeyboardEvent& event)
	{
		if (_content != nullptr) {
			_content->OnKeyPressed(event, _root);
		}
	}

	void WidgetSection::OnTextInput(const nCine::TextInputEvent& event)
	{
		if (_content != nullptr) {
			_content->OnTextInput(event);
		}
	}

	NavigationFlags WidgetSection::GetNavigationFlags() const
	{
		// Restrict navigation to gamepads while a widget is capturing raw text input (so the keyboard types instead)
		return (_content != nullptr && _content->IsCapturingInput() ? NavigationFlags::AllowGamepads : NavigationFlags::AllowAll);
	}

	void WidgetSection::OnBackPressed()
	{
		_root->PlaySfx("MenuSelect"_s, 0.5f);
		_root->LeaveSection();
	}
}
