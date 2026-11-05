#include "Element.h"
#include "Surface.h"
#include "../../ContentResolver.h"

#if defined(WITH_UI_WINDOWS)

#include <algorithm>
#include <cmath>

namespace Jazz2::UI::Windows
{
	namespace
	{
		// How fast the highlight under the pointer fades in and out, a part of the way each frame
		constexpr float HoverFadeSpeed = 0.25f;
	}

	UI::Font* Context::GetFont(float scale)
	{
		if (Theme::ReadableFont) {
			if (UI::Font* font = Surface::GetReadableFont(Theme::GetTextScale(scale))) {
				return font;
			}
		}
		return ContentResolver::Get().GetFont(FontType::Small);
	}

	float Context::Px(float units) const
	{
		return std::round(units * Scale);
	}

	float Context::MeasureText(StringView text) const
	{
		UI::Font* font = GetFont(Scale);
		return (font != nullptr ? font->MeasureString(text, Theme::GetTextScale(Scale)).X : 0.0f);
	}

	float Context::GetTextHeight() const
	{
		UI::Font* font = GetFont(Scale);
		return (font != nullptr ? font->GetSizeInPixels() * Theme::GetTextScale(Scale) : 0.0f);
	}

	std::uint32_t Element::_layoutFrame = 0;
	bool Element::_layoutActive = false;

	Element::Element()
		: Bounds(), Visible(true), Enabled(true), Width(Auto), Height(Auto), Flex(0.0f), _parent(nullptr), _surface(nullptr),
			_hovered(false), _pressed(false), _focused(false), _hoverAmount(0.0f), _measureFrame(0), _measureScale(0.0f)
	{
	}

	void Element::BeginLayout()
	{
		_layoutFrame++;
		if (_layoutFrame == 0) {
			// 0 means no layout, a size measured then is never kept
			_layoutFrame = 1;
		}
		_layoutActive = true;
	}

	void Element::EndLayout()
	{
		_layoutActive = false;
	}

	Vector2f Element::MeasureCached(const Context& context, Vector2f available)
	{
		if (!_layoutActive) {
			return Measure(context, available);
		}
		// A size set while the frame is laid out, as a parent may do before it places its children, is measured again
		if (_measureFrame == _layoutFrame && _measureScale == context.Scale && _measureFixed.X == Width && _measureFixed.Y == Height &&
			(!MeasureDependsOnAvailable() || (_measureAvailable.X == available.X && _measureAvailable.Y == available.Y))) {
			return _measureSize;
		}
		_measureSize = Measure(context, available);
		_measureFrame = _layoutFrame;
		_measureScale = context.Scale;
		_measureAvailable = available;
		_measureFixed = Vector2f(Width, Height);
		return _measureSize;
	}

	bool Element::MeasureDependsOnAvailable() const
	{
		return true;
	}

	Element::~Element()
	{
		Surface::NotifyDestroyed(this);
	}

	Element* Element::AddChild(std::unique_ptr<Element> child)
	{
		child->_parent = this;
		_children.push_back(Death::move(child));
		return _children.back().get();
	}

	Element* Element::InsertChild(std::size_t index, std::unique_ptr<Element> child)
	{
		child->_parent = this;
		index = std::min(index, _children.size());
		return _children.insert(_children.begin() + index, Death::move(child))->get();
	}

	void Element::ClearChildren()
	{
		if (_children.empty()) {
			return;
		}
		// Taken out of the list first, what their removal does may add children again
		SmallVector<std::unique_ptr<Element>, 0> children = Death::move(_children);
		_children.clear();
		for (auto& child : children) {
			Surface::Retire(Death::move(child));
		}
	}

	Surface* Element::GetSurface() const
	{
		const Element* element = this;
		while (element->_parent != nullptr) {
			element = element->_parent;
		}
		return element->_surface;
	}

	bool Element::IsShown()
	{
		return (Visible && (!IsVisibleFunc || IsVisibleFunc()));
	}

	bool Element::IsUsable()
	{
		for (Element* element = this; element != nullptr; element = element->_parent) {
			if (!element->Enabled || (element->IsEnabledFunc && !element->IsEnabledFunc())) {
				return false;
			}
		}
		return true;
	}

