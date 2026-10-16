#include "UiSurface.h"

#if defined(WITH_EDITOR)

#include "../../ContentResolver.h"
#include "../../Rendering/PolylineRenderer.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Graphics/IGfxDevice.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		// How long the pointer has to rest over an element before its tooltip is shown, in frames at 60 FPS
		constexpr float TooltipDelay = 36.0f;
		// How long a toast is shown, in frames at 60 FPS
		constexpr float ToastDuration = 180.0f;
		constexpr float ToastFadeTime = 20.0f;
		constexpr std::int32_t MaxToasts = 4;
		// Slots of the interface: the view of the level (what is over the level, what the tool shows over that, and
		// the ring of the play transition), the docked panels, then whatever is shown over them
		constexpr std::int32_t RootSlot = 3;
		constexpr std::int32_t FirstOverlaySlot = RootSlot + UiSurface::RootSlots;
		// Most milliseconds between the clicks of a double click
		constexpr std::int64_t DoubleClickTime = 450;
	}

	UiSurface* UiSurface::_instance = nullptr;

	UiSurface::UiSurface()
		: _scale(1.0f), _hover(nullptr), _capture(nullptr), _focus(nullptr), _tooltipTime(0.0f),
			_tooltipShown(false), _lastClickTime(0), _lastClickElement(nullptr)
	{
		_instance = this;
		_lines = std::make_unique<Rendering::PolylineRenderer>(1 << 18);
		OrderedDraws = true;
		_skin.Build(_scale);
	}

	UiSurface::~UiSurface()
	{
		// The elements are destroyed while the surface is still known, so they don't reach a dangling one
		_overlays.clear();
		_root = nullptr;
		if (_instance == this) {
			_instance = nullptr;
		}
	}

	UiElement* UiSurface::SetRoot(std::unique_ptr<UiElement> root)
	{
		_root = std::move(root);
		_root->_surface = this;
		return _root.get();
	}

	UiElement* UiSurface::PushOverlay(std::unique_ptr<UiElement> element, UiOverlayOptions&& options)
	{
		Overlay& overlay = _overlays.emplace_back();
		overlay.Element = std::move(element);
		overlay.Element->_surface = this;
		overlay.Options = std::move(options);
		return overlay.Element.get();
	}

	void UiSurface::CloseOverlay(UiElement* element)
	{
		for (Overlay& overlay : _overlays) {
			if (overlay.Element.get() == element && !overlay.Closing) {
				overlay.Closing = true;
				if (overlay.Options.OnClosed) {
					overlay.Options.OnClosed();
				}
				break;
			}
		}
	}

	void UiSurface::MoveOverlay(UiElement* element, Vector2f position)
	{
		for (Overlay& overlay : _overlays) {
			if (overlay.Element.get() == element) {
				// Kept as it is, it's only placed on the screen when it's drawn, so a window dragged past the edge stops
				// there and comes back with the pointer, not ahead of it
				overlay.Options.Centered = false;
				overlay.Options.Position = Vector2f(std::round(position.X), std::round(position.Y));
				break;
			}
		}
	}

	void UiSurface::ClosePopups()
	{
		for (std::size_t i = 0; i < _overlays.size(); i++) {
			if (!_overlays[i].Options.Modal) {
				CloseOverlay(_overlays[i].Element.get());
			}
		}
	}

	bool UiSurface::HasModal() const
	{
		for (const Overlay& overlay : _overlays) {
			if (overlay.Options.Modal && !overlay.Closing) {
				return true;
			}
		}
		return false;
	}

	bool UiSurface::HasOverlays() const
	{
		for (const Overlay& overlay : _overlays) {
			if (!overlay.Closing) {
				return true;
			}
		}
		return false;
	}

	void UiSurface::SetScale(float scale)
	{
		_scale = std::clamp(scale, 0.5f, 8.0f);
		_skin.Build(_scale);
	}

	UI::Font* UiSurface::GetFont() const
	{
		UI::Font* font = UiContext::GetFont();
		if (font != nullptr) {
			// Text drawn at a whole multiple of its size is crisp only if it's not smoothed
			float textScale = _scale * UiTheme::TextScale;
			bool integral = (std::abs(textScale - std::round(textScale)) < 0.01f);
			font->SetMagnificationFilter(integral ? SamplerFilter::Nearest : SamplerFilter::Linear);
		}
		return font;
	}

	UiContext UiSurface::GetContext() const
	{
		return UiContext{ _scale };
	}

	Vector2f UiSurface::GetScreenSize() const
	{
		IGfxDevice& gfxDevice = theApplication().GetGfxDevice();
		return Vector2f(float(gfxDevice.drawableWidth()), float(gfxDevice.drawableHeight()));
	}

	bool UiSurface::OnPointerDown(const UiPointerEvent& event)
	{
		_pointerPos = event.Pos;
		_tooltipTime = 0.0f;
		_tooltipShown = false;

		// A click outside of the elements shown over the interface closes those that want it, from the top down to the
		// one clicked into, so a list dropped down from a dialog closes on a click anywhere else in the dialog too
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			Overlay& overlay = _overlays[i - 1];
			if (overlay.Closing) {
				continue;
			}
			if (overlay.Element->Bounds.Contains(event.Pos) || overlay.Options.Modal) {
				break;
			}
			if (overlay.Options.CloseOnOutsideClick) {
				CloseOverlay(overlay.Element.get());
			}
		}

		// A window clicked into comes in front of the other windows, what pops up and closes by itself stays in front
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			Overlay& overlay = _overlays[i - 1];
			if (overlay.Closing) {
				continue;
			}
			if (overlay.Element->Bounds.Contains(event.Pos)) {
				if (!overlay.Options.Modal && !overlay.Options.CloseOnOutsideClick && i < _overlays.size()) {
					Overlay raised = std::move(overlay);
					_overlays.erase(_overlays.begin() + (i - 1));
					_overlays.push_back(std::move(raised));
				}
				break;
			}
			if (overlay.Options.Modal) {
				break;
			}
		}

		bool blocked;
		UiElement* hit = HitTestAll(event.Pos, blocked);
		if (blocked) {
			return true;
		}

		bool keepsFocus = false;
		for (UiElement* element = hit; element != nullptr && !keepsFocus; element = element->GetParent()) {
			keepsFocus = element->KeepsFocus();
		}
		if ((hit == nullptr || hit->IsFocusable() == false) && !keepsFocus) {
			// A click anywhere else finishes typing
			if (_focus != nullptr && hit != _focus) {
				SetFocus(nullptr);
			}
		}

		if (hit == nullptr) {
			return false;
		}

		if (hit->IsFocusable() && hit->IsUsable() && _focus != hit && !keepsFocus) {
			SetFocus(hit);
		}

		// A second click on the same element shortly after the first one is a double click
		UiPointerEvent clickEvent = event;
		std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		float slop = std::round(4.0f * _scale);
		if (hit == _lastClickElement && now - _lastClickTime < DoubleClickTime && (event.Pos - _lastClickPos).SqrLength() < slop * slop) {
			clickEvent.Clicks = 2;
			_lastClickElement = nullptr;
		} else {
			_lastClickElement = hit;
		}
		_lastClickTime = now;
		_lastClickPos = event.Pos;

		for (UiElement* parent = hit->GetParent(); parent != nullptr; parent = parent->GetParent()) {
			parent->OnPointerDownInside(clickEvent);
		}
		if (hit->IsUsable() && hit->OnPointerDown(clickEvent)) {
			_capture = hit;
			_capture->_pressed = true;
		}
		return true;
	}

	bool UiSurface::OnPointerMove(const UiPointerEvent& event)
	{
		if ((event.Pos - _pointerPos).SqrLength() > 4.0f) {
			_tooltipTime = 0.0f;
			_tooltipShown = false;
		}
		_pointerPos = event.Pos;

		bool blocked;
		UiElement* hit = HitTestAll(event.Pos, blocked);
		SetHover(_capture != nullptr ? (hit == _capture ? hit : nullptr) : hit);

		if (_capture != nullptr) {
			_capture->OnPointerMove(event);
			return true;
		}
		return (hit != nullptr || blocked);
	}

	bool UiSurface::OnPointerUp(const UiPointerEvent& event)
	{
		_pointerPos = event.Pos;
		if (_capture == nullptr) {
			bool blocked;
			return (HitTestAll(event.Pos, blocked) != nullptr || blocked);
		}

		UiElement* capture = _capture;
		_capture = nullptr;
		capture->_pressed = false;
		bool inside = capture->Bounds.Contains(event.Pos);
		capture->OnPointerUp(event, inside);
		return true;
	}

	bool UiSurface::OnWheel(const UiPointerEvent& event)
	{
		bool blocked;
		UiElement* hit = HitTestAll(event.Pos, blocked);
		if (blocked) {
			return true;
		}
		for (UiElement* element = hit; element != nullptr; element = element->GetParent()) {
			if (element->IsUsable() && element->OnWheel(event)) {
				return true;
			}
		}
		return (hit != nullptr);
	}

	bool UiSurface::OnKeyPressed(const KeyboardEvent& event)
	{
		if (_focus != nullptr && _focus->OnKeyPressed(event)) {
			return true;
		}
		if (event.sym == Keys::Escape) {
			for (std::size_t i = _overlays.size(); i > 0; i--) {
				Overlay& overlay = _overlays[i - 1];
				if (!overlay.Closing) {
					// The top element gets a chance to handle it first, a dialog cancels itself
					if (!overlay.Element->OnKeyPressed(event)) {
						CloseOverlay(overlay.Element.get());
					}
					return true;
				}
			}
		}
		// The top overlay gets the keys, a dialog confirms itself by Enter
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			Overlay& overlay = _overlays[i - 1];
			if (!overlay.Closing) {
				if (overlay.Element->OnKeyPressed(event)) {
					return true;
				}
				return overlay.Options.Modal;
			}
		}
		return false;
	}

	bool UiSurface::OnTextInput(const TextInputEvent& event)
	{
		return (_focus != nullptr && _focus->OnTextInput(event));
	}

	bool UiSurface::IsOverInterface(Vector2f pos)
	{
		bool blocked;
		return (HitTestAll(pos, blocked) != nullptr || blocked);
	}

	void UiSurface::SetFocus(UiElement* element)
	{
		if (_focus == element) {
			return;
		}
		UiElement* previous = _focus;
		_focus = element;
		if (previous != nullptr) {
			previous->_focused = false;
			previous->OnFocusChanged(false);
		}
		if (_focus != nullptr) {
			_focus->_focused = true;
			_focus->OnFocusChanged(true);
		}
	}

	void UiSurface::ShowToast(StringView text, EditorIcon icon)
	{
		if (_toasts.size() >= MaxToasts) {
			_toasts.erase(_toasts.begin());
		}
		Toast& toast = _toasts.emplace_back();
		toast.Text = text;
		toast.Icon = icon;
		toast.TimeLeft = ToastDuration;
	}

	void UiSurface::NotifyDestroyed(UiElement* element)
	{
		if (_instance != nullptr) {
			_instance->Forget(element);
		}
	}

	void UiSurface::OnUpdate(float timeMult)
	{
		UI::Canvas::OnUpdate(timeMult);

		RemoveClosedOverlays();

		if (_root != nullptr) {
			_root->Update(timeMult);
		}
		for (Overlay& overlay : _overlays) {
			overlay.Element->Update(timeMult);
		}

		if (_hover != nullptr && _capture == nullptr && !_tooltipShown) {
			_tooltipTime += timeMult;
			if (_tooltipTime >= TooltipDelay) {
				_tooltipShown = true;
			}
		}

		for (std::size_t i = 0; i < _toasts.size(); ) {
			_toasts[i].TimeLeft -= timeMult;
			if (_toasts[i].TimeLeft <= 0.0f) {
				_toasts.erase(_toasts.begin() + i);
			} else {
				i++;
			}
		}
	}

	bool UiSurface::OnDraw(RenderQueue& renderQueue)
	{
		UI::Canvas::OnDraw(renderQueue);

		Vector2f screenSize = GetScreenSize();
		ViewSize = Vector2i(std::int32_t(screenSize.X), std::int32_t(screenSize.Y));
		ClipRect = Rectf(0.0f, 0.0f, 0.0f, 0.0f);

		UiContext context = GetContext();
		_lines->BeginFrame();
		UiPainter painter(*this, _skin, GetFont(), _scale, _lines.get());

		if (_root != nullptr && _root->IsShown()) {
			_root->Arrange(context, Rectf(0.0f, 0.0f, screenSize.X, screenSize.Y));
			painter.SetSlot(RootSlot);
			_root->Draw(painter);
		}

		std::int32_t slot = FirstOverlaySlot;
		for (Overlay& overlay : _overlays) {
			if (overlay.Closing) {
				continue;
			}

			UiElement* element = overlay.Element.get();
			Vector2f size = element->Measure(context, screenSize);
			size.X = std::min(size.X, screenSize.X);
			size.Y = std::min(size.Y, screenSize.Y);
			Vector2f pos = (overlay.Options.Centered
				? Vector2f(std::round((screenSize.X - size.X) * 0.5f), std::round((screenSize.Y - size.Y) * 0.4f))
				: overlay.Options.Position);
			// Moved to fit the screen
			pos.X = std::clamp(pos.X, 0.0f, screenSize.X - size.X);
			pos.Y = std::clamp(pos.Y, 0.0f, screenSize.Y - size.Y);
			element->Arrange(context, Rectf(pos.X, pos.Y, size.X, size.Y));

			painter.SetSlot(slot++);
			if (overlay.Options.Modal) {
				painter.FillRect(Rectf(0.0f, 0.0f, screenSize.X, screenSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.45f), UiLayer::Back);
			}
			element->Draw(painter);
		}

		painter.SetSlot(slot);
		DrawToasts(painter);
		if (_tooltipShown && _hover != nullptr && _capture == nullptr) {
			DrawTooltip(painter);
		}

		if (DrawOver) {
			painter.SetSlot(slot + 1);
			DrawOver(painter, screenSize);
		}

		bool cursorHidden = false;
		for (const Overlay& overlay : _overlays) {
			if (!overlay.Closing && overlay.Options.HidesVirtualCursor) {
				cursorHidden = true;
				break;
			}
		}
		if (ShowVirtualCursor && !cursorHidden) {
			// A ring over everything, with a dot where it points
			painter.SetSlot(slot + 3);
			float radius = std::max(5.0f, std::round(_scale * 6.0f));
			painter.Circle(VirtualCursor, radius, Colorf(1.0f, 1.0f, 1.0f, 0.9f), UiLayer::Content);
			painter.Circle(VirtualCursor, radius - std::max(1.5f, std::round(_scale * 1.5f)), Colorf(0.1f, 0.08f, 0.2f, 0.6f), UiLayer::Overlay);
			painter.Circle(VirtualCursor, std::max(1.5f, std::round(_scale * 1.5f)), UiTheme::Accent, UiLayer::OverlayText);
		}

		return true;
	}

	UiElement* UiSurface::HitTestAll(Vector2f pos, bool& blocked)
	{
		blocked = false;
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			Overlay& overlay = _overlays[i - 1];
			if (overlay.Closing) {
				continue;
			}
			if (UiElement* hit = overlay.Element->HitTest(pos)) {
				return hit;
			}
			if (overlay.Element->Bounds.Contains(pos)) {
				blocked = true;
				return nullptr;
			}
			if (overlay.Options.Modal) {
				blocked = true;
				return nullptr;
			}
		}
		return (_root != nullptr ? _root->HitTest(pos) : nullptr);
	}

	void UiSurface::SetHover(UiElement* element)
	{
		if (_hover == element) {
			return;
		}
		if (_hover != nullptr) {
			_hover->_hovered = false;
		}
		_hover = element;
		_tooltipTime = 0.0f;
		_tooltipShown = false;
		if (_hover != nullptr) {
			_hover->_hovered = true;
		}
	}

	void UiSurface::RemoveClosedOverlays()
	{
		for (std::size_t i = 0; i < _overlays.size(); ) {
			if (_overlays[i].Closing) {
				_overlays.erase(_overlays.begin() + i);
			} else {
				i++;
			}
		}
	}

	void UiSurface::Forget(UiElement* element)
	{
		if (_hover == element) {
			_hover = nullptr;
		}
		if (_capture == element) {
			_capture = nullptr;
		}
		if (_lastClickElement == element) {
			_lastClickElement = nullptr;
		}
		if (_focus == element) {
			_focus = nullptr;
		}
	}

	void UiSurface::DrawTooltip(UiPainter& painter)
	{
		StringView text = _hover->GetTooltip(_pointerPos);
		if (text.empty()) {
			return;
		}

		Vector2f screenSize = GetScreenSize();
		float padding = painter.Px(UiTheme::Padding * 1.5f);
		// As wide as its longest line and as high as all of its lines, a tooltip may have more of them
		std::int32_t lineCount = 1;
		for (char c : text) {
			if (c == '\n') {
				lineCount++;
			}
		}
		float lineHeight = std::max(painter.GetTextHeight(), painter.Px(UiTheme::RowHeight) * 0.75f);
		Vector2f size(painter.MeasureText(text) + padding * 2.0f, lineHeight * float(lineCount) + padding * 2.0f);
		Vector2f pos(_pointerPos.X + painter.Px(8.0f), _pointerPos.Y + painter.Px(14.0f));
		if (pos.X + size.X > screenSize.X) {
			pos.X = std::max(screenSize.X - size.X, 0.0f);
		}
		if (pos.Y + size.Y > screenSize.Y) {
			pos.Y = _pointerPos.Y - size.Y - painter.Px(4.0f);
		}
		Rectf rect(std::round(pos.X), std::round(pos.Y), std::round(size.X), std::round(size.Y));
		painter.Panel(rect, true, UiTheme::PanelHeader, UiTheme::PanelBorder);
		painter.TextInRect(text, Rectf(rect.X + padding, rect.Y, rect.W - padding * 2.0f, rect.H), UiTheme::Text, Alignment::Left);
	}

	void UiSurface::DrawToasts(UiPainter& painter)
	{
		Vector2f screenSize = GetScreenSize();
		float padding = painter.Px(UiTheme::Padding * 1.5f);
		float iconSize = painter.Px(UiTheme::IconSize);
		float y = screenSize.Y - painter.Px(UiTheme::StatusBarHeight + 8.0f);

		for (std::size_t i = _toasts.size(); i > 0; i--) {
			const Toast& toast = _toasts[i - 1];
			float alpha = std::min(1.0f, toast.TimeLeft / ToastFadeTime);
			float age = ToastDuration - toast.TimeLeft;
			float slide = std::max(0.0f, 1.0f - age / ToastFadeTime);

			Vector2f size(padding * 3.0f + iconSize + painter.MeasureText(toast.Text), std::max(iconSize, painter.Px(UiTheme::RowHeight)) + padding * 2.0f);
			Rectf rect(std::round((screenSize.X - size.X) * 0.5f), std::round(y - size.Y + slide * painter.Px(12.0f)), std::round(size.X), std::round(size.Y));

			painter.PushOpacity(alpha);
			painter.Panel(rect, true, UiTheme::PanelHeader, UiTheme::PanelBorder);
			// A calm blue for a message, red for a warning
			Colorf iconColor = (toast.Icon == EditorIcon::Warning ? UiTheme::Danger : Colorf(0.55f, 0.75f, 1.0f, 1.0f));
			painter.Icon(toast.Icon, Rectf(rect.X + padding, rect.Y, iconSize, rect.H), iconColor);
			painter.TextInRect(toast.Text, Rectf(rect.X + padding * 2.0f + iconSize, rect.Y, rect.W, rect.H), UiTheme::Text);
			painter.PopOpacity();

			y = rect.Y - painter.Px(4.0f);
		}
	}
}

#endif
