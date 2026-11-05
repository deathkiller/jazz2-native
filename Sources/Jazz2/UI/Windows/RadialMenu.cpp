#include "RadialMenu.h"

#if defined(WITH_UI_WINDOWS)

#include "Surface.h"

#include <cmath>

namespace Jazz2::UI::Windows
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

	RadialMenu::RadialMenu(SmallVectorImpl<MenuEntry>&& entries, Vector2f center)
		: _entries(Death::move(entries)), _center(center), _selected(-1), _appear(0.0f), _radius(0.0f), _itemRadius(0.0f)
	{
	}

	RadialMenu* RadialMenu::Show(Surface& surface, Vector2f center, SmallVectorImpl<MenuEntry>&& entries, Function<void()>&& onClosed)
	{
		auto menu = std::make_unique<RadialMenu>(Death::move(entries), center);
		RadialMenu* menuPtr = menu.get();
		OverlayOptions options;
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

	Vector2f RadialMenu::Measure(const Context& context, Vector2f available)
	{
		return available;
	}

	void RadialMenu::Arrange(const Context& context, const Rectf& bounds)
	{
		Bounds = bounds;
		_radius = context.Px(RingUnits);
		_itemRadius = context.Px(ItemUnits);
		// The whole ring stays on the screen
		float reach = _radius + _itemRadius * 1.5f;
		_center.X = std::clamp(_center.X, bounds.X + reach, std::max(bounds.X + reach, bounds.X + bounds.W - reach));
		_center.Y = std::clamp(_center.Y, bounds.Y + reach, std::max(bounds.Y + reach, bounds.Y + bounds.H - reach));
	}

	Vector2f RadialMenu::GetItemCenter(std::int32_t index) const
	{
		// From the top, clockwise, it grows out of the middle as it opens
		float t = 1.0f - (1.0f - _appear) * (1.0f - _appear);
		float angle = -Pi * 0.5f + 2.0f * Pi * float(index) / float(std::max<std::size_t>(_entries.size(), 1));
		return _center + Vector2f(std::cos(angle), std::sin(angle)) * (_radius * t);
	}

	std::int32_t RadialMenu::GetItemAt(Vector2f pos)
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

	void RadialMenu::PointTo(Vector2f direction)
	{
		_selected = (direction.Length() < 0.5f ? -1 : GetItemAt(_center + direction.Normalized() * _radius));
	}

	void RadialMenu::Confirm()
	{
		Function<void()> execute;
		if (_selected >= 0 && _selected < std::int32_t(_entries.size())) {
			execute = Death::move(_entries[_selected].Execute);
		}
		if (Surface* surface = GetSurface()) {
			surface->CloseOverlay(this);
		}
		if (execute) {
			execute();
		}
	}

	void RadialMenu::Draw(Painter& painter)
	{
		float t = 1.0f - (1.0f - _appear) * (1.0f - _appear);
		painter.PushOpacity(t);
		// A soft ring behind the items
		painter.Circle(_center, (_radius + _itemRadius * 1.3f) * t, Colorf(0.06f, 0.05f, 0.14f, 0.82f), PaintLayer::Back);
		painter.Circle(_center, _radius * 0.5f * t, Colorf(0.12f, 0.1f, 0.24f, 0.95f), PaintLayer::Fill);
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		for (std::int32_t i = 0; i < std::int32_t(_entries.size()); i++) {
			MenuEntry& entry = _entries[i];
			if (entry.IsSeparator()) {
				continue;
			}
			Vector2f center = GetItemCenter(i);
			bool selected = (i == _selected);
			bool enabled = (!entry.IsEnabled || entry.IsEnabled());
			bool checked = (entry.IsChecked && entry.IsChecked());
			float radius = _itemRadius * (selected ? 1.18f : 1.0f);
			painter.Circle(center, radius, selected ? Theme::ButtonCheckedHover : (checked ? Theme::ButtonChecked : Theme::ButtonFill), PaintLayer::Content);
			if (selected) {
				painter.Circle(center, radius + thickness * 2.0f, Colorf(Theme::Accent.R, Theme::Accent.G, Theme::Accent.B, 0.7f), PaintLayer::Fill);
			}
			float icon = painter.Px(Theme::IconSize);
			painter.Icon(entry.Icon, Rectf(center.X - icon * 0.5f, center.Y - icon * 0.5f, icon, icon), enabled ? Theme::IconColor : Theme::IconDisabled, PaintLayer::Overlay);
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
				Theme::TextAccent, Alignment::Center, PaintLayer::OverlayText);
			painter.SetTextSize(originalSize);
		}
		painter.PopOpacity();
	}

	void RadialMenu::Update(float timeMult)
	{
		_appear = std::min(_appear + Theme::GetAnimationStep(timeMult, AppearFrames), 1.0f);
		Element::Update(timeMult);
	}

	Element* RadialMenu::HitTest(Vector2f pos)
	{
		// It covers the screen, also in the frame it was opened in, before it's laid out
		return this;
	}

	bool RadialMenu::AcceptsPointer() const
	{
		return true;
	}

	bool RadialMenu::OnPointerDown(const PointerEvent& event)
	{
		// The finger that held still to open it may stay down and slide to an item, let go there it picks it
		// Pressed by the finger that opened it before it was even laid out, that's in its middle
		_pressFromCenter = (_radius <= 0.0f || (event.Pos - _center).Length() < _radius * 0.45f);
		_movedOut = !_pressFromCenter;
		_selected = GetItemAt(event.Pos);
		return true;
	}

	void RadialMenu::OnPointerMove(const PointerEvent& event)
	{
		_selected = GetItemAt(event.Pos);
		if ((event.Pos - _center).Length() >= _radius * 0.45f) {
			_movedOut = true;
		}
	}

	void RadialMenu::OnPointerUp(const PointerEvent& event, bool inside)
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