	Vector2f Element::Measure(const Context& context, Vector2f available)
	{
		Vector2f size;
		for (auto& child : _children) {
			if (child->IsShown()) {
				Vector2f childSize = child->MeasureCached(context, available);
				size.X = std::max(size.X, childSize.X);
				size.Y = std::max(size.Y, childSize.Y);
			}
		}

		Vector2f fixedSize = GetFixedSize(context);
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	void Element::Arrange(const Context& context, const Rectf& bounds)
	{
		Bounds = bounds;
		for (auto& child : _children) {
			if (child->IsShown()) {
				child->Arrange(context, bounds);
			}
		}
	}

	void Element::Draw(Painter& painter)
	{
		for (auto& child : _children) {
			// What is scrolled or cut away isn't drawn at all, a long list doesn't cost more than what's seen of it, one in a
			// window of its own is drawn there
			if (child->IsShown() && !child->_detached && painter.IsVisible(child->Bounds)) {
				child->Draw(painter);
			}
		}
	}

	void Element::Update(float timeMult)
	{
		float target = (_hovered || _pressed ? 1.0f : 0.0f);
		if (_hoverAmount != target) {
			float step = 1.0f - std::pow(1.0f - HoverFadeSpeed, timeMult);
			_hoverAmount += (target - _hoverAmount) * step;
			if (std::abs(target - _hoverAmount) < 0.01f) {
				_hoverAmount = target;
			}
		}

		for (auto& child : _children) {
			child->Update(timeMult);
		}
	}

	Element* Element::HitTest(Vector2f pos)
	{
		if (!IsShown() || !Bounds.Contains(pos)) {
			return nullptr;
		}
		for (std::size_t i = _children.size(); i > 0; i--) {
			if (_children[i - 1]->_detached) {
				continue;
			}
			if (Element* hit = _children[i - 1]->HitTest(pos)) {
				return hit;
			}
		}
		return (AcceptsPointer() ? this : nullptr);
	}

	Rectf Element::GetHitBounds() const
	{
		return Bounds;
	}

	bool Element::AcceptsPointer() const
	{
		return false;
	}

	bool Element::OnPointerDown(const PointerEvent& event)
	{
		return false;
	}

	void Element::OnPointerDownInside(const PointerEvent& event)
	{
	}

	void Element::OnPointerMove(const PointerEvent& event)
	{
	}

	void Element::OnPointerUp(const PointerEvent& event, bool inside)
	{
	}

	void Element::OnPointerCancel()
	{
	}

	bool Element::YieldsToScroll() const
	{
		return false;
	}

	bool Element::DragsAfterHold() const
	{
		return false;
	}

	bool Element::YieldsToViewGestures() const
	{
		return false;
	}

	void Element::OnPointerHold(const PointerEvent& event)
	{
	}

	bool Element::OnWheel(const PointerEvent& event)
	{
		return false;
	}

	bool Element::OnKeyPressed(const KeyboardEvent& event)
	{
		return false;
	}

	bool Element::OnTextInput(const TextInputEvent& event)
	{
		return false;
	}

	bool Element::IsFocusable() const
	{
		return false;
	}

	bool Element::IsTabStop() const
	{
		return IsFocusable();
	}

	bool Element::KeepsFocus() const
	{
		return false;
	}

	void Element::OnFocusChanged(bool focused)
	{
	}

	StringView Element::GetTitle() const
	{
		return {};
	}

	StringView Element::GetTooltip(Vector2f pos) const
	{
		return Tooltip;
	}

	Vector2f Element::GetFixedSize(const Context& context) const
	{
		return Vector2f(Width >= 0.0f ? context.Px(Width) : -1.0f, Height >= 0.0f ? context.Px(Height) : -1.0f);
	}

	Stack::Stack(Windows::Orientation orientation, float spacing, Thickness padding)
		: Orientation(orientation), Spacing(spacing), Padding(padding)
	{
	}

	Vector2f Stack::Measure(const Context& context, Vector2f available)
	{
		bool horizontal = (Orientation == Orientation::Horizontal);
		float padX = context.Px(Padding.Left) + context.Px(Padding.Right);
		float padY = context.Px(Padding.Top) + context.Px(Padding.Bottom);
		float spacing = context.Px(Spacing);
		Vector2f inner(std::max(available.X - padX, 0.0f), std::max(available.Y - padY, 0.0f));
		_childAvailable = inner;
		_childAvailableFrame = GetLayoutFrame();

		float main = 0.0f, cross = 0.0f;
		std::int32_t count = 0;
		for (auto& child : _children) {
			if (!child->IsShown()) {
				continue;
			}
			Vector2f size = child->MeasureCached(context, inner);
			main += (horizontal ? size.X : size.Y);
			cross = std::max(cross, horizontal ? size.Y : size.X);
			count++;
		}
		if (count > 1) {
			main += spacing * float(count - 1);
		}
		_childContent = (horizontal ? Vector2f(main, cross) : Vector2f(cross, main));

		Vector2f size = (horizontal ? Vector2f(main + padX, cross + padY) : Vector2f(cross + padX, main + padY));
		Vector2f fixedSize = GetFixedSize(context);
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	void Stack::Arrange(const Context& context, const Rectf& bounds)
	{
		Bounds = bounds;

		bool horizontal = (Orientation == Orientation::Horizontal);
		float left = context.Px(Padding.Left), top = context.Px(Padding.Top);
		float right = context.Px(Padding.Right), bottom = context.Px(Padding.Bottom);
		Rectf inner(bounds.X + left, bounds.Y + top, std::max(bounds.W - left - right, 0.0f), std::max(bounds.H - top - bottom, 0.0f));
		float spacing = context.Px(Spacing);

		// The children are measured in the space Measure() gave them in this frame, so they aren't measured again (see
		// MeasureCached()), as long as the stack got at least the room they took and at most the room they were given ---
		// a size limited by the room, or text wrapped by it, comes out the same in any room between the two. A stack that
		// got less, as one squeezed, or one that wasn't measured, measures them in the room it got.
		std::uint32_t layoutFrame = GetLayoutFrame();
		Vector2f childAvailable(inner.W, inner.H);
		if (layoutFrame != 0 && _childAvailableFrame == layoutFrame) {
			if (inner.W + 0.5f >= _childContent.X && inner.W <= _childAvailable.X + 0.5f) {
				childAvailable.X = _childAvailable.X;
			}
			if (inner.H + 0.5f >= _childContent.Y && inner.H <= _childAvailable.Y + 0.5f) {
				childAvailable.Y = _childAvailable.Y;
			}
		}

		SmallVector<float, 16>& sizes = _childSizes;
		sizes.clear();
		float fixedTotal = 0.0f, flexTotal = 0.0f;
		std::int32_t count = 0;
		for (auto& child : _children) {
			if (!child->IsShown()) {
				sizes.push_back(0.0f);
				continue;
			}
			count++;
			if (child->Flex > 0.0f) {
				flexTotal += child->Flex;
				sizes.push_back(0.0f);
			} else {
				Vector2f size = child->MeasureCached(context, childAvailable);
				float mainSize = (horizontal ? size.X : size.Y);
				sizes.push_back(mainSize);
				fixedTotal += mainSize;
			}
		}
		if (count > 1) {
			fixedTotal += spacing * float(count - 1);
		}

		float leftover = std::max((horizontal ? inner.W : inner.H) - fixedTotal, 0.0f);
		float pos = (horizontal ? inner.X : inner.Y);
		for (std::size_t i = 0; i < _children.size(); i++) {
			Element* child = _children[i].get();
			if (!child->IsShown()) {
				continue;
			}
			float mainSize = (child->Flex > 0.0f ? std::floor(leftover * child->Flex / flexTotal) : sizes[i]);
			Rectf childBounds = (horizontal
				? Rectf(pos, inner.Y, mainSize, inner.H)
				: Rectf(inner.X, pos, inner.W, mainSize));
			child->Arrange(context, childBounds);
			pos += mainSize + spacing;
		}
	}

	Panel::Panel(PanelStyle style, Windows::Orientation orientation, float spacing, Thickness padding)
		: Stack(orientation, spacing, padding), Style(style), ClipContent(false)
	{
	}

	void Panel::Draw(Painter& painter)
	{
		switch (Style) {
			case PanelStyle::Panel:
				painter.Panel(Bounds, false);
				break;
			case PanelStyle::Floating:
				painter.Panel(Bounds, true, Theme::PanelFill, Theme::PanelBorder);
				break;
			case PanelStyle::Header:
				painter.FillRect(Bounds, painter.Backdrop(Bounds, Theme::PanelHeader, false));
				break;
			case PanelStyle::Inset:
				painter.RoundRect(Bounds, Theme::InputFill);
				painter.RoundRectBorder(Bounds, Theme::PanelBorderDim);
				break;
			case PanelStyle::Docked:
				painter.Panel(Bounds, false, Theme::PanelFill, Theme::PanelBorderDim, false);
				break;
			default:
				break;
		}
		if (ClipContent) {
			painter.PushClip(Bounds);
			Stack::Draw(painter);
			painter.PopClip();
		} else {
			Stack::Draw(painter);
		}
	}

	bool Panel::AcceptsPointer() const
	{
		return (Style != PanelStyle::None);
	}
}

#endif
