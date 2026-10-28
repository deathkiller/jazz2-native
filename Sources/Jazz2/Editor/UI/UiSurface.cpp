#include "UiSurface.h"

#if defined(WITH_EDITOR)

#include "UiMenu.h"
#include "UiReadableFont.h"
#include "UiWidgets.h"
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
		// How long a finger has to rest on an element before its tooltip is shown, and how long the tooltip stays after
		// the finger is lifted, in frames at 60 FPS
		constexpr float TouchTooltipDelay = 30.0f;
		constexpr float TouchTooltipLinger = 120.0f;
		// How long a finger has to rest on an element that is dragged only after it's held, half a second as a long press
		// of the system takes, in frames at 60 FPS
		constexpr float TouchHoldDelay = 30.0f;
		// How far the middle of the cursor overlay is from the pointer, along both axes, and the gap below it to a label
		// shown by the pointer
		constexpr float CursorOverlayOffset = 21.0f;
		constexpr float CursorOverlayGap = 4.0f;
	}

	UiSurface* UiSurface::_instance = nullptr;

	UiSurface::UiSurface()
		: _scale(1.0f), _hover(nullptr), _capture(nullptr), _focus(nullptr), _tooltipTime(0.0f),
			_tooltipShown(false), _lastClickTime(0), _lastClickElement(nullptr), _touchDown(false), _touchTooltip(false),
			_tooltipLinger(0.0f), _touchScroll(nullptr), _touchScrolling(false), _touchScrollOrigin(0.0f), _touchVelocity(0.0f)
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
		while (!_retired.empty()) {
			SmallVector<std::unique_ptr<UiElement>, 0> retired = Death::move(_retired);
			_retired.clear();
		}
		if (_instance == this) {
			_instance = nullptr;
		}
		// The pixel font is shared with the game, which draws it smoothed
		if (UI::Font* font = ContentResolver::Get().GetFont(FontType::Small)) {
			font->SetMagnificationFilter(SamplerFilter::Linear);
		}
	}

	UiElement* UiSurface::SetRoot(std::unique_ptr<UiElement> root)
	{
		if (_root != nullptr) {
			Retire(Death::move(_root));
		}
		_root = Death::move(root);
		_root->_surface = this;
		return _root.get();
	}

	UiElement* UiSurface::PushOverlay(std::unique_ptr<UiElement> element, UiOverlayOptions&& options)
	{
		if (options.Modal && _focus != nullptr) {
			// Nothing under it can be typed into anymore, what was typed is confirmed now, before the element is added,
			// as confirming it may show something else
			SetFocus(nullptr);
		}
		Overlay& overlay = _overlays.emplace_back();
		overlay.Element = Death::move(element);
		overlay.Element->_surface = this;
		overlay.Options = Death::move(options);
		return overlay.Element.get();
	}

	void UiSurface::CloseOverlay(UiElement* element)
	{
		Function<void()> onClosed;
		bool closed = false;
		for (Overlay& overlay : _overlays) {
			if (overlay.Element.get() == element && !overlay.Closing) {
				overlay.Closing = true;
				onClosed = Death::move(overlay.Options.OnClosed);
				closed = true;
				break;
			}
		}
		if (closed) {
			// It's destroyed only at the end of the frame, a press held in it gets nothing more until then
			CancelCaptureInside(element);
			// What was shown from it goes with it, by index, as closing one may show something else
			for (std::size_t i = 0; i < _overlays.size(); i++) {
				if (_overlays[i].Options.Owner == element && !_overlays[i].Closing) {
					CloseOverlay(_overlays[i].Element.get());
				}
			}
		}
		// Called once the list isn't touched anymore, it may show something else, which can move the list in memory
		if (onClosed) {
			onClosed();
		}
	}

	void UiSurface::MoveOverlay(UiElement* element, Vector2f position, float fallbackRight)
	{
		for (Overlay& overlay : _overlays) {
			if (overlay.Element.get() == element) {
				// Kept as it is, it's only placed on the screen when it's drawn, so a window dragged past the edge stops
				// there and comes back with the pointer, not ahead of it
				overlay.Options.Centered = false;
				overlay.Options.Position = Vector2f(std::round(position.X), std::round(position.Y));
				overlay.Options.FallbackRight = (fallbackRight >= 0.0f ? std::round(fallbackRight) : fallbackRight);
				break;
			}
		}
	}

	bool UiSurface::RaiseOverlay(UiElement* element)
	{
		std::size_t index = _overlays.size();
		for (std::size_t i = 0; i < _overlays.size(); i++) {
			if (_overlays[i].Element.get() == element && !_overlays[i].Closing) {
				index = i;
				break;
			}
		}
		if (index == _overlays.size()) {
			return false;
		}
		// In front of the other windows, but not of anything modal shown after it, which would be covered then
		std::size_t target = _overlays.size() - 1;
		for (std::size_t i = index + 1; i < _overlays.size(); i++) {
			if (_overlays[i].Options.Modal && !_overlays[i].Closing) {
				target = i - 1;
				break;
			}
		}
		if (!_overlays[index].Options.Modal && target > index) {
			Overlay raised = Death::move(_overlays[index]);
			_overlays.erase(_overlays.begin() + index);
			_overlays.insert(_overlays.begin() + target, Death::move(raised));
		}
		if (_focus != nullptr && GetOverlayIndexOf(_focus) != GetOverlayIndexOf(element)) {
			SetFocus(nullptr);
		}
		return true;
	}

	void UiSurface::ClosePopups()
	{
		// Windows stay, a menu item or a choice in a list doesn't close the window it was picked in
		for (std::size_t i = 0; i < _overlays.size(); i++) {
			if (_overlays[i].Options.IsPopup()) {
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
			if (IsActive(overlay)) {
				return true;
			}
		}
		return false;
	}

	bool UiSurface::IsOverlayShown(const UiElement* element) const
	{
		for (const Overlay& overlay : _overlays) {
			if (overlay.Element.get() == element) {
				return !overlay.Closing;
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
		UI::Font* font = UiContext::GetFont(_scale);
		if (font != nullptr && font == ContentResolver::Get().GetFont(FontType::Small)) {
			// Text in the pixel font drawn at a whole multiple of its size is crisp only if it's not smoothed. It's set on
			// every font asked for, a font is loaded again with the default filter when the palette changes, and the
			// texture does nothing when the filter is the same. The readable font is drawn at its own size, a text made
			// smaller or larger stays smooth.
			font->SetMagnificationFilter(UiTheme::IsTextScaleWhole(_scale) ? SamplerFilter::Nearest : SamplerFilter::Linear);
		}
		return font;
	}

	UI::Font* UiSurface::GetReadableFont(float textScale)
	{
		if (_instance == nullptr) {
			return nullptr;
		}
		if (_instance->_readableFont == nullptr) {
			_instance->_readableFont = std::make_unique<UiReadableFont>();
		}
		return _instance->_readableFont->Get(textScale);
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

	Rectf UiSurface::GetSafeBounds() const
	{
		// Each edge a part of the side it's on, rounded as the game rounds its own margins
		Vector2f screen = GetScreenSize();
		float left = std::round(screen.X * std::clamp(SafeMargins.Left, 0.0f, 0.5f));
		float top = std::round(screen.Y * std::clamp(SafeMargins.Top, 0.0f, 0.5f));
		float right = std::round(screen.X * std::clamp(SafeMargins.Right, 0.0f, 0.5f));
		float bottom = std::round(screen.Y * std::clamp(SafeMargins.Bottom, 0.0f, 0.5f));
		return Rectf(left, top, std::max(screen.X - left - right, 1.0f), std::max(screen.Y - top - bottom, 1.0f));
	}

	bool UiSurface::OnPointerDown(const UiPointerEvent& event)
	{
		_pointerPos = event.Pos;
		_tooltipTime = 0.0f;
		_tooltipShown = false;
		_tooltipLinger = 0.0f;
		_touchDown = false;
		_touchStart = event.Pos;
		_touchLastPos = event.Pos;
		_touchTooltip = false;
		_touchScroll = nullptr;
		_touchScrolling = false;
		_touchVelocity = 0.0f;
		_touchHold = TouchHold::None;
		_touchHoldTime = 0.0f;

		// A click outside of the elements shown over the interface closes those that want it, from the top down to the
		// one clicked into, so a list dropped down from a dialog closes on a click anywhere else in the dialog too
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			Overlay& overlay = _overlays[i - 1];
			if (!IsActive(overlay)) {
				continue;
			}
			// With the edges a window is resized by, which reach a little outside of it
			if (overlay.Element->GetHitBounds().Contains(event.Pos) || overlay.Options.Modal) {
				break;
			}
			if (overlay.Options.CloseOnOutsideClick) {
				CloseOverlay(overlay.Element.get());
			}
		}

		// A window clicked into comes in front of the other windows, what pops up and closes by itself stays in front
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			Overlay& overlay = _overlays[i - 1];
			if (!IsActive(overlay)) {
				continue;
			}
			if (overlay.Element->GetHitBounds().Contains(event.Pos)) {
				if (!overlay.Options.Modal && !overlay.Options.CloseOnOutsideClick && i < _overlays.size()) {
					Overlay raised = Death::move(overlay);
					_overlays.erase(_overlays.begin() + (i - 1));
					_overlays.push_back(Death::move(raised));
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
				// What the text was confirmed into may have rebuilt a part of the interface, the element under the
				// pointer is looked for again
				hit = HitTestAll(event.Pos, blocked);
				if (blocked) {
					return true;
				}
			}
		}

		if (hit == nullptr) {
			return false;
		}

		if (TouchInput) {
			_touchDown = true;
			// A finger dragged from here may scroll the list it's in
			for (UiElement* element = hit; element != nullptr; element = element->GetParent()) {
				if (UiScrollView* scrollView = runtime_cast<UiScrollView>(element)) {
					if (scrollView->IsScrollable()) {
						// A list still moving from a flick stops under the finger
						scrollView->ScrollTo(scrollView->GetScroll(), true);
						_touchScroll = scrollView;
						_touchScrollOrigin = scrollView->GetScroll();
						_touchLastTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
						break;
					}
				}
			}
		}

		if (hit->IsFocusable() && hit->IsUsable() && _focus != hit && !keepsFocus) {
			SetFocus(hit);
			if (hit->GetSurface() != this) {
				// What the text typed before was confirmed into rebuilt the part of the interface the pointer is over, the
				// element under it is looked for again, the focus was taken from the text already
				hit = HitTestAll(event.Pos, blocked);
				if (blocked) {
					return true;
				}
				if (hit == nullptr) {
					return false;
				}
				if (hit->IsFocusable() && hit->IsUsable() && _focus != hit) {
					SetFocus(hit);
					if (hit->GetSurface() != this) {
						return true;
					}
				}
			}
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
			// A finger drags it only once it rested on it, moved at once it scrolls the list instead
			if (_touchDown && _capture->DragsAfterHold()) {
				_touchHold = TouchHold::Waiting;
			}
		}
		return true;
	}

	void UiSurface::CancelPointerCapture()
	{
		_touchHold = TouchHold::None;
		if (_capture == nullptr) {
			return;
		}
		// As when a finger starts to scroll a list, see OnPointerMove()
		UiElement* capture = _capture;
		_capture = nullptr;
		capture->_pressed = false;
		capture->OnPointerCancel();
	}

	void UiSurface::DismissTooltip()
	{
		if (_hover != nullptr) {
			_hover->_hovered = false;
			_hover = nullptr;
		}
		_tooltipTime = 0.0f;
		_tooltipShown = false;
		_tooltipLinger = 0.0f;
	}

	void UiSurface::TrackPointer(Vector2f pos)
	{
		// A finger always trembles a little, the moves themselves decide when its tooltip goes away
		if (!_touchDown && (pos - _pointerPos).SqrLength() > 4.0f) {
			_tooltipTime = 0.0f;
			_tooltipShown = false;
		}
		_pointerPos = pos;
	}

	bool UiSurface::OnPointerMove(const UiPointerEvent& event)
	{
		if (_touchDown) {
			// A finger always trembles a little, the tooltip waits until it moved away from where it was put down
			if ((event.Pos - _touchStart).SqrLength() > GetTouchSlop() * GetTouchSlop() && !_touchTooltip) {
				_tooltipTime = 0.0f;
				_tooltipShown = false;
			}
		} else if ((event.Pos - _pointerPos).SqrLength() > 4.0f) {
			_tooltipTime = 0.0f;
			_tooltipShown = false;
		}
		_pointerPos = event.Pos;

		if (_touchDown && _touchScroll != nullptr) {
			// A finger dragged up or down over a list scrolls it, what it pressed is let go, unless it's dragged itself
			Vector2f moved = event.Pos - _touchStart;
			if (!_touchScrolling && std::abs(moved.Y) > GetTouchSlop() && std::abs(moved.Y) > std::abs(moved.X) &&
				(_capture == nullptr || _capture->YieldsToScroll() || _touchHold == TouchHold::Waiting)) {
				_touchScrolling = true;
				_touchHold = TouchHold::None;
				_tooltipShown = false;
				_touchTooltip = false;
				if (_capture != nullptr) {
					UiElement* capture = _capture;
					_capture = nullptr;
					capture->_pressed = false;
					capture->OnPointerCancel();
				}
				// From here on, so the list doesn't jump by the distance it took to tell it's a drag
				_touchStart = event.Pos;
				if (_touchScroll != nullptr) {
					_touchScrollOrigin = _touchScroll->GetScroll();
				}
			}
			if (_touchScrolling) {
				// The speed in pixels a frame at 60 FPS, by the time between the events, a backend may send several in a frame
				std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
				float frames = std::max(float(now - _touchLastTime) / (1000.0f / 60.0f), 0.5f);
				_touchVelocity = _touchVelocity * 0.4f + ((event.Pos.Y - _touchLastPos.Y) / frames) * 0.6f;
				_touchLastTime = now;
				_touchLastPos = event.Pos;
				if (_touchScroll != nullptr) {
					_touchScroll->ScrollTo(_touchScrollOrigin - (event.Pos.Y - _touchStart.Y), true);
				}
				SetHover(nullptr);
				return true;
			}
		}
		_touchLastPos = event.Pos;

		if (_touchHold == TouchHold::Waiting) {
			if ((event.Pos - _touchStart).SqrLength() <= GetTouchSlop() * GetTouchSlop()) {
				// Until it's held, a finger trembling on it doesn't move it
				return true;
			}
			// Moved away before it was held, so it's neither dragged nor clicked when the finger is lifted
			CancelPointerCapture();
		}

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
		bool wasTouch = _touchDown;
		bool touchTooltip = (_touchDown && _touchTooltip && _tooltipShown);
		_touchDown = false;
		_touchHold = TouchHold::None;

		if (_touchScrolling) {
			// A flick goes on for a while, unless the finger stood still before it was lifted
			_touchScrolling = false;
			std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
			if (_touchScroll != nullptr && now - _touchLastTime < 100) {
				_touchScroll->Fling(_touchVelocity);
			}
			_touchScroll = nullptr;
			SetHover(nullptr);
			return true;
		}
		_touchScroll = nullptr;

		bool handled;
		if (_capture == nullptr) {
			bool blocked;
			handled = (HitTestAll(event.Pos, blocked) != nullptr || blocked);
		} else {
			UiElement* capture = _capture;
			_capture = nullptr;
			capture->_pressed = false;
			// A finger held still to see the tooltip doesn't click what it was on; an element may take the pointer a little
			// outside of itself, as the cross of a window
			bool inside = (!touchTooltip && capture->GetHitBounds().Contains(event.Pos));
			capture->OnPointerUp(event, inside);
			handled = true;
		}

		if (wasTouch) {
			// Nothing is pointed at once the finger is lifted, a tooltip shown by holding it stays for a moment
			if (touchTooltip) {
				_tooltipLinger = TouchTooltipLinger;
			} else {
				SetHover(nullptr);
			}
		}
		return handled;
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
		// The focus gets the keys only if nothing modal is shown over it
		UiElement* focus = (_focus != nullptr && AcceptsInput(_focus) ? _focus : nullptr);
		std::int32_t focusOverlay = (focus != nullptr ? GetOverlayIndexOf(focus) : -2);
		if (focus != nullptr && IsPassThroughKey && runtime_cast<UiTextBox>(focus) != nullptr && IsPassThroughKey(event)) {
			// A shortcut that works while typing, such as saving, confirms the text first, so it's saved too
			SetFocus(nullptr);
			return false;
		}
		if (focus != nullptr && focus->OnKeyPressed(event)) {
			return true;
		}
		bool alt = (event.mod & KeyMod::Alt) != 0;
		bool ctrl = (event.mod & KeyMod::Ctrl) != 0;
		if (event.sym == Keys::Tab && !alt && !ctrl) {
			if (MoveFocus((event.mod & KeyMod::Shift) != 0)) {
				return true;
			}
		}
		if (event.sym == Keys::Escape) {
			if (focus != nullptr && _focus != focus) {
				// Typing was cancelled by it, which is all it does, unless the box is in a dialog, which is cancelled by the
				// same key --- an edit shown over a dialog closed itself, the dialog stays
				if (focusOverlay < 0 || focusOverlay >= std::int32_t(_overlays.size()) || !IsActive(_overlays[focusOverlay])) {
					return true;
				}
			}
			std::int32_t focusIndex = (_focus != nullptr ? GetOverlayIndexOf(_focus) : -2);
			for (std::size_t i = _overlays.size(); i > 0; i--) {
				const Overlay& overlay = _overlays[i - 1];
				if (!IsActive(overlay)) {
					continue;
				}
				// A window that isn't modal is closed only while it's used, while the focus is in it or the pointer is
				// over it, the key is meant for what is under it otherwise, as a selection of the level; what is modal
				// and what pops up and closes by itself always get it
				if (!overlay.Options.Modal && !overlay.Options.IsPopup() && focusIndex != std::int32_t(i - 1) &&
					!overlay.Element->GetHitBounds().Contains(_pointerPos)) {
					continue;
				}
				// The top element gets a chance to handle it first, a dialog cancels itself
				UiElement* element = overlay.Element.get();
				if (!element->OnKeyPressed(event)) {
					CloseOverlay(element);
				}
				return true;
			}
			if (focus != nullptr && focusOverlay == -1) {
				// A control of the panels reached by Tab is let go, the keys go to the level again
				SetFocus(nullptr);
				return true;
			}
		}
		// The top overlay gets the keys, a dialog confirms itself by Enter
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			if (!IsActive(_overlays[i - 1])) {
				continue;
			}
			bool modal = _overlays[i - 1].Options.Modal;
			if (_overlays[i - 1].Element->OnKeyPressed(event)) {
				return true;
			}
			if (modal) {
				return true;
			}
			break;
		}
		if (HasModal()) {
			// Nothing under what is modal gets the keys, also while something pops up over it, as a list dropped down
			// from a dialog, the menu bar neither
			return true;
		}
		// Alt with a letter opens a menu of the menu bar, by the letter it has underlined
		if (alt && !ctrl && event.sym >= Keys::A && event.sym <= Keys::Z) {
			if (UiMenuBar* menuBar = FindMenuBar(_root.get())) {
				if (menuBar->OpenByMnemonic(char('a' + (std::int32_t(event.sym) - std::int32_t(Keys::A))))) {
					return true;
				}
			}
		}
		return false;
	}

	bool UiSurface::OnTextInput(const TextInputEvent& event)
	{
		if (_focus != nullptr && AcceptsInput(_focus)) {
			if (_focus->OnTextInput(event)) {
				return true;
			}
			if (GetOverlayIndexOf(_focus) < 0) {
				return false;
			}
		}
		// Without a focus that takes it, a list or a menu shown over the interface finds what is typed
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			if (IsActive(_overlays[i - 1])) {
				return _overlays[i - 1].Element->OnTextInput(event);
			}
		}
		return false;
	}

	UiMenuBar* UiSurface::FindMenuBar(UiElement* element)
	{
		if (element == nullptr || !element->IsShown()) {
			return nullptr;
		}
		if (UiMenuBar* menuBar = runtime_cast<UiMenuBar>(element)) {
			return menuBar;
		}
		for (std::int32_t i = 0; i < element->GetChildCount(); i++) {
			if (UiMenuBar* menuBar = FindMenuBar(element->GetChild(i))) {
				return menuBar;
			}
		}
		return nullptr;
	}

	UiElement* UiSurface::GetFocusScope() const
	{
		// Only inside of what is modal, otherwise where the focus is, or the top element shown over the interface
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			if (IsActive(_overlays[i - 1]) && _overlays[i - 1].Options.Modal) {
				return _overlays[i - 1].Element.get();
			}
		}
		if (_focus != nullptr && AcceptsInput(_focus)) {
			UiElement* top = _focus;
			while (top->GetParent() != nullptr) {
				top = top->GetParent();
			}
			return top;
		}
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			if (IsActive(_overlays[i - 1])) {
				return _overlays[i - 1].Element.get();
			}
		}
		return _root.get();
	}

	void UiSurface::CollectTabStops(UiElement* element, const Rectf& screen, bool scrolled, SmallVectorImpl<UiElement*>& stops)
	{
		if (!element->IsShown()) {
			return;
		}
		// What is off the screen isn't reached, such as a closed drawer, unless it's only scrolled away in a list that is
		// on the screen, a field of a long form is reached and scrolled to then
		const Rectf& bounds = element->Bounds;
		bool hasRoom = (bounds.W > 0.0f && bounds.H > 0.0f);
		bool onScreen = (hasRoom && bounds.X < screen.X + screen.W && bounds.X + bounds.W > screen.X &&
			bounds.Y < screen.Y + screen.H && bounds.Y + bounds.H > screen.Y);
		if (element->IsTabStop() && element->IsUsable() && ((hasRoom && (onScreen || scrolled)) || element == _focus)) {
			stops.push_back(element);
		}
		if (!scrolled && onScreen && runtime_cast<UiScrollView>(element) != nullptr) {
			scrolled = true;
		}
		for (std::int32_t i = 0; i < element->GetChildCount(); i++) {
			CollectTabStops(element->GetChild(i), screen, scrolled, stops);
		}
	}

	bool UiSurface::MoveFocus(bool backwards)
	{
		UiElement* scope = GetFocusScope();
		if (scope == nullptr) {
			return false;
		}
		Vector2f screenSize = GetScreenSize();
		SmallVector<UiElement*, 32> stops;
		CollectTabStops(scope, Rectf(0.0f, 0.0f, screenSize.X, screenSize.Y), false, stops);
		if (stops.empty()) {
			return false;
		}
		std::int32_t index = -1;
		for (std::size_t i = 0; i < stops.size(); i++) {
			if (stops[i] == _focus) {
				index = std::int32_t(i);
				break;
			}
		}
		std::int32_t count = std::int32_t(stops.size());
		std::int32_t next = (index < 0 ? (backwards ? count - 1 : 0) : (index + (backwards ? count - 1 : 1)) % count);
		UiElement* element = stops[next];
		SetFocus(element);
		if (_focus == element) {
			if (UiTextBox* textBox = runtime_cast<UiTextBox>(element)) {
				// All of its text, typing replaces it, as in a dialog of the system
				textBox->BeginEdit();
			}
			EnsureVisible(element);
		}
		return true;
	}

	void UiSurface::EnsureVisible(UiElement* element)
	{
		for (UiElement* parent = (element != nullptr ? element->GetParent() : nullptr); parent != nullptr; parent = parent->GetParent()) {
			if (UiScrollView* scrollView = runtime_cast<UiScrollView>(parent)) {
				if (UiElement* content = scrollView->GetContent()) {
					float top = element->Bounds.Y - content->Bounds.Y;
					scrollView->ScrollIntoView(top, top + element->Bounds.H);
				}
				return;
			}
		}
	}

	bool UiSurface::IsActive(const Overlay& overlay) const
	{
		if (overlay.Closing) {
			return false;
		}
		return !(WindowsHidden && !overlay.Options.Modal && !overlay.Options.IsPopup());
	}

	std::int32_t UiSurface::GetOverlayIndexOf(UiElement* element) const
	{
		if (element == nullptr) {
			return -2;
		}
		UiElement* top = element;
		while (top->GetParent() != nullptr) {
			top = top->GetParent();
		}
		if (top == _root.get()) {
			return -1;
		}
		for (std::size_t i = 0; i < _overlays.size(); i++) {
			if (_overlays[i].Element.get() == top) {
				return std::int32_t(i);
			}
		}
		return -2;
	}

	bool UiSurface::AcceptsInput(UiElement* element) const
	{
		std::int32_t index = GetOverlayIndexOf(element);
		if (index == -2 || (index >= 0 && !IsActive(_overlays[index]))) {
			return false;
		}
		for (std::size_t i = std::size_t(index + 1); i < _overlays.size(); i++) {
			if (_overlays[i].Options.Modal && IsActive(_overlays[i])) {
				return false;
			}
		}
		return true;
	}

	float UiSurface::GetTouchSlop() const
	{
		return std::round(10.0f * std::max(_scale, 1.0f));
	}

	bool UiSurface::IsOverInterface(Vector2f pos)
	{
		bool blocked;
		return (HitTestAll(pos, blocked) != nullptr || blocked);
	}

	bool UiSurface::TakesTouchAt(Vector2f pos)
	{
		bool blocked;
		UiElement* hit = HitTestAll(pos, blocked);
		return (hit != nullptr ? !hit->YieldsToViewGestures() : blocked);
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

	void UiSurface::SetCursorOverlay(UiCursorOverlay overlay)
	{
		_cursorOverlay = overlay;
	}

	UiCursorOverlay UiSurface::GetResizeOverlay(bool left, bool top, bool right, bool bottom)
	{
		bool horizontal = (left || right), vertical = (top || bottom);
		if (horizontal && vertical) {
			return ((left && top) || (right && bottom) ? UiCursorOverlay::ResizeDiagonalNWSE : UiCursorOverlay::ResizeDiagonalNESW);
		}
		if (horizontal) {
			return UiCursorOverlay::ResizeHorizontal;
		}
		if (vertical) {
			return UiCursorOverlay::ResizeVertical;
		}
		return UiCursorOverlay::None;
	}

	float UiSurface::GetPointerLabelOffset(float distance) const
	{
		if (_cursorOverlay == UiCursorOverlay::None || TouchInput) {
			return distance;
		}
		return std::max(distance, std::round(_scale * CursorOverlayOffset) + float(_skin.GetIconSize()) * 0.5f + std::round(_scale * CursorOverlayGap));
	}

	void UiSurface::SetPointerTooltip(StringView text)
	{
		_pointerTooltip = text;
	}

	void UiSurface::DrawCursorOverlay(UiPainter& painter)
	{
		EditorIcon icon;
		switch (_cursorOverlay) {
			case UiCursorOverlay::ResizeHorizontal: icon = EditorIcon::CursorResizeHorizontal; break;
			case UiCursorOverlay::ResizeVertical: icon = EditorIcon::CursorResizeVertical; break;
			case UiCursorOverlay::ResizeDiagonalNWSE: icon = EditorIcon::CursorResizeNWSE; break;
			case UiCursorOverlay::ResizeDiagonalNESW: icon = EditorIcon::CursorResizeNESW; break;
			case UiCursorOverlay::Move: icon = EditorIcon::CursorMove; break;
			default: return;
		}

		// Below and to the right of the pointer, out of the way of what it points at, on the other side near the edge
		// of the screen; on whole pixels, so the pixel art stays sharp
		float size = float(_skin.GetIconSize());
		float offset = painter.Px(CursorOverlayOffset);
		Vector2f screenSize = GetScreenSize();
		float x = _pointerPos.X + offset - size * 0.5f;
		float y = _pointerPos.Y + offset - size * 0.5f;
		if (x + size > screenSize.X) {
			// The arrow of the system points up and to the left from its tip, nothing of it is on this side, so closer
			x = _pointerPos.X - offset * 0.5625f - size * 0.5f;
		}
		if (y + size > screenSize.Y) {
			y = _pointerPos.Y - offset - size * 0.5f;
		}
		Rectf rect(std::round(x), std::round(y), size, size);

		float pixel = std::max(1.0f, std::round(size / float(EditorIcons::Size)));

		// A soft shadow below and to the right, as the cursors of the system have: the shape with its outline, blurred, by
		// copies a pixel of the art apart that add up where they overlap, so it fades out at its edges
		Vector2f shadow(rect.X + pixel * 2.0f, rect.Y + pixel * 2.0f);
		for (std::int32_t dy = -2; dy <= 2; dy++) {
			for (std::int32_t dx = -2; dx <= 2; dx++) {
				painter.Icon(icon, Rectf(shadow.X + pixel * float(dx), shadow.Y + pixel * float(dy), size, size), Colorf(0.0f, 0.0f, 0.0f, 0.022f), UiLayer::Back);
			}
		}

		// A dark outline a pixel of the art wide all around, so it's seen on light and dark colors alike
		for (std::int32_t dy = -1; dy <= 1; dy++) {
			for (std::int32_t dx = -1; dx <= 1; dx++) {
				if (dx != 0 || dy != 0) {
					painter.Icon(icon, Rectf(rect.X + pixel * float(dx), rect.Y + pixel * float(dy), size, size), Colorf(0.0f, 0.0f, 0.0f, 0.85f), UiLayer::Content);
				}
			}
		}
		painter.Icon(icon, rect, Colorf::White, UiLayer::Overlay);
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
		// Taken out of the list first, whatever their destruction does sees a list that's whole
		if (!_retired.empty()) {
			SmallVector<std::unique_ptr<UiElement>, 0> retired = Death::move(_retired);
			_retired.clear();
		}

		// A box that was hidden or disabled while it was typed into isn't typed into anymore, what was typed is confirmed
		if (_focus != nullptr && (!IsShownInTree(_focus) || !_focus->IsUsable())) {
			SetFocus(nullptr);
		}

		if (_root != nullptr) {
			_root->Update(timeMult);
		}
		// By index, an element may show another one while it's updated, which can move the list in memory
		for (std::size_t i = 0; i < _overlays.size(); i++) {
			UiElement* element = _overlays[i].Element.get();
			element->Update(timeMult);
		}

		if (_touchHold == TouchHold::Waiting && _touchDown && _capture != nullptr && !_touchScrolling) {
			// The finger rested on it long enough, it drags it from now on, which the device tells by a short buzz
			_touchHoldTime += timeMult;
			if (_touchHoldTime >= TouchHoldDelay) {
				_touchHold = TouchHold::Held;
				_tooltipShown = false;
				_touchTooltip = false;
				theApplication().Vibrate(12);
				UiPointerEvent holdEvent;
				holdEvent.Pos = _pointerPos;
				_capture->OnPointerHold(holdEvent);
			}
		}

		if (_tooltipLinger > 0.0f) {
			_tooltipLinger -= timeMult;
			if (_tooltipLinger <= 0.0f) {
				_tooltipLinger = 0.0f;
				SetHover(nullptr);
			}
		} else if (TouchInput) {
			// A finger held still on an element shows its tooltip, also while it presses it, unless holding it starts dragging it
			if (_touchDown && _hover != nullptr && !_touchScrolling && !_tooltipShown && _touchHold == TouchHold::None) {
				_tooltipTime += timeMult;
				if (_tooltipTime >= TouchTooltipDelay && !_hover->GetTooltip(_pointerPos).empty()) {
					_tooltipShown = true;
					_touchTooltip = true;
				}
			}
		} else if (_hover != nullptr && _capture == nullptr && !_tooltipShown) {
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

	class UiSurface::PanelsCanvas : public UI::Canvas
	{
	public:
		explicit PanelsCanvas(UiSurface& surface)
			: _surface(surface)
		{
			OrderedDraws = true;
		}

		bool OnDraw(RenderQueue& renderQueue) override
		{
			UI::Canvas::OnDraw(renderQueue);
			_surface.DrawPanelsInto(*this);
			return true;
		}

	private:
		UiSurface& _surface;
	};

	SceneNode* UiSurface::GetPanelsNode()
	{
		if (_panelsCanvas == nullptr) {
			_panelsCanvas = std::make_unique<PanelsCanvas>(*this);
		}
		return _panelsCanvas.get();
	}

	std::uint16_t UiSurface::GetPanelsDepth()
	{
		return std::uint16_t(UiPainter::BaseDepth + RootSlot * UiPainter::SlotDepth);
	}

	void UiSurface::DrawPanelsInto(UI::Canvas& canvas)
	{
		// The picture of the level is drawn before the screen, the surface knows by the frame it's drawn already
		if (!DrawsPanelsInLevel || !DrawsPanelsInLevel()) {
			return;
		}
		_panelsDrawnFrame = theApplication().GetFrameCount();

		Vector2f screenSize = GetScreenSize();
		canvas.ViewSize = Vector2i(std::int32_t(screenSize.X), std::int32_t(screenSize.Y));
		canvas.ClipRect = Rectf(0.0f, 0.0f, 0.0f, 0.0f);
		_lines->BeginFrame();
		UiPainter painter(canvas, _skin, GetFont(), _scale, _lines.get());
		UiElement::BeginLayout();
		_drawingInLevel = true;
		DrawRoot(painter, screenSize, true);
		_drawingInLevel = false;
		UiElement::EndLayout();
	}

	void UiSurface::DrawRoot(UiPainter& painter, Vector2f screenSize, bool inLevel)
	{
		Rectf backdropBounds;
		const Texture* backdrop = (GetBackdrop ? GetBackdrop(UiBackdrop::Panels, screenSize, backdropBounds, inLevel) : nullptr);
		// What is shown over the level is drawn before the panels, it's in their picture
		painter.SetBackdrop(backdrop, backdropBounds, UiTheme::BackdropFillOpacity, RootSlot);

		if (_root != nullptr && _root->IsShown()) {
			_root->Arrange(GetContext(), Rectf(0.0f, 0.0f, screenSize.X, screenSize.Y));
			painter.SetSlot(RootSlot);
			_root->Draw(painter);
		}
	}

	bool UiSurface::OnDraw(RenderQueue& renderQueue)
	{
		UI::Canvas::OnDraw(renderQueue);

		Vector2f screenSize = GetScreenSize();
		ViewSize = Vector2i(std::int32_t(screenSize.X), std::int32_t(screenSize.Y));
		ClipRect = Rectf(0.0f, 0.0f, 0.0f, 0.0f);

		UiContext context = GetContext();
		bool panelsDrawn = (_panelsDrawnFrame == theApplication().GetFrameCount());
		if (!panelsDrawn) {
			_lines->BeginFrame();
		}
		UiPainter painter(*this, _skin, GetFont(), _scale, _lines.get());
		// Sizes measured from here on are kept for the frame, nothing changes while it's laid out and drawn
		UiElement::BeginLayout();

		if (!panelsDrawn) {
			DrawRoot(painter, screenSize, false);
		}
		Rectf overlayBackdropBounds;
		const Texture* overlayBackdrop = (GetBackdrop ? GetBackdrop(UiBackdrop::Overlays, screenSize, overlayBackdropBounds, panelsDrawn) : nullptr);
		painter.SetBackdrop(overlayBackdrop, overlayBackdropBounds, UiTheme::BackdropWindowFillOpacity);

		// Windows and menus stay in the part of the screen that isn't cut off
		Rectf safe = GetSafeBounds();
		std::int32_t slot = FirstOverlaySlot;
		for (std::size_t i = 0; i < _overlays.size(); i++) {
			if (!IsActive(_overlays[i])) {
				continue;
			}

			UiElement* element = _overlays[i].Element.get();
			const UiOverlayOptions& options = _overlays[i].Options;
			Vector2f size = element->MeasureCached(context, Vector2f(safe.W, safe.H));
			size.X = std::min(size.X, safe.W);
			size.Y = std::min(size.Y, safe.H);
			Vector2f pos = (options.Centered
				? Vector2f(std::round(safe.X + (safe.W - size.X) * 0.5f), std::round(safe.Y + (safe.H - size.Y) * 0.4f))
				: options.Position);
			if (!options.Centered && options.FallbackRight >= 0.0f && pos.X + size.X > safe.X + safe.W) {
				pos.X = options.FallbackRight - size.X;
			}
			// Moved to fit the screen
			pos.X = std::clamp(pos.X, safe.X, safe.X + safe.W - size.X);
			pos.Y = std::clamp(pos.Y, safe.Y, safe.Y + safe.H - size.Y);
			element->Arrange(context, Rectf(pos.X, pos.Y, size.X, size.Y));

			painter.SetSlot(slot++);
			if (options.Modal) {
				painter.FillRect(Rectf(0.0f, 0.0f, screenSize.X, screenSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.45f), UiLayer::Back);
			}
			element->Draw(painter);
		}

		painter.SetSlot(slot);
		DrawToasts(painter);
		if (!_pointerTooltip.empty()) {
			DrawTooltip(painter, _pointerTooltip);
		} else if (_tooltipShown && _hover != nullptr && (_capture == nullptr || _touchTooltip)) {
			DrawTooltip(painter, _hover->GetTooltip(_pointerPos));
		}
		_pointerTooltip = {};

		if (DrawOver) {
			painter.SetSlot(slot + 1);
			DrawOver(painter, screenSize);
		}

		bool cursorHidden = false;
		for (const Overlay& overlay : _overlays) {
			if (IsActive(overlay) && overlay.Options.HidesVirtualCursor) {
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

		// Over everything, the virtual cursor too, then forgotten, whatever is pointed at asks for it again next frame
		if (_cursorOverlay != UiCursorOverlay::None && !TouchInput) {
			painter.SetSlot(slot + 4);
			DrawCursorOverlay(painter);
		}
		_cursorOverlay = UiCursorOverlay::None;

		UiElement::EndLayout();
		return true;
	}

	UiElement* UiSurface::HitTestAll(Vector2f pos, bool& blocked)
	{
		blocked = false;
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			Overlay& overlay = _overlays[i - 1];
			if (!IsActive(overlay)) {
				continue;
			}
			if (UiElement* hit = overlay.Element->HitTest(pos)) {
				return hit;
			}
			if (overlay.Element->GetHitBounds().Contains(pos)) {
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
		// Taken out of the list first, and destroyed only then, so whatever their destruction does sees a list that's whole
		SmallVector<std::unique_ptr<UiElement>, 4> closed;
		for (std::size_t i = 0; i < _overlays.size(); ) {
			if (_overlays[i].Closing) {
				closed.push_back(Death::move(_overlays[i].Element));
				_overlays.erase(_overlays.begin() + i);
			} else {
				i++;
			}
		}
		closed.clear();
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
		if (_touchScroll == element) {
			_touchScroll = nullptr;
		}
	}

	void UiSurface::Retire(std::unique_ptr<UiElement> element)
	{
		if (element == nullptr || _instance == nullptr) {
			// Destroyed at once
			return;
		}
		UiSurface* surface = _instance;
		UiElement* retired = element.get();
		// Kept whole until it's destroyed, a callback that took it out of the interface may still be running in it
		surface->_retired.push_back(Death::move(element));
		// Taken away by the parent pointer, IsInside() then finds what is inside of it
		surface->CancelCaptureInside(retired);
		retired->_parent = nullptr;
		retired->_surface = nullptr;
		surface->ForgetInside(retired);
	}

	void UiSurface::ForgetInside(UiElement* element)
	{
		// What it holds is looked for by the parents, nothing in it has to be visited
		if (_hover != nullptr && IsInside(_hover, element)) {
			_hover->_hovered = false;
			_hover = nullptr;
		}
		if (_capture != nullptr && IsInside(_capture, element)) {
			_capture->_pressed = false;
			_capture = nullptr;
		}
		if (_lastClickElement != nullptr && IsInside(_lastClickElement, element)) {
			_lastClickElement = nullptr;
		}
		if (_focus != nullptr && IsInside(_focus, element)) {
			// As if it was destroyed, nothing is confirmed, it's gone with what it would be confirmed into
			_focus->_focused = false;
			_focus = nullptr;
		}
		if (_touchScroll != nullptr && IsInside(_touchScroll, element)) {
			_touchScroll = nullptr;
			_touchScrolling = false;
		}
	}

	void UiSurface::CancelCaptureInside(UiElement* element)
	{
		if (_capture == nullptr || !IsInside(_capture, element)) {
			return;
		}
		// What is dragged gets its end, a number dragged keeps the value it was dragged to
		UiElement* capture = _capture;
		_capture = nullptr;
		capture->_pressed = false;
		capture->OnPointerCancel();
	}

	bool UiSurface::IsInside(const UiElement* element, const UiElement* ancestor)
	{
		for (const UiElement* current = element; current != nullptr; current = current->_parent) {
			if (current == ancestor) {
				return true;
			}
		}
		return false;
	}

	bool UiSurface::IsShownInTree(UiElement* element)
	{
		for (UiElement* current = element; current != nullptr; current = current->_parent) {
			if (!current->IsShown()) {
				return false;
			}
		}
		return true;
	}

	bool UiSurface::LeavesKeyToShortcuts(const KeyboardEvent& event)
	{
		if (IsKeyLeftByText) {
			return IsKeyLeftByText(event);
		}
		return (event.sym == Keys::F1 || event.sym == Keys::F5);
	}

	void UiSurface::DrawTooltip(UiPainter& painter, StringView text)
	{
		if (text.empty()) {
			return;
		}

		Rectf safe = GetSafeBounds();
		float padding = painter.Px(UiTheme::Padding * 1.5f);
		// Less above and below the text than at its sides, the line itself is a little higher than the text already
		float paddingY = std::round(padding * 0.5f);
		// As wide as its longest line and as high as all of its lines, a tooltip may have more of them
		std::int32_t lineCount = 1;
		for (char c : text) {
			if (c == '\n') {
				lineCount++;
			}
		}
		float lineHeight = std::max(painter.GetTextHeight(), painter.Px(UiTheme::RowHeight) * 0.75f);
		Vector2f size(painter.MeasureText(text) + padding * 2.0f, lineHeight * float(lineCount) + paddingY * 2.0f);
		// Below the arrows shown by the pointer, if there are any, not over them
		Vector2f pos(_pointerPos.X + painter.Px(8.0f), _pointerPos.Y + GetPointerLabelOffset(painter.Px(14.0f)));
		if (TouchInput) {
			// Above the finger, which would cover it below
			pos = Vector2f(_pointerPos.X - size.X * 0.5f, _pointerPos.Y - size.Y - painter.Px(24.0f));
		}
		if (pos.X + size.X > safe.X + safe.W) {
			pos.X = safe.X + safe.W - size.X;
		}
		pos.X = std::max(pos.X, safe.X);
		if (pos.Y + size.Y > safe.Y + safe.H) {
			// Above the pointer then, also above the arrows, which go there too near the bottom of the screen
			pos.Y = _pointerPos.Y - size.Y - GetPointerLabelOffset(painter.Px(4.0f));
		}
		pos.Y = std::max(pos.Y, safe.Y);
		Rectf rect(std::round(pos.X), std::round(pos.Y), std::round(size.X), std::round(size.Y));
		painter.Panel(rect, true, UiTheme::PanelHeader, UiTheme::PanelBorder);
		painter.TextInRect(text, Rectf(rect.X + padding, rect.Y, rect.W - padding * 2.0f, rect.H), UiTheme::Text, Alignment::Left);
	}

	void UiSurface::DrawToasts(UiPainter& painter)
	{
		Rectf safe = GetSafeBounds();
		float padding = painter.Px(UiTheme::Padding * 1.5f);
		float iconSize = painter.Px(UiTheme::IconSize);
		float y = safe.Y + safe.H - painter.Px(UiTheme::StatusBarHeight + 8.0f);

		for (std::size_t i = _toasts.size(); i > 0; i--) {
			const Toast& toast = _toasts[i - 1];
			float alpha = std::min(1.0f, toast.TimeLeft / ToastFadeTime);
			float age = ToastDuration - toast.TimeLeft;
			float slide = (UiTheme::ReducedMotion ? 0.0f : std::max(0.0f, 1.0f - age / ToastFadeTime));

			Vector2f size(padding * 3.0f + iconSize + painter.MeasureText(toast.Text), std::max(iconSize, painter.Px(UiTheme::RowHeight)) + padding * 2.0f);
			Rectf rect(std::round(safe.X + (safe.W - size.X) * 0.5f), std::round(y - size.Y + slide * painter.Px(12.0f)), std::round(size.X), std::round(size.Y));

			painter.PushOpacity(alpha);
			painter.Panel(rect, true, UiTheme::PanelHeader, UiTheme::PanelBorder);
			// Red for a warning, a message in the colors of its own icon
			Colorf iconColor = (toast.Icon == EditorIcon::Warning ? UiTheme::Danger : Colorf(1.0f, 1.0f, 1.0f, 1.0f));
			painter.Icon(toast.Icon, Rectf(rect.X + padding, rect.Y, iconSize, rect.H), iconColor);
			painter.TextInRect(toast.Text, Rectf(rect.X + padding * 2.0f + iconSize, rect.Y, rect.W, rect.H), UiTheme::Text);
			painter.PopOpacity();

			y = rect.Y - painter.Px(4.0f);
		}
	}
}

#endif
