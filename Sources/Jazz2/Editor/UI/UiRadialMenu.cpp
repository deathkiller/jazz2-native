#include "UiRadialMenu.h"

#if defined(WITH_EDITOR)

#include "UiSurface.h"

#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		constexpr float Pi = 3.14159265f;
		// Radius of the ring and of an item in units
		constexpr float RingUnits = 58.0f;
		constexpr float ItemUnits = 15.0f;
		// Size of the name in the middle relative to the usual text
		constexpr float LabelTextSize = 0.75f;
		// How long it takes to open, in frames at 60 FPS
		constexpr float AppearFrames = 8.0f;
	}

	UiRadialMenu::UiRadialMenu(SmallVectorImpl<UiMenuEntry>&& entries, Vector2f center)
		: _entries(Death::move(entries)), _center(center), _selected(-1), _appear(0.0f), _radius(0.0f), _itemRadius(0.0f)
	{
	}

	UiRadialMenu* UiRadialMenu::Show(UiSurface& surface, Vector2f center, SmallVectorImpl<UiMenuEntry>&& entries, Function<void()>&& onClosed)
	{
		auto menu = std::make_unique<UiRadialMenu>(Death::move(entries), center);
		UiRadialMenu* menuPtr = menu.get();
		UiOverlayOptions options;
		options.OnClosed = Death::move(onClosed);
		// It covers the screen, a tap anywhere outside of its items closes it
		options.Position = Vector2f(0.0f, 0.0f);
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		// The stick points at an item, the cursor would only cover the name in the middle
		options.HidesVirtualCursor = true;
		surface.PushOverlay(Death::move(menu), Death::move(options));
		return menuPtr;
	}

	Vector2f UiRadialMenu::Measure(const UiContext& context, Vector2f available)
	{
		return available;
	}

	void UiRadialMenu::Arrange(const UiContext& context, const Rectf& bounds)
	{
		Bounds = bounds;
		_radius = context.Px(RingUnits);
		_itemRadius = context.Px(ItemUnits);
		// The whole ring stays on the screen
		float reach = _radius + _itemRadius * 1.5f;
		_center.X = std::clamp(_center.X, bounds.X + reach, std::max(bounds.X + reach, bounds.X + bounds.W - reach));
		_center.Y = std::clamp(_center.Y, bounds.Y + reach, std::max(bounds.Y + reach, bounds.Y + bounds.H - reach));
	}

	Vector2f UiRadialMenu::GetItemCenter(std::int32_t index) const
	{
		// From the top, clockwise, it grows out of the middle as it opens
		float t = 1.0f - (1.0f - _appear) * (1.0f - _appear);
		float angle = -Pi * 0.5f + 2.0f * Pi * float(index) / float(std::max<std::size_t>(_entries.size(), 1));
		return _center + Vector2f(std::cos(angle), std::sin(angle)) * (_radius * t);
	}

	std::int32_t UiRadialMenu::GetItemAt(Vector2f pos)
	{
		Vector2f offset = pos - _center;
		if (_radius <= 0.0f || offset.Length() < _radius * 0.45f || _entries.empty()) {
			return -1;
		}
		// A finger slid out of the middle picks by its direction however far it went, a tap only on the ring, a tap
		// farther away closes the menu
		if (!_pressFromCenter && offset.Length() > _radius + _itemRadius * 1.6f) {
			return -1;
		}
		// The item whose direction is the closest, the ring is split evenly among them
		float angle = std::atan2(offset.Y, offset.X) + Pi * 0.5f;
		float step = 2.0f * Pi / float(_entries.size());
		std::int32_t index = std::int32_t(std::round(angle / step));
		index = ((index % std::int32_t(_entries.size())) + std::int32_t(_entries.size())) % std::int32_t(_entries.size());
		return (_entries[index].IsSeparator() || (_entries[index].IsEnabled && !_entries[index].IsEnabled()) ? -1 : index);
	}

	void UiRadialMenu::PointTo(Vector2f direction)
	{
		_selected = (direction.Length() < 0.5f ? -1 : GetItemAt(_center + direction.Normalized() * _radius));
	}

	void UiRadialMenu::Confirm()
	{
		Function<void()> execute;
		if (_selected >= 0 && _selected < std::int32_t(_entries.size())) {
			execute = Death::move(_entries[_selected].Execute);
		}
		if (UiSurface* surface = GetSurface()) {
			surface->CloseOverlay(this);
		}
		if (execute) {
			execute();
		}
	}

	void UiRadialMenu::Draw(UiPainter& painter)
	{
		float t = 1.0f - (1.0f - _appear) * (1.0f - _appear);
		painter.PushOpacity(t);
		// A soft ring behind the items
		painter.Circle(_center, (_radius + _itemRadius * 1.3f) * t, Colorf(0.06f, 0.05f, 0.14f, 0.82f), UiLayer::Back);
		painter.Circle(_center, _radius * 0.5f * t, Colorf(0.12f, 0.1f, 0.24f, 0.95f), UiLayer::Fill);
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		for (std::int32_t i = 0; i < std::int32_t(_entries.size()); i++) {
			UiMenuEntry& entry = _entries[i];
			if (entry.IsSeparator()) {
				continue;
			}
			Vector2f center = GetItemCenter(i);
			bool selected = (i == _selected);
			bool enabled = (!entry.IsEnabled || entry.IsEnabled());
			bool checked = (entry.IsChecked && entry.IsChecked());
			float radius = _itemRadius * (selected ? 1.18f : 1.0f);
			painter.Circle(center, radius, selected ? UiTheme::ButtonCheckedHover : (checked ? UiTheme::ButtonChecked : UiTheme::ButtonFill), UiLayer::Content);
			if (selected) {
				painter.Circle(center, radius + thickness * 2.0f, Colorf(UiTheme::Accent.R, UiTheme::Accent.G, UiTheme::Accent.B, 0.7f), UiLayer::Fill);
			}
			float icon = painter.Px(UiTheme::IconSize);
			painter.Icon(entry.Icon, Rectf(center.X - icon * 0.5f, center.Y - icon * 0.5f, icon, icon), enabled ? UiTheme::IconColor : UiTheme::IconDisabled, UiLayer::Overlay);
		}
		// What is picked, written in the middle
		if (_selected >= 0 && _selected < std::int32_t(_entries.size())) {
			// Smaller than usual, and even smaller if a long name still wouldn't fit between the items, but never below
			// the text of the smallest interface, the pixel font isn't readable any smaller
			float width = (_radius - _itemRadius * 1.25f) * 2.0f;
			float originalSize = painter.GetTextSize();
			float minSize = std::min(originalSize, 1.0f / std::max(painter.GetScale(), 1.0f));
			float size = std::max(originalSize * LabelTextSize, minSize);
			painter.SetTextSize(size);
			float textWidth = painter.MeasureText(_entries[_selected].Label);
			if (textWidth > width) {
				painter.SetTextSize(std::max(size * width / textWidth, minSize));
			}
			painter.TextInRect(_entries[_selected].Label, Rectf(_center.X - width * 0.5f, _center.Y - _itemRadius, width, _itemRadius * 2.0f),
				UiTheme::TextAccent, Alignment::Center, UiLayer::OverlayText);
			painter.SetTextSize(originalSize);
		}
		painter.PopOpacity();
	}

	void UiRadialMenu::Update(float timeMult)
	{
		_appear = std::min(_appear + UiTheme::GetAnimationStep(timeMult, AppearFrames), 1.0f);
		UiElement::Update(timeMult);
	}

	UiElement* UiRadialMenu::HitTest(Vector2f pos)
	{
		// It covers the screen, also in the frame it was opened in, before it's laid out
		return this;
	}

	bool UiRadialMenu::AcceptsPointer() const
	{
		return true;
	}

	bool UiRadialMenu::OnPointerDown(const UiPointerEvent& event)
	{
		// The finger that held still to open it may stay down and slide to an item, let go there it picks it
		// Pressed by the finger that opened it before it was even laid out, that's in its middle
		_pressFromCenter = (_radius <= 0.0f || (event.Pos - _center).Length() < _radius * 0.45f);
		_movedOut = !_pressFromCenter;
		_selected = GetItemAt(event.Pos);
		return true;
	}

	void UiRadialMenu::OnPointerMove(const UiPointerEvent& event)
	{
		_selected = GetItemAt(event.Pos);
		if ((event.Pos - _center).Length() >= _radius * 0.45f) {
			_movedOut = true;
		}
	}

	void UiRadialMenu::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		_selected = GetItemAt(event.Pos);
		if (_selected < 0 && !_movedOut) {
			// Let go in the middle without going anywhere, it stays open for a tap
			return;
		}
		Confirm();
	}
}

#endif
