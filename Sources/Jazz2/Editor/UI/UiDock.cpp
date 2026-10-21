#include "UiDock.h"

#if defined(WITH_EDITOR)

#include "UiSurface.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		// How long the panel glides between its docked and its floating place, in frames at 60 FPS
		constexpr float GlideFrames = 16.0f;
		// How long the place it would be docked at takes to light up
		constexpr float PreviewFrames = 8.0f;
		// How long a drawer takes to slide in or out
		constexpr float DrawerFrames = 12.0f;
		// How far the header of a docked panel is dragged before the panel comes off, in units
		constexpr float UndockDistance = 8.0f;
		// How close to the edge a floating panel is let go to dock it, in units
		constexpr float DockZoneWidth = 40.0f;
		// How far from the floating panel to the edge
		constexpr float FloatingInset = 24.0f;

		float EaseInOut(float t)
		{
			// Cubic, so it leaves and arrives softly but crosses the screen quickly
			return (t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f);
		}

		Rectf LerpRect(const Rectf& from, const Rectf& to, float t)
		{
			return Rectf(std::round(from.X + (to.X - from.X) * t), std::round(from.Y + (to.Y - from.Y) * t),
				std::round(from.W + (to.W - from.W) * t), std::round(from.H + (to.H - from.H) * t));
		}

		/** Dots that show the header can be grabbed */
		class DockGrip : public UiElement
		{
		public:
			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(context.Px(7.0f), context.Px(UiTheme::ButtonHeight));
			}

			void Draw(UiPainter& painter) override
			{
				float dot = std::max(1.0f, std::round(painter.GetScale()));
				float step = dot * 2.0f;
				float x = std::round(Bounds.X + (Bounds.W - step - dot) * 0.5f);
				float y = std::round(Bounds.Y + (Bounds.H - step * 3.0f - dot) * 0.5f);
				Colorf color = UiTheme::IconColor;
				color.A = (GetParent() != nullptr && GetParent()->IsHovered() ? 0.8f : 0.4f);
				for (std::int32_t row = 0; row < 4; row++) {
					for (std::int32_t column = 0; column < 2; column++) {
						painter.FillRect(Rectf(x + column * step, y + row * step, dot, dot), color, UiLayer::Content);
					}
				}
			}
		};
	}

	UiDockFrame::UiDockFrame(UiDockSide side, StringView tooltip)
		: _side(side), _dockButton(nullptr), _floating(false), _buttonShowsFloating(true), _floatAmount(0.0f), _previewAmount(0.0f),
			_moving(false), _undockPending(false), _overDockZone(false), _restoreBounds(false), _resizeEdges(0), _hoverEdges(0), _scale(1.0f)
	{
		_panel = Add<UiPanel>(UiPanelStyle::Panel, UiOrientation::Vertical, UiTheme::Spacing, UiThickness(UiTheme::Padding));

		_header = _panel->Add<UiDragBar>(2.0f);
		_header->Tooltip = tooltip;
		_header->Add<DockGrip>();
		_headerContent = _header->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		_headerContent->Flex = 1.0f;
		_dockButton = _header->Add<UiButton>(StringView(), EditorIcon::Undock, [this]() {
			// A drawer only closes
			if (_drawer) {
				SetDrawerOpen(false);
			} else {
				SetFloating(!_floating);
			}
		});
		_dockButton->Style = UiButtonStyle::Flat;
		_header->OnDragStart = [this](Vector2f pos) { BeginMove(pos); };
		_header->OnDrag = [this](Vector2f offset, Vector2f pos) { Move(offset, pos); };
		_header->OnDragEnd = [this](Vector2f pos) { EndMove(pos); };
		_header->OnDoubleClick = [this]() {
			if (_drawer) {
				SetDrawerOpen(false);
			} else {
				SetFloating(!_floating);
			}
		};
	}

	void UiDockFrame::SetFloating(bool floating, bool animate)
	{
		if (!animate) {
			_floatAmount = (floating ? 1.0f : 0.0f);
		}
		if (_floating == floating) {
			return;
		}
		// A panel that hasn't floated yet floats next to where it's docked, see GetFloatingRect()
		_floating = floating;
		_restoreBounds = false;
		NotifyChanged();
	}

	float UiDockFrame::GetDockedAmount() const
	{
		// A drawer takes no room, it slides over the view
		return (_drawer ? 0.0f : 1.0f - EaseInOut(_floatAmount));
	}

	bool UiDockFrame::IsLifted() const
	{
		return (_drawer ? _drawerAmount > 0.0f : (_floating || _floatAmount > 0.0f));
	}

	void UiDockFrame::SetDrawerMode(bool drawer)
	{
		if (_drawer == drawer) {
			return;
		}
		_drawer = drawer;
		_drawerOpen = false;
		_drawerAmount = 0.0f;
		_moving = false;
		_undockPending = false;
		_buttonShowsFloating = !_floating;
	}

	void UiDockFrame::SetDrawerOpen(bool open)
	{
		if (_drawer) {
			_drawerOpen = open;
		}
	}

	Rectf UiDockFrame::GetTargetBounds(const UiContext& context, const Rectf& dockBounds) const
	{
		if (_drawer) {
			// Slid out past its edge with its shadow, and in over the view
			float offset = std::round((1.0f - EaseInOut(_drawerAmount)) * (dockBounds.W + context.Px(12.0f)));
			return Rectf(_side == UiDockSide::Left ? dockBounds.X - offset : dockBounds.X + offset, dockBounds.Y, dockBounds.W, dockBounds.H);
		}
		if (_floatAmount <= 0.0f) {
			return dockBounds;
		}
		Rectf floatingRect = GetFloatingRect(context.Scale, dockBounds);
		return (_floatAmount >= 1.0f ? floatingRect : LerpRect(dockBounds, floatingRect, EaseInOut(_floatAmount)));
	}

	Rectf UiDockFrame::GetDefaultFloatingBounds(float scale, const Rectf& dockBounds) const
	{
		// As wide as when docked, not as high, a little off the edge it was docked at
		float width = std::max(MinFloatingWidth, std::round(dockBounds.W / scale));
		float height = std::max(MinFloatingHeight, std::round(dockBounds.H / scale * 0.7f));
		float x = (_side == UiDockSide::Left ? dockBounds.X / scale + FloatingInset : (dockBounds.X + dockBounds.W) / scale - width - FloatingInset);
		return Rectf(std::round(x), std::round(dockBounds.Y / scale + FloatingInset), width, height);
	}

	Rectf UiDockFrame::GetFloatingRect(float scale, const Rectf& dockBounds) const
	{
		Rectf bounds = (FloatingBounds.W > 0.0f ? FloatingBounds : GetDefaultFloatingBounds(scale, dockBounds));
		Rectf rect(std::round(bounds.X * scale), std::round(bounds.Y * scale), std::round(bounds.W * scale), std::round(bounds.H * scale));

		// It stays on the screen, the part of it that isn't cut off, whatever size the window got since
		UiSurface* surface = GetSurface();
		Rectf screen = (surface != nullptr ? surface->GetSafeBounds() : Rectf(0.0f, 0.0f, dockBounds.X + dockBounds.W, dockBounds.Y + dockBounds.H));
		rect.W = std::min(rect.W, screen.W);
		rect.H = std::min(rect.H, screen.H);
		rect.X = std::clamp(rect.X, screen.X, screen.X + screen.W - rect.W);
		rect.Y = std::clamp(rect.Y, screen.Y, screen.Y + screen.H - rect.H);
		return rect;
	}

	std::uint8_t UiDockFrame::GetEdgesAt(Vector2f pos) const
	{
		return (IsResizable() ? UiResizeEdges::GetAt(Bounds, _scale, pos, 2.5f) : 0);
	}

	bool UiDockFrame::IsResizable() const
	{
		return (!_drawer && _floating && _floatAmount >= 1.0f && !_moving);
	}

	Rectf UiDockFrame::GetHitBounds() const
	{
		return (IsResizable() ? UiResizeEdges::GetHitBounds(Bounds, _scale) : Bounds);
	}

	bool UiDockFrame::IsInDockZone(Vector2f pos) const
	{
		if (pos.Y < _dockBounds.Y || pos.Y > _dockBounds.Y + _dockBounds.H) {
			return false;
		}
		float zone = _scale * DockZoneWidth;
		return (_side == UiDockSide::Left ? pos.X < _dockBounds.X + zone : pos.X > _dockBounds.X + _dockBounds.W - zone);
	}

	void UiDockFrame::BeginMove(Vector2f pos)
	{
		if (_drawer) {
			return;
		}
		_moving = true;
		_moveStart = pos;
		_undockPending = !_floating;
		_overDockZone = false;
		_boundsBeforeMove = FloatingBounds;
		if (_floating) {
			// From where it's seen, it may have been put back on the screen
			Rectf rect = GetFloatingRect(_scale, _dockBounds);
			FloatingBounds = Rectf(rect.X / _scale, rect.Y / _scale, rect.W / _scale, rect.H / _scale);
			_moveStartBounds = FloatingBounds;
		}
	}

	void UiDockFrame::Move(Vector2f offset, Vector2f pos)
	{
		if (!_moving) {
			return;
		}
		if (_undockPending) {
			if (offset.Length() < _scale * UndockDistance) {
				return;
			}
			// It comes off where it's held, as large as it floated the last time
			_undockPending = false;
			Rectf bounds = (FloatingBounds.W > 0.0f ? FloatingBounds : GetDefaultFloatingBounds(_scale, _dockBounds));
			Vector2f grab = (_moveStart - Vector2f(_dockBounds.X, _dockBounds.Y)) / _scale;
			grab.X = std::min(grab.X, bounds.W - FloatingInset);
			_moveStartBounds = Rectf(_moveStart.X / _scale - grab.X, _moveStart.Y / _scale - grab.Y, bounds.W, bounds.H);
			FloatingBounds = _moveStartBounds;
			_floating = true;
			_restoreBounds = false;
		}
		if (!_floating) {
			return;
		}
		FloatingBounds.X = _moveStartBounds.X + offset.X / _scale;
		FloatingBounds.Y = _moveStartBounds.Y + offset.Y / _scale;
		_overDockZone = IsInDockZone(pos);
	}

	void UiDockFrame::EndMove(Vector2f pos)
	{
		if (!_moving) {
			return;
		}
		_moving = false;
		_overDockZone = false;
		if (_undockPending) {
			// Only a click
			_undockPending = false;
			return;
		}
		if (_floating && IsInDockZone(pos)) {
			_floating = false;
			// It floats where it was before the next time, once it glided back, it would jump there now
			_restoreBounds = true;
		}
		NotifyChanged();
	}

	void UiDockFrame::NotifyChanged()
	{
		if (OnChanged) {
			OnChanged();
		}
	}

	void UiDockFrame::DrawDockPreview(UiPainter& painter)
	{
		if (_previewAmount <= 0.0f) {
			return;
		}
		// The place grows out of its middle and lights up
		float amount = EaseInOut(_previewAmount);
		float inset = std::round((1.0f - amount) * painter.Px(16.0f));
		Rectf rect(_dockBounds.X + inset, _dockBounds.Y + inset, _dockBounds.W - inset * 2.0f, _dockBounds.H - inset * 2.0f);
		if (rect.W <= 0.0f || rect.H <= 0.0f) {
			return;
		}
		Colorf fill = UiTheme::Accent;
		fill.A = 0.16f * amount;
		painter.FillRect(rect, fill, UiLayer::Fill);
		Colorf border = UiTheme::Accent;
		border.A = 0.85f * amount;
		painter.Frame(rect, border, std::max(1.0f, std::round(painter.GetScale() * 1.5f)), UiLayer::Overlay);
	}

	void UiDockFrame::Arrange(const UiContext& context, const Rectf& bounds)
	{
		_dockBounds = bounds;
		_scale = context.Scale;
		Bounds = GetTargetBounds(context, bounds);
		// A shadow under it once it's off its place
		_panel->Style = (IsLifted() ? UiPanelStyle::Floating : UiPanelStyle::Panel);
		_panel->Arrange(context, Bounds);
	}

	void UiDockFrame::Draw(UiPainter& painter)
	{
		if (_drawer) {
			// A closed drawer isn't there at all, and one sliding comes out of its edge, not over what is next to it
			if (_drawerAmount <= 0.0f) {
				return;
			}
			float margin = painter.Px(12.0f);
			Rectf clip = (_side == UiDockSide::Left
				? Rectf(_dockBounds.X, _dockBounds.Y - margin, _dockBounds.W + margin, _dockBounds.H + margin * 2.0f)
				: Rectf(_dockBounds.X - margin, _dockBounds.Y - margin, _dockBounds.W + margin, _dockBounds.H + margin * 2.0f));
			painter.PushClip(clip);
			UiElement::Draw(painter);
			painter.PopClip();
			return;
		}

		UiElement::Draw(painter);

		if (!_floating || _floatAmount < 1.0f) {
			return;
		}
		UiResizeEdges::Draw(painter, Bounds, _resizeEdges != 0 ? _resizeEdges : _hoverEdges);
	}

	void UiDockFrame::Update(float timeMult)
	{
		// With reduced motion, everything gets where it goes at once
		float target = (_floating ? 1.0f : 0.0f);
		if (_floatAmount != target) {
			float step = UiTheme::GetAnimationStep(timeMult, GlideFrames);
			_floatAmount = (_floatAmount < target ? std::min(_floatAmount + step, target) : std::max(_floatAmount - step, target));
			if (_floatAmount <= 0.0f && _restoreBounds) {
				_restoreBounds = false;
				FloatingBounds = _boundsBeforeMove;
				NotifyChanged();
			}
		}

		float drawerTarget = (_drawer && _drawerOpen ? 1.0f : 0.0f);
		if (_drawerAmount != drawerTarget) {
			float step = UiTheme::GetAnimationStep(timeMult, DrawerFrames);
			_drawerAmount = (_drawerAmount < drawerTarget ? std::min(_drawerAmount + step, drawerTarget) : std::max(_drawerAmount - step, drawerTarget));
		}

		float previewTarget = (_moving && _floating && _overDockZone ? 1.0f : 0.0f);
		if (_previewAmount != previewTarget) {
			float step = UiTheme::GetAnimationStep(timeMult, PreviewFrames);
			_previewAmount = (_previewAmount < previewTarget ? std::min(_previewAmount + step, previewTarget) : std::max(_previewAmount - step, previewTarget));
		}

		UiSurface* surface = GetSurface();
		_hoverEdges = (_hovered && surface != nullptr ? GetEdgesAt(surface->GetPointerPos()) : 0);
		if (surface != nullptr) {
			UiResizeEdges::ShowCursor(*surface, _resizeEdges != 0 ? _resizeEdges : _hoverEdges);
		}

		if (_drawer) {
			if (_dockButton->Icon != EditorIcon::Close) {
				_dockButton->Icon = EditorIcon::Close;
				_dockButton->Tooltip = _x("editor", "Close the panel");
			}
		} else if (_buttonShowsFloating != _floating || _dockButton->Tooltip.empty() || _dockButton->Icon == EditorIcon::Close) {
			_buttonShowsFloating = _floating;
			_dockButton->Icon = (_floating ? EditorIcon::Dock : EditorIcon::Undock);
			_dockButton->Tooltip = (_floating ? _x("editor", "Dock the panel back") : _x("editor", "Float the panel, it can be moved anywhere"));
		}

		UiElement::Update(timeMult);
	}

	UiElement* UiDockFrame::HitTest(Vector2f pos)
	{
		if (!IsShown() || (_drawer && _drawerAmount <= 0.0f)) {
			return nullptr;
		}
		if (GetEdgesAt(pos) != 0) {
			return this;
		}
		if (!Bounds.Contains(pos)) {
			return nullptr;
		}
		for (std::size_t i = _children.size(); i > 0; i--) {
			if (UiElement* hit = _children[i - 1]->HitTest(pos)) {
				return hit;
			}
		}
		// Nothing goes through to the level under it
		return this;
	}

	bool UiDockFrame::AcceptsPointer() const
	{
		return true;
	}

	bool UiDockFrame::OnPointerDown(const UiPointerEvent& event)
	{
		if (OnActivated) {
			OnActivated();
		}
		std::uint8_t edges = GetEdgesAt(event.Pos);
		if (edges == 0 || event.Button != MouseButton::Left) {
			return false;
		}
		_resizeEdges = edges;
		_resizeStart = event.Pos;
		Rectf rect = GetFloatingRect(_scale, _dockBounds);
		_resizeStartBounds = Rectf(rect.X / _scale, rect.Y / _scale, rect.W / _scale, rect.H / _scale);
		return true;
	}

	void UiDockFrame::OnPointerDownInside(const UiPointerEvent& event)
	{
		if (OnActivated) {
			OnActivated();
		}
	}

	void UiDockFrame::OnPointerMove(const UiPointerEvent& event)
	{
		if (_resizeEdges == 0) {
			return;
		}
		Vector2f offset = (event.Pos - _resizeStart) / _scale;
		const Rectf& start = _resizeStartBounds;
		Rectf bounds = start;
		if (_resizeEdges & UiResizeEdges::Left) {
			bounds.X = std::min(start.X + offset.X, start.X + start.W - MinFloatingWidth);
			bounds.W = start.X + start.W - bounds.X;
		} else if (_resizeEdges & UiResizeEdges::Right) {
			bounds.W = std::max(MinFloatingWidth, start.W + offset.X);
		}
		if (_resizeEdges & UiResizeEdges::Top) {
			bounds.Y = std::min(start.Y + offset.Y, start.Y + start.H - MinFloatingHeight);
			bounds.H = start.Y + start.H - bounds.Y;
		} else if (_resizeEdges & UiResizeEdges::Bottom) {
			bounds.H = std::max(MinFloatingHeight, start.H + offset.Y);
		}
		FloatingBounds = bounds;
	}

	void UiDockFrame::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (_resizeEdges != 0) {
			_resizeEdges = 0;
			NotifyChanged();
		}
	}
}

#endif
