#include "Surface.h"

#if defined(WITH_UI_WINDOWS)

#include "Menu.h"
#include "ReadableFont.h"
#include "Widgets.h"
#include "../../ContentResolver.h"
#include "../../Rendering/PolylineRenderer.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Graphics/Camera.h"
#include "../../../nCine/Graphics/IGfxDevice.h"
#include "../../../nCine/Graphics/Texture.h"
#include "../../../nCine/Graphics/Viewport.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Jazz2::UI::Windows
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
		constexpr std::int32_t FirstOverlaySlot = RootSlot + Surface::RootSlots;
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
#if defined(NCINE_HAS_AUXILIARY_WINDOWS)
		// The window the pointer is over when it's over none of them
		constexpr std::uint32_t PointerOutside = ~std::uint32_t(0);
#endif
		// How long a skin made for a display with another scale is kept after no window was drawn with it, in updates
		constexpr std::uint32_t RasterSkinFrames = 120;
	}

	Surface* Surface::_instance = nullptr;

	Surface::Surface()
		: _scale(1.0f), _hover(nullptr), _capture(nullptr), _focus(nullptr), _tooltipTime(0.0f),
			_tooltipShown(false), _lastClickTime(0), _lastClickElement(nullptr), _touchDown(false), _touchTooltip(false),
			_tooltipLinger(0.0f), _touchScroll(nullptr), _touchScrolling(false), _touchScrollOrigin(0.0f), _touchVelocity(0.0f)
	{
		_instance = this;
		_lines = std::make_unique<Rendering::PolylineRenderer>(1 << 18);
		OrderedDraws = true;
		_skin.Build(_scale);
	}

	Surface::~Surface()
	{
		// The elements are destroyed while the surface is still known, so they don't reach a dangling one
		_overlays.clear();
		_root = nullptr;
		while (!_retired.empty()) {
			SmallVector<std::unique_ptr<Element>, 0> retired = Death::move(_retired);
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

	Element* Surface::SetRoot(std::unique_ptr<Element> root)
	{
		if (_root != nullptr) {
			Retire(Death::move(_root));
		}
		_root = Death::move(root);
		_root->_surface = this;
		return _root.get();
	}

	Element* Surface::PushOverlay(std::unique_ptr<Element> element, OverlayOptions&& options)
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

	void Surface::CloseOverlay(Element* element)
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

	void Surface::MoveOverlay(Element* element, Vector2f position, float fallbackRight)
	{
		for (Overlay& overlay : _overlays) {
			if (overlay.Element.get() == element) {
				// Kept as it is, it's only placed on the screen when it's drawn, so a window dragged past the edge stops
				// there and comes back with the pointer, not ahead of it
				overlay.Options.Centered = false;
				overlay.Options.Position = Vector2f(std::round(position.X), std::round(position.Y));
				overlay.MovedByUser = true;
				overlay.MovedNow = true;
				overlay.Options.FallbackRight = (fallbackRight >= 0.0f ? std::round(fallbackRight) : fallbackRight);
				break;
			}
		}
	}

	bool Surface::RaiseOverlay(Element* element)
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

	void Surface::ClosePopups()
	{
		// Windows stay, a menu item or a choice in a list doesn't close the window it was picked in
		for (std::size_t i = 0; i < _overlays.size(); i++) {
			if (_overlays[i].Options.IsPopup()) {
				CloseOverlay(_overlays[i].Element.get());
			}
		}
	}

	bool Surface::HasModal() const
	{
		for (const Overlay& overlay : _overlays) {
			if (overlay.Options.Modal && !overlay.Closing) {
				return true;
			}
		}
		return false;
	}

	bool Surface::HasOverlays() const
	{
		for (const Overlay& overlay : _overlays) {
			if (IsActive(overlay)) {
				return true;
			}
		}
		return false;
	}

	bool Surface::HasStackedModal() const
	{
		return (GetStackedOverlayCount() > 0);
	}

	std::size_t Surface::GetStackedOverlayCount() const
	{
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			if (!IsActive(_overlays[i - 1]) || !_overlays[i - 1].Options.Modal) {
				continue;
			}
			for (std::size_t j = 0; j < i - 1; j++) {
				if (IsActive(_overlays[j])) {
					return i - 1;
				}
			}
			return 0;
		}
		return 0;
	}

	bool Surface::IsDetached(const Element* element) const
	{
		for (const Overlay& overlay : _overlays) {
			if (overlay.Element.get() == element) {
				return (overlay.Detached.Window != nullptr);
			}
		}
		for (const DetachableElement& detachable : _detachables) {
			if (detachable.Element == element) {
				return (detachable.Detached.Window != nullptr);
			}
		}
		return false;
	}

	bool Surface::CanDetach() const
	{
#if defined(NCINE_HAS_AUXILIARY_WINDOWS)
		// A window of its own would be behind the main window in fullscreen, or on another display that is left out of it
		IGfxDevice& gfxDevice = theApplication().GetGfxDevice();
		return (AllowDetaching && !TouchInput && !gfxDevice.isFullscreen() && gfxDevice.supportsAuxiliaryWindows());
#else
		return false;
#endif
	}

	bool Surface::IsInPointerWindow(const Overlay& overlay) const
	{
		return (overlay.Detached.Window != nullptr ? overlay.Detached.Window->GetId() == _pointerWindow : _pointerWindow == 0);
	}

	float Surface::GetDesktopScale() const
	{
		IGfxDevice& gfxDevice = theApplication().GetGfxDevice();
		return (gfxDevice.drawableWidth() > 0 ? float(gfxDevice.width()) / float(gfxDevice.drawableWidth()) : 1.0f);
	}

	Rectf Surface::GetWantedBounds(const Overlay& overlay)
	{
		Rectf safe = GetSafeBounds();
		Vector2f size(overlay.Element->Bounds.W, overlay.Element->Bounds.H);
		if (size.X <= 0.0f || size.Y <= 0.0f) {
			// Not laid out yet, as a menu just shown, it's measured now, so it's shown where it belongs from the first frame
			Element::BeginLayout();
			size = overlay.Element->MeasureCached(GetContext(), Vector2f(safe.W, safe.H));
			Element::EndLayout();
			size.X = std::min(size.X, safe.W);
			size.Y = std::min(size.Y, safe.H);
		}
		if (overlay.Options.Centered) {
			return Rectf(std::round(safe.X + (safe.W - size.X) * 0.5f), std::round(safe.Y + (safe.H - size.Y) * 0.4f), std::round(size.X), std::round(size.Y));
		}
		Vector2f pos = overlay.Options.Position;
		if (overlay.Options.FallbackRight >= 0.0f && pos.X + size.X > safe.X + safe.W) {
			pos.X = overlay.Options.FallbackRight - size.X;
		}
		return Rectf(std::round(pos.X), std::round(pos.Y), std::round(size.X), std::round(size.Y));
	}

	bool Surface::OverlapsDetached(const Rectf& rect, std::size_t overlayCount) const
	{
		auto overlaps = [&rect](const DetachedWindow* window) {
			if (window == nullptr || !window->IsShown()) {
				return false;
			}
			const Rectf& bounds = window->GetBounds();
			return (rect.X < bounds.X + bounds.W && rect.X + rect.W > bounds.X && rect.Y < bounds.Y + bounds.H && rect.Y + rect.H > bounds.Y);
		};
		for (std::size_t i = 0; i < overlayCount && i < _overlays.size(); i++) {
			if (overlaps(_overlays[i].Detached.Window.get())) {
				return true;
			}
		}
		for (const DetachableElement& detachable : _detachables) {
			if (overlaps(detachable.Detached.Window.get())) {
				return true;
			}
		}
		return false;
	}

	Rectf Surface::UpdateDetachment(Detachment& detachment, Element* element, Rectf wanted, bool moving, bool popup, bool overlay, bool force, bool restore)
	{
		Vector2f screen = GetScreenSize();
		bool outside = (force || wanted.X < 0.0f || wanted.Y < 0.0f || wanted.X + wanted.W > screen.X || wanted.Y + wanted.H > screen.Y);
		IGfxDevice& gfxDevice = theApplication().GetGfxDevice();
		Vector2i mainPos = gfxDevice.windowPosition();
		float toDesktop = GetDesktopScale();

		if (detachment.Window == nullptr) {
			// Only once it's moved out of the main window, or a popup that doesn't fit it, one that is just too large stays
			if (!outside || !(moving || popup || restore) || wanted.W < 1.0f || wanted.H < 1.0f) {
				detachment.LastWanted = wanted;
				return wanted;
			}
			// A popup of a window of its own is put where that window shows the place, it may be drawn at another scale.
			// One that isn't dragged out now, as a panel that was in a window of its own the last time, is put all on a
			// display, the displays may have changed since.
			Vector2i desktopPos = ToDesktop(Vector2f(wanted.X, wanted.Y));
			Vector2i desktopSize(std::int32_t(std::round(wanted.W * toDesktop)), std::int32_t(std::round(wanted.H * toDesktop)));
			desktopPos = KeepOnDesktop(desktopPos, desktopSize, popup || restore);
			Rectf created(std::round(float(desktopPos.X - mainPos.X) / toDesktop), std::round(float(desktopPos.Y - mainPos.Y) / toDesktop), wanted.W, wanted.H);
			bool layout = overlay;
			detachment.Window = DetachedWindow::Create(element, created, desktopPos, toDesktop, popup, false, [this, layout](UI::Canvas& canvas, DetachedWindow& window) {
				DrawDetached(canvas, window, layout);
			});
			detachment.LastWanted = wanted;
			if (detachment.Window == nullptr) {
				return wanted;
			}
			detachment.Window->SetTitle(element->GetTitle());
			element->_detached = !overlay;
			// A window dragged out stays in front, the focus stays where it is until it's clicked
			detachment.Window->Show(false);
			detachment.LastWanted = created;
			return created;
		}

		DetachedWindow& window = *detachment.Window;
		bool moved = (moving && (wanted.X != detachment.LastWanted.X || wanted.Y != detachment.LastWanted.Y));
		if (moved && !outside) {
			// Moved back into the main window, it's shown there again
			detachment.Window = nullptr;
			element->_detached = false;
			detachment.LastWanted = wanted;
			return wanted;
		}

		Vector2i desktopPos;
		Rectf bounds;
		if (moved) {
			desktopPos = Vector2i(mainPos.X + std::int32_t(std::round(wanted.X * toDesktop)), mainPos.Y + std::int32_t(std::round(wanted.Y * toDesktop)));
			bounds = wanted;
		} else {
			// It stays where it is on the desktop, also when the main window moves, the element follows the window
			desktopPos = window.GetDesktopPosition();
			bounds = Rectf(std::round(float(desktopPos.X - mainPos.X) / toDesktop), std::round(float(desktopPos.Y - mainPos.Y) / toDesktop), wanted.W, wanted.H);
		}
		// Never so far off the displays that it can't be grabbed anymore, also when a display was taken away
		Vector2i kept = KeepOnDesktop(desktopPos, window.GetDesktopSize(), false);
		if (kept != desktopPos) {
			desktopPos = kept;
			bounds.X = std::round(float(desktopPos.X - mainPos.X) / toDesktop);
			bounds.Y = std::round(float(desktopPos.Y - mainPos.Y) / toDesktop);
		}
		window.Place(bounds, desktopPos, toDesktop);
		window.Show(false);
		detachment.LastWanted = bounds;
		return bounds;
	}

	DetachedWindow* Surface::FindDetachedWindow(std::uint32_t windowId) const
	{
		if (windowId == 0) {
			return nullptr;
		}
		for (const Overlay& overlay : _overlays) {
			if (overlay.Detached.Window != nullptr && overlay.Detached.Window->GetId() == windowId) {
				return overlay.Detached.Window.get();
			}
		}
		for (const DetachableElement& detachable : _detachables) {
			if (detachable.Detached.Window != nullptr && detachable.Detached.Window->GetId() == windowId) {
				return detachable.Detached.Window.get();
			}
		}
		return nullptr;
	}

	Vector2i Surface::ToDesktop(Vector2f point) const
	{
		// The windows in front first, they're shown over those before them
		auto contains = [&point](const DetachedWindow* window) {
			if (window == nullptr || !window->IsShown()) {
				return false;
			}
			const Rectf& bounds = window->GetBounds();
			return (point.X >= bounds.X && point.Y >= bounds.Y && point.X < bounds.X + bounds.W && point.Y < bounds.Y + bounds.H);
		};
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			if (contains(_overlays[i - 1].Detached.Window.get())) {
				return _overlays[i - 1].Detached.Window->ToDesktop(point);
			}
		}
		for (const DetachableElement& detachable : _detachables) {
			if (contains(detachable.Detached.Window.get())) {
				return detachable.Detached.Window->ToDesktop(point);
			}
		}
		Vector2i mainPos = theApplication().GetGfxDevice().windowPosition();
		float toDesktop = GetDesktopScale();
		return Vector2i(mainPos.X + std::int32_t(std::round(point.X * toDesktop)), mainPos.Y + std::int32_t(std::round(point.Y * toDesktop)));
	}

	Vector2i Surface::KeepOnDesktop(Vector2i position, Vector2i size, bool whole) const
	{
#if defined(NCINE_HAS_AUXILIARY_WINDOWS)
		// The display its top is on, or the nearest one
		Recti area;
		if (!theApplication().GetGfxDevice().desktopWorkArea(Vector2i(position.X + size.X / 2, position.Y), area)) {
			return position;
		}
		if (whole) {
			position.X = std::clamp(position.X, area.X, std::max(area.X, area.X + area.W - size.X));
			position.Y = std::clamp(position.Y, area.Y, std::max(area.Y, area.Y + area.H - size.Y));
		} else {
			// As the windows of the system, a part of its top stays on the display to be grabbed by, never above it
			float toDesktop = GetDesktopScale();
			std::int32_t keepX = std::min(size.X, std::int32_t(std::round(48.0f * _scale * toDesktop)));
			std::int32_t keepY = std::min(size.Y, std::int32_t(std::round(24.0f * _scale * toDesktop)));
			position.X = std::clamp(position.X, area.X - size.X + keepX, std::max(area.X - size.X + keepX, area.X + area.W - keepX));
			position.Y = std::clamp(position.Y, area.Y, std::max(area.Y, area.Y + area.H - keepY));
		}
#endif
		return position;
	}

	Skin& Surface::GetRasterSkin(float raster)
	{
		if (std::abs(raster - 1.0f) < 0.001f) {
			return _skin;
		}
		// Made only for a window on a display with another scale, and dropped a while after none is there anymore
		float scale = _scale * raster;
		RasterSkin* entry = nullptr;
		for (RasterSkin& rasterSkin : _rasterSkins) {
			if (std::abs(rasterSkin.Scale - scale) < 0.001f) {
				entry = &rasterSkin;
				break;
			}
		}
		if (entry == nullptr) {
			entry = &_rasterSkins.emplace_back();
			entry->Scale = scale;
			entry->Atlas = std::make_unique<Skin>();
		}
		entry->Atlas->Build(scale);
		entry->LastUsed = _updateFrame;
		return *entry->Atlas;
	}

	UI::Font* Surface::GetRasterFont(float raster) const
	{
		return (std::abs(raster - 1.0f) < 0.001f ? GetFont() : Context::GetFont(_scale * raster));
	}

	Rectf Surface::PlaceDetachable(Element* element, const Rectf& wanted, bool moving, bool restore)
	{
		DetachableElement* entry = nullptr;
		for (DetachableElement& detachable : _detachables) {
			if (detachable.Element == element) {
				entry = &detachable;
				break;
			}
		}
		if (entry == nullptr) {
			if (!CanDetach()) {
				return wanted;
			}
			entry = &_detachables.emplace_back();
			entry->Element = element;
		}
		entry->Placed = true;
		if (!CanDetach()) {
			entry->Detached.Window = nullptr;
			element->_detached = false;
			return wanted;
		}
		if (WindowsHidden) {
			if (entry->Detached.Window != nullptr) {
				entry->Detached.Window->Hide();
			}
			return wanted;
		}
		return UpdateDetachment(entry->Detached, element, wanted, moving, false, false, false, restore);
	}

	void Surface::ReleaseDetachable(Element* element)
	{
		for (std::size_t i = 0; i < _detachables.size(); i++) {
			if (_detachables[i].Element == element) {
				element->_detached = false;
				_detachables.erase(_detachables.begin() + i);
				return;
			}
		}
	}

	void Surface::UpdateDetached()
	{
		bool canDetach = CanDetach();
		for (std::size_t i = 0; i < _overlays.size(); i++) {
			Overlay& overlay = _overlays[i];
			bool moving = overlay.MovedNow;
			overlay.MovedNow = false;
			if (!canDetach || !overlay.Options.Detachable || overlay.Options.Modal || overlay.Closing) {
				if (overlay.Detached.Window != nullptr && !overlay.Options.Centered) {
					// Back in the main window, as when it switched to fullscreen, it's put where it's seen all
					Rectf safe = GetSafeBounds();
					const Rectf& bounds = overlay.Detached.Window->GetBounds();
					overlay.Options.Position.X = std::round(std::clamp(overlay.Options.Position.X, safe.X, std::max(safe.X, safe.X + safe.W - bounds.W)));
					overlay.Options.Position.Y = std::round(std::clamp(overlay.Options.Position.Y, safe.Y, std::max(safe.Y, safe.Y + safe.H - bounds.H)));
				}
				overlay.Detached.Window = nullptr;
				continue;
			}
			if (!IsActive(overlay)) {
				// Hidden while a level is played, it comes back where it was
				if (overlay.Detached.Window != nullptr) {
					overlay.Detached.Window->Hide();
				}
				continue;
			}

			Rectf wanted = GetWantedBounds(overlay);
			bool popup = overlay.Options.IsPopup();
			// A popup over a window of its own would be covered by it in the main window
			bool overDetached = (popup && OverlapsDetached(wanted, i));
			Rectf bounds = UpdateDetachment(overlay.Detached, overlay.Element.get(), wanted, moving || overlay.MovedByUser, popup, true, overDetached);
			if (overlay.Detached.Window != nullptr && !overlay.Options.Centered) {
				// Where it is now, also after the main window moved
				overlay.Options.Position = Vector2f(bounds.X, bounds.Y);
				overlay.Options.FallbackRight = -1.0f;
			}
		}

		// Elements of the tree whose owner didn't place them since the last update are hidden, as while a level is played,
		// their windows come back with them
		for (std::size_t i = 0; i < _detachables.size(); ) {
			DetachableElement& detachable = _detachables[i];
			if (!canDetach || (!detachable.Placed && detachable.Detached.Window == nullptr)) {
				detachable.Element->_detached = false;
				_detachables.erase(_detachables.begin() + i);
				continue;
			}
			if (!detachable.Placed) {
				detachable.Detached.Window->Hide();
			}
			detachable.Placed = false;
			i++;
		}
	}

	void Surface::DrawDetached(UI::Canvas& canvas, DetachedWindow& window, bool layout)
	{
		Element* element = window.GetElement();
		const Rectf& bounds = window.GetBounds();
		// Positions are relative to the main window, they can be outside of it, the font mustn't leave out glyphs drawn there
		canvas.ViewSize = Vector2i(0, 0);
		canvas.ClipRect = Rectf(0.0f, 0.0f, 0.0f, 0.0f);
		float raster = window.GetRaster();
		canvas.PixelScale = raster;
		window.GetLines()->BeginFrame();
		Painter painter(canvas, GetRasterSkin(raster), GetRasterFont(raster), _scale, window.GetLines());
		painter.SetRaster(raster);
		if (!window.HasRoundedCorners()) {
			painter.SetSquareWindow(bounds);
		}
		Element::BeginLayout();

		if (layout) {
			Context context = GetContext();
			Vector2f size = element->MeasureCached(context, GetScreenSize());
			element->Arrange(context, Rectf(bounds.X, bounds.Y, size.X, size.Y));
		}
		painter.SetSlot(FirstOverlaySlot);
		element->Draw(painter);

		std::int32_t slot = FirstOverlaySlot + 1;
		if (HasModal()) {
			// A window waiting for an answer in the main window takes everything, this one too
			painter.SetSlot(slot++);
			painter.FillRect(bounds, Colorf(0.0f, 0.0f, 0.0f, 0.45f), PaintLayer::Back);
		}
		if (_pointerWindow == window.GetId()) {
			// What the pointer shows is in the window the pointer is over
			_detachedDrawBounds = bounds;
			painter.SetSlot(slot);
			if (_tooltipWindowText.empty()) {
				// Unless it didn't fit, it's in a window of its own then
				DrawTooltip(painter, GetTooltipText());
			}
			if (_cursorOverlay != CursorOverlay::None && !TouchInput) {
				painter.SetSlot(slot + 1);
				DrawCursorOverlay(painter);
			}
			_detachedDrawBounds = Rectf();
		}

		Element::EndLayout();
	}
	bool Surface::IsOverlayShown(const Element* element) const
	{
		for (const Overlay& overlay : _overlays) {
			if (overlay.Element.get() == element) {
				return !overlay.Closing;
			}
		}
		return false;
	}

	void Surface::SetScale(float scale)
	{
		_scale = std::clamp(scale, 0.5f, 8.0f);
		_skin.Build(_scale);
	}

	UI::Font* Surface::GetFont() const
	{
		UI::Font* font = Context::GetFont(_scale);
		if (font != nullptr && font == ContentResolver::Get().GetFont(FontType::Small)) {
			// Text in the pixel font drawn at a whole multiple of its size is crisp only if it's not smoothed. It's set on
			// every font asked for, a font is loaded again with the default filter when the palette changes, and the
			// texture does nothing when the filter is the same. The readable font is drawn at its own size, a text made
			// smaller or larger stays smooth.
			font->SetMagnificationFilter(Theme::IsTextScaleWhole(_scale) ? SamplerFilter::Nearest : SamplerFilter::Linear);
		}
		return font;
	}

	UI::Font* Surface::GetReadableFont(float textScale)
	{
		if (_instance == nullptr) {
			return nullptr;
		}
		if (_instance->_readableFont == nullptr) {
			_instance->_readableFont = std::make_unique<ReadableFont>();
		}
		return _instance->_readableFont->Get(textScale);
	}

	Context Surface::GetContext() const
	{
		return Context{ _scale };
	}

	Vector2f Surface::GetScreenSize() const
	{
		IGfxDevice& gfxDevice = theApplication().GetGfxDevice();
		return Vector2f(float(gfxDevice.drawableWidth()), float(gfxDevice.drawableHeight()));
	}

	Rectf Surface::GetSafeBounds() const
	{
		// Each edge a part of the side it's on, rounded as the game rounds its own margins
		Vector2f screen = GetScreenSize();
		float left = std::round(screen.X * std::clamp(SafeMargins.Left, 0.0f, 0.5f));
		float top = std::round(screen.Y * std::clamp(SafeMargins.Top, 0.0f, 0.5f));
		float right = std::round(screen.X * std::clamp(SafeMargins.Right, 0.0f, 0.5f));
		float bottom = std::round(screen.Y * std::clamp(SafeMargins.Bottom, 0.0f, 0.5f));
		return Rectf(left, top, std::max(screen.X - left - right, 1.0f), std::max(screen.Y - top - bottom, 1.0f));
	}

	bool Surface::OnPointerDown(const PointerEvent& event)
	{
		_pointerPos = event.Pos;
		_pointerWindow = event.WindowId;
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
			if ((IsInPointerWindow(overlay) && overlay.Element->GetHitBounds().Contains(event.Pos)) || overlay.Options.Modal) {
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
			if (IsInPointerWindow(overlay) && overlay.Element->GetHitBounds().Contains(event.Pos)) {
				if (overlay.Detached.Window != nullptr) {
					overlay.Detached.Window->Raise();
				}
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
		Element* hit = HitTestAll(event.Pos, blocked);
		if (blocked) {
			return true;
		}

		bool keepsFocus = false;
		for (Element* element = hit; element != nullptr && !keepsFocus; element = element->GetParent()) {
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
			for (Element* element = hit; element != nullptr; element = element->GetParent()) {
				if (ScrollView* scrollView = runtime_cast<ScrollView>(element)) {
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
		PointerEvent clickEvent = event;
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

		for (Element* parent = hit->GetParent(); parent != nullptr; parent = parent->GetParent()) {
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

	void Surface::CancelPointerCapture()
	{
		_touchHold = TouchHold::None;
		if (_capture == nullptr) {
			return;
		}
		// As when a finger starts to scroll a list, see OnPointerMove()
		Element* capture = _capture;
		_capture = nullptr;
		capture->_pressed = false;
		capture->OnPointerCancel();
	}

	void Surface::DismissTooltip()
	{
		if (_hover != nullptr) {
			_hover->_hovered = false;
			_hover = nullptr;
		}
		_tooltipTime = 0.0f;
		_tooltipShown = false;
		_tooltipLinger = 0.0f;
	}

	void Surface::TrackPointer(Vector2f pos)
	{
		// A finger always trembles a little, the moves themselves decide when its tooltip goes away
		if (!_touchDown && (pos - _pointerPos).SqrLength() > 4.0f) {
			_tooltipTime = 0.0f;
			_tooltipShown = false;
		}
		_pointerPos = pos;
	}

	bool Surface::OnPointerMove(const PointerEvent& event)
	{
		_pointerWindow = event.WindowId;
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
					Element* capture = _capture;
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
		Element* hit = HitTestAll(event.Pos, blocked);
		SetHover(_capture != nullptr ? (hit == _capture ? hit : nullptr) : hit);

		if (_capture != nullptr) {
			_capture->OnPointerMove(event);
			return true;
		}
		return (hit != nullptr || blocked);
	}

	bool Surface::OnPointerUp(const PointerEvent& event)
	{
		_pointerPos = event.Pos;
		_pointerWindow = event.WindowId;
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
			Element* capture = _capture;
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

	bool Surface::OnWheel(const PointerEvent& event)
	{
		_pointerWindow = event.WindowId;
		bool blocked;
		Element* hit = HitTestAll(event.Pos, blocked);
		if (blocked) {
			return true;
		}
		for (Element* element = hit; element != nullptr; element = element->GetParent()) {
			if (element->IsUsable() && element->OnWheel(event)) {
				return true;
			}
		}
		return (hit != nullptr);
	}

	bool Surface::OnKeyPressed(const KeyboardEvent& event)
	{
		// The focus gets the keys only if nothing modal is shown over it
		Element* focus = (_focus != nullptr && AcceptsInput(_focus) ? _focus : nullptr);
		std::int32_t focusOverlay = (focus != nullptr ? GetOverlayIndexOf(focus) : -2);
		if (focus != nullptr && IsPassThroughKey && runtime_cast<TextBox>(focus) != nullptr && IsPassThroughKey(event)) {
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
				Element* element = overlay.Element.get();
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
			if (MenuBar* menuBar = FindMenuBar(_root.get())) {
				if (menuBar->OpenByMnemonic(char('a' + (std::int32_t(event.sym) - std::int32_t(Keys::A))))) {
					return true;
				}
			}
		}
		return false;
	}

	bool Surface::OnTextInput(const TextInputEvent& event)
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

	MenuBar* Surface::FindMenuBar(Element* element)
	{
		if (element == nullptr || !element->IsShown()) {
			return nullptr;
		}
		if (MenuBar* menuBar = runtime_cast<MenuBar>(element)) {
			return menuBar;
		}
		for (std::int32_t i = 0; i < element->GetChildCount(); i++) {
			if (MenuBar* menuBar = FindMenuBar(element->GetChild(i))) {
				return menuBar;
			}
		}
		return nullptr;
	}

	Element* Surface::GetFocusScope() const
	{
		// Only inside of what is modal, otherwise where the focus is, or the top element shown over the interface
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			if (IsActive(_overlays[i - 1]) && _overlays[i - 1].Options.Modal) {
				return _overlays[i - 1].Element.get();
			}
		}
		if (_focus != nullptr && AcceptsInput(_focus)) {
			Element* top = _focus;
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

	void Surface::CollectTabStops(Element* element, const Rectf& screen, bool scrolled, SmallVectorImpl<Element*>& stops)
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
		if (!scrolled && onScreen && runtime_cast<ScrollView>(element) != nullptr) {
			scrolled = true;
		}
		for (std::int32_t i = 0; i < element->GetChildCount(); i++) {
			CollectTabStops(element->GetChild(i), screen, scrolled, stops);
		}
	}

	bool Surface::MoveFocus(bool backwards)
	{
		Element* scope = GetFocusScope();
		if (scope == nullptr) {
			return false;
		}
		Vector2f screenSize = GetScreenSize();
		SmallVector<Element*, 32> stops;
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
		Element* element = stops[next];
		SetFocus(element);
		if (_focus == element) {
			if (TextBox* textBox = runtime_cast<TextBox>(element)) {
				// All of its text, typing replaces it, as in a dialog of the system
				textBox->BeginEdit();
			}
			EnsureVisible(element);
		}
		return true;
	}

	void Surface::EnsureVisible(Element* element)
	{
		for (Element* parent = (element != nullptr ? element->GetParent() : nullptr); parent != nullptr; parent = parent->GetParent()) {
			if (ScrollView* scrollView = runtime_cast<ScrollView>(parent)) {
				if (Element* content = scrollView->GetContent()) {
					float top = element->Bounds.Y - content->Bounds.Y;
					scrollView->ScrollIntoView(top, top + element->Bounds.H);
				}
				return;
			}
		}
	}

	bool Surface::IsActive(const Overlay& overlay) const
	{
		if (overlay.Closing) {
			return false;
		}
		return !(WindowsHidden && !overlay.Options.Modal && !overlay.Options.IsPopup());
	}

	std::int32_t Surface::GetOverlayIndexOf(Element* element) const
	{
		if (element == nullptr) {
			return -2;
		}
		Element* top = element;
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

	bool Surface::AcceptsInput(Element* element) const
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

	float Surface::GetTouchSlop() const
	{
		return std::round(10.0f * std::max(_scale, 1.0f));
	}

	bool Surface::IsOverInterface(Vector2f pos)
	{
		if (_pointerWindow != 0) {
			// Over a window out of the main window, nothing of the main window is under the pointer
			return true;
		}
		bool blocked;
		return (HitTestAll(pos, blocked) != nullptr || blocked);
	}

	bool Surface::TakesTouchAt(Vector2f pos)
	{
		bool blocked;
		Element* hit = HitTestAll(pos, blocked);
		return (hit != nullptr ? !hit->YieldsToViewGestures() : blocked);
	}

	void Surface::SetFocus(Element* element)
	{
		if (_focus == element) {
			return;
		}
		Element* previous = _focus;
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

	void Surface::SetCursorOverlay(CursorOverlay overlay)
	{
		_cursorOverlay = overlay;
	}

	CursorOverlay Surface::GetResizeOverlay(bool left, bool top, bool right, bool bottom)
	{
		bool horizontal = (left || right), vertical = (top || bottom);
		if (horizontal && vertical) {
			return ((left && top) || (right && bottom) ? CursorOverlay::ResizeDiagonalNWSE : CursorOverlay::ResizeDiagonalNESW);
		}
		if (horizontal) {
			return CursorOverlay::ResizeHorizontal;
		}
		if (vertical) {
			return CursorOverlay::ResizeVertical;
		}
		return CursorOverlay::None;
	}

	float Surface::GetPointerLabelOffset(float distance) const
	{
		if (_cursorOverlay == CursorOverlay::None || TouchInput) {
			return distance;
		}
		return std::max(distance, std::round(_scale * CursorOverlayOffset) + float(_skin.GetIconSize()) * 0.5f + std::round(_scale * CursorOverlayGap));
	}

	void Surface::SetPointerTooltip(StringView text)
	{
		_pointerTooltip = text;
	}

	void Surface::DrawCursorOverlay(Painter& painter)
	{
		IconId icon;
		switch (_cursorOverlay) {
			case CursorOverlay::ResizeHorizontal: icon = StockIcon::CursorResizeHorizontal; break;
			case CursorOverlay::ResizeVertical: icon = StockIcon::CursorResizeVertical; break;
			case CursorOverlay::ResizeDiagonalNWSE: icon = StockIcon::CursorResizeNWSE; break;
			case CursorOverlay::ResizeDiagonalNESW: icon = StockIcon::CursorResizeNESW; break;
			case CursorOverlay::Move: icon = StockIcon::CursorMove; break;
			default: return;
		}

		// Below and to the right of the pointer, out of the way of what it points at, on the other side near the edge
		// of the screen; on whole pixels, so the pixel art stays sharp
		float size = float(_skin.GetIconSize());
		float offset = painter.Px(CursorOverlayOffset);
		Vector2f screenSize = GetScreenSize();
		if (_detachedDrawBounds.W > 0.0f) {
			screenSize = Vector2f(_detachedDrawBounds.X + _detachedDrawBounds.W, _detachedDrawBounds.Y + _detachedDrawBounds.H);
		}
		float x = _pointerPos.X + offset - size * 0.5f;
		float y = _pointerPos.Y + offset - size * 0.5f;
		if (x + size > screenSize.X) {
			// The arrow of the system points up and to the left from its tip, nothing of it is on this side, so closer
			x = _pointerPos.X - offset * 0.5625f - size * 0.5f;
		}
		if (y + size > screenSize.Y) {
			y = _pointerPos.Y - offset * 0.25f - size * 0.5f;
		}
		Rectf rect(std::round(x), std::round(y), size, size);

		float pixel = std::max(1.0f, std::round(size / float(Icons::PixelArtSize)));

		// A soft shadow below and to the right, as the cursors of the system have: the shape with its outline, blurred, by
		// copies a pixel of the art apart that add up where they overlap, so it fades out at its edges
		Vector2f shadow(rect.X + pixel * 2.0f, rect.Y + pixel * 2.0f);
		for (std::int32_t dy = -2; dy <= 2; dy++) {
			for (std::int32_t dx = -2; dx <= 2; dx++) {
				painter.Icon(icon, Rectf(shadow.X + pixel * float(dx), shadow.Y + pixel * float(dy), size, size), Colorf(0.0f, 0.0f, 0.0f, 0.022f), PaintLayer::Back);
			}
		}

		// A dark outline a pixel of the art wide all around, so it's seen on light and dark colors alike
		for (std::int32_t dy = -1; dy <= 1; dy++) {
			for (std::int32_t dx = -1; dx <= 1; dx++) {
				if (dx != 0 || dy != 0) {
					painter.Icon(icon, Rectf(rect.X + pixel * float(dx), rect.Y + pixel * float(dy), size, size), Colorf(0.0f, 0.0f, 0.0f, 0.85f), PaintLayer::Content);
				}
			}
		}
		painter.Icon(icon, rect, Colorf::White, PaintLayer::Overlay);
	}

	void Surface::ShowToast(StringView text, IconId icon)
	{
		if (_toasts.size() >= MaxToasts) {
			_toasts.erase(_toasts.begin());
		}
		Toast& toast = _toasts.emplace_back();
		toast.Text = text;
		toast.Icon = icon;
		toast.TimeLeft = ToastDuration;
	}

	void Surface::NotifyDestroyed(Element* element)
	{
		if (_instance != nullptr) {
			_instance->Forget(element);
		}
	}

	void Surface::OnUpdate(float timeMult)
	{
		UI::Canvas::OnUpdate(timeMult);

		RemoveClosedOverlays();
		// Taken out of the list first, whatever their destruction does sees a list that's whole
		if (!_retired.empty()) {
			SmallVector<std::unique_ptr<Element>, 0> retired = Death::move(_retired);
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
			Element* element = _overlays[i].Element.get();
			element->Update(timeMult);
		}
		UpdateKeyboardAvoidance();

		if (_touchHold == TouchHold::Waiting && _touchDown && _capture != nullptr && !_touchScrolling) {
			// The finger rested on it long enough, it drags it from now on, which the device tells by a short buzz
			_touchHoldTime += timeMult;
			if (_touchHoldTime >= TouchHoldDelay) {
				_touchHold = TouchHold::Held;
				_tooltipShown = false;
				_touchTooltip = false;
				theApplication().Vibrate(12);
				PointerEvent holdEvent;
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

		UpdatePointerLeave();
		UpdateDetached();
		UpdateTooltipWindow();

		// The skins of displays with another scale that no window is on anymore
		_updateFrame++;
		for (std::size_t i = 0; i < _rasterSkins.size(); ) {
			if (_updateFrame - _rasterSkins[i].LastUsed > RasterSkinFrames) {
				_rasterSkins.erase(_rasterSkins.begin() + i);
			} else {
				i++;
			}
		}
	}

	void Surface::UpdatePointerLeave()
	{
#if defined(NCINE_HAS_AUXILIARY_WINDOWS)
		// No event comes when the pointer leaves a window, what is shown by it would stay there
		if (_capture != nullptr || TouchInput || _pointerWindow == PointerOutside) {
			return;
		}
		IGfxDevice& gfxDevice = theApplication().GetGfxDevice();
		Vector2i global;
		if (!gfxDevice.globalMousePosition(global)) {
			return;
		}
		Recti bounds;
		if (_pointerWindow == 0) {
			Vector2i pos = gfxDevice.windowPosition();
			bounds = Recti(pos.X, pos.Y, gfxDevice.width(), gfxDevice.height());
		} else if (AuxiliaryWindow* window = AuxiliaryWindow::FindById(_pointerWindow)) {
			bounds = window->GetBounds();
		}
		if (global.X >= bounds.X && global.Y >= bounds.Y && global.X < bounds.X + bounds.W && global.Y < bounds.Y + bounds.H) {
			return;
		}
		PointerEvent event;
		event.Pos = Vector2f(-1.0e6f, -1.0e6f);
		event.WindowId = PointerOutside;
		OnPointerMove(event);
#endif
	}

	void Surface::UpdateKeyboardAvoidance()
	{
		Element* focus = (KeyboardInset > 0.0f && _focus != nullptr && runtime_cast<TextBox>(_focus) != nullptr && IsShownInTree(_focus) ? _focus : nullptr);
		std::int32_t index = (focus != nullptr ? GetOverlayIndexOf(focus) : -2);
		float keyboardTop = GetScreenSize().Y - KeyboardInset - std::round(8.0f * _scale);
		Rectf box = (focus != nullptr ? focus->Bounds : Rectf());

		// A window is moved up as far as the box needs, measured from where it would be without it, its top stays on the screen
		float shift = 0.0f;
		const Element* window = (index >= 0 ? _overlays[index].Element.get() : nullptr);
		if (window != nullptr) {
			float applied = (window == _keyboardWindow ? _keyboardShift : 0.0f);
			shift = std::max(0.0f, box.Y + box.H + applied - keyboardTop);
			shift = std::round(std::min(shift, std::max(0.0f, box.Y + applied - GetSafeBounds().Y)));
		}
		_keyboardWindow = (shift > 0.0f ? window : nullptr);
		_keyboardShift = shift;

		// The panels are laid out above the keyboard for a box it would cover, as long as a box in them is typed into
		if (index == -1) {
			if (_keyboardRootFocus != focus) {
				_keyboardRootFocus = focus;
				_keyboardRootInset = (_keyboardRootInset > 0.0f || box.Y + box.H > keyboardTop ? KeyboardInset : 0.0f);
				_keyboardScrollFrames = 2;
			} else if (_keyboardRootInset > 0.0f) {
				_keyboardRootInset = KeyboardInset;
			}
			// Once they're laid out so, the list the box is in is scrolled to it
			if (_keyboardScrollFrames > 0 && --_keyboardScrollFrames == 0) {
				EnsureVisible(focus);
			}
		} else {
			_keyboardRootFocus = nullptr;
			_keyboardRootInset = 0.0f;
			_keyboardScrollFrames = 0;
		}
	}

	class Surface::PanelsCanvas : public UI::Canvas
	{
	public:
		explicit PanelsCanvas(Surface& surface)
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
		Surface& _surface;
	};

	SceneNode* Surface::GetPanelsNode()
	{
		if (_panelsCanvas == nullptr) {
			_panelsCanvas = std::make_unique<PanelsCanvas>(*this);
		}
		return _panelsCanvas.get();
	}

	std::uint16_t Surface::GetPanelsDepth()
	{
		return std::uint16_t(Painter::BaseDepth + RootSlot * Painter::SlotDepth);
	}

	std::uint16_t Surface::GetOverlaysDepth()
	{
		return std::uint16_t(Painter::BaseDepth + FirstOverlaySlot * Painter::SlotDepth);
	}

	void Surface::DrawPanelsInto(UI::Canvas& canvas)
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
		Painter painter(canvas, _skin, GetFont(), _scale, _lines.get());
		Element::BeginLayout();
		_drawingInLevel = true;
		DrawRoot(painter, screenSize, true);
		_drawingInLevel = false;

		// What is under a modal window is drawn here too, so the whole blurred picture the modal one is drawn over has it
		_overlaysInLevel = 0;
		std::size_t stacked = GetStackedOverlayCount();
		Rectf backdropBounds;
		const Texture* backdrop = (stacked > 0 && GetBackdrop ? GetBackdrop(BackdropKind::UnderModal, screenSize, backdropBounds, true) : nullptr);
		if (backdrop != nullptr) {
			painter.SetBackdrop(backdrop, backdropBounds, Theme::BackdropWindowFillOpacity, FirstOverlaySlot);
			Rectf safe = GetSafeBounds();
			std::int32_t slot = FirstOverlaySlot;
			for (std::size_t i = 0; i < stacked; i++) {
				if (IsActive(_overlays[i]) && _overlays[i].Detached.Window == nullptr) {
					DrawOverlay(painter, i, slot++, screenSize, safe);
				}
			}
			_overlaysInLevel = stacked;
		}
		Element::EndLayout();
	}

	void Surface::DrawRoot(Painter& painter, Vector2f screenSize, bool inLevel)
	{
		Rectf backdropBounds;
		const Texture* backdrop = (GetBackdrop ? GetBackdrop(BackdropKind::Panels, screenSize, backdropBounds, inLevel) : nullptr);
		// What is shown over the level is drawn before the panels, it's in their picture
		painter.SetBackdrop(backdrop, backdropBounds, Theme::BackdropFillOpacity, RootSlot);

		if (_root != nullptr && _root->IsShown()) {
			_root->Arrange(GetContext(), Rectf(0.0f, 0.0f, screenSize.X, std::max(screenSize.Y - _keyboardRootInset, 1.0f)));
			painter.SetSlot(RootSlot);
			_root->Draw(painter);
		}
	}

	void Surface::DrawOverlay(Painter& painter, std::size_t index, std::int32_t slot, Vector2f screenSize, const Rectf& safe)
	{
		Context context = GetContext();
		Element* element = _overlays[index].Element.get();
		const OverlayOptions& options = _overlays[index].Options;
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
		if (element == _keyboardWindow) {
			pos.Y -= _keyboardShift;
		}
		element->Arrange(context, Rectf(pos.X, pos.Y, size.X, size.Y));

		painter.SetSlot(slot);
		if (options.Modal) {
			painter.FillRect(Rectf(0.0f, 0.0f, screenSize.X, screenSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.45f), PaintLayer::Back);
		}
		element->Draw(painter);
	}

	bool Surface::OnDraw(RenderQueue& renderQueue)
	{
		UI::Canvas::OnDraw(renderQueue);

		Vector2f screenSize = GetScreenSize();
		ViewSize = Vector2i(std::int32_t(screenSize.X), std::int32_t(screenSize.Y));
		ClipRect = Rectf(0.0f, 0.0f, 0.0f, 0.0f);

		bool panelsDrawn = (_panelsDrawnFrame == theApplication().GetFrameCount());
		if (!panelsDrawn) {
			_lines->BeginFrame();
		}
		Painter painter(*this, _skin, GetFont(), _scale, _lines.get());
		// Sizes measured from here on are kept for the frame, nothing changes while it's laid out and drawn
		Element::BeginLayout();

		if (!panelsDrawn) {
			DrawRoot(painter, screenSize, false);
		}
		Rectf overlayBackdropBounds;
		const Texture* overlayBackdrop = (GetBackdrop ? GetBackdrop(BackdropKind::Overlays, screenSize, overlayBackdropBounds, panelsDrawn) : nullptr);
		painter.SetBackdrop(overlayBackdrop, overlayBackdropBounds, Theme::BackdropWindowFillOpacity);

		// Windows and menus stay in the part of the screen that isn't cut off
		Rectf safe = GetSafeBounds();
		std::int32_t slot = FirstOverlaySlot;
		for (std::size_t i = (panelsDrawn ? std::min(_overlaysInLevel, _overlays.size()) : 0); i < _overlays.size(); i++) {
			if (IsActive(_overlays[i]) && _overlays[i].Detached.Window == nullptr) {
				DrawOverlay(painter, i, slot++, screenSize, safe);
			}
		}

		painter.SetSlot(slot);
		DrawToasts(painter);
		// Over a window out of the main window, it was drawn in that window already
		if (_pointerWindow == 0) {
			DrawTooltip(painter, GetTooltipText());
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
			painter.Circle(VirtualCursor, radius, Colorf(1.0f, 1.0f, 1.0f, 0.9f), PaintLayer::Content);
			painter.Circle(VirtualCursor, radius - std::max(1.5f, std::round(_scale * 1.5f)), Colorf(0.1f, 0.08f, 0.2f, 0.6f), PaintLayer::Overlay);
			painter.Circle(VirtualCursor, std::max(1.5f, std::round(_scale * 1.5f)), Theme::Accent, PaintLayer::OverlayText);
		}

		// Over everything, the virtual cursor too, then forgotten, whatever is pointed at asks for it again next frame
		if (_cursorOverlay != CursorOverlay::None && !TouchInput && _pointerWindow == 0) {
			painter.SetSlot(slot + 4);
			DrawCursorOverlay(painter);
		}
		_cursorOverlay = CursorOverlay::None;

		Element::EndLayout();
		return true;
	}

	Element* Surface::HitTestAll(Vector2f pos, bool& blocked)
	{
		blocked = false;
		for (std::size_t i = _overlays.size(); i > 0; i--) {
			Overlay& overlay = _overlays[i - 1];
			if (!IsActive(overlay)) {
				continue;
			}
			// Only what is in the window the pointer is over, a modal element blocks them all
			if (IsInPointerWindow(overlay)) {
				if (Element* hit = overlay.Element->HitTest(pos)) {
					return hit;
				}
				if (overlay.Element->GetHitBounds().Contains(pos)) {
					blocked = true;
					return nullptr;
				}
			}
			if (overlay.Options.Modal) {
				blocked = true;
				return nullptr;
			}
		}
		if (_pointerWindow != 0) {
			// An element of the tree in a window of its own
			for (DetachableElement& detachable : _detachables) {
				if (detachable.Detached.Window != nullptr && detachable.Detached.Window->GetId() == _pointerWindow) {
					if (Element* hit = detachable.Element->HitTest(pos)) {
						return hit;
					}
					break;
				}
			}
			// Over a window out of the main window, which takes the pointer even where nothing of it is
			blocked = true;
			return nullptr;
		}
		return (_root != nullptr ? _root->HitTest(pos) : nullptr);
	}

	void Surface::SetHover(Element* element)
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

	void Surface::RemoveClosedOverlays()
	{
		// Taken out of the list first, and destroyed only then, so whatever their destruction does sees a list that's whole
		SmallVector<std::unique_ptr<Element>, 4> closed;
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

	void Surface::Forget(Element* element)
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
		ForgetDetachables(element, false);
	}

	void Surface::ForgetDetachables(Element* element, bool inside)
	{
		for (std::size_t i = 0; i < _detachables.size(); ) {
			if (inside ? IsInside(_detachables[i].Element, element) : _detachables[i].Element == element) {
				_detachables.erase(_detachables.begin() + i);
			} else {
				i++;
			}
		}
	}

	void Surface::Retire(std::unique_ptr<Element> element)
	{
		if (element == nullptr || _instance == nullptr) {
			// Destroyed at once
			return;
		}
		Surface* surface = _instance;
		Element* retired = element.get();
		// Kept whole until it's destroyed, a callback that took it out of the interface may still be running in it
		surface->_retired.push_back(Death::move(element));
		// Taken away by the parent pointer, IsInside() then finds what is inside of it
		surface->CancelCaptureInside(retired);
		retired->_parent = nullptr;
		retired->_surface = nullptr;
		surface->ForgetInside(retired);
	}

	void Surface::ForgetInside(Element* element)
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
		ForgetDetachables(element, true);
	}

	void Surface::CancelCaptureInside(Element* element)
	{
		if (_capture == nullptr || !IsInside(_capture, element)) {
			return;
		}
		// What is dragged gets its end, a number dragged keeps the value it was dragged to
		Element* capture = _capture;
		_capture = nullptr;
		capture->_pressed = false;
		capture->OnPointerCancel();
	}

	bool Surface::IsInside(const Element* element, const Element* ancestor)
	{
		for (const Element* current = element; current != nullptr; current = current->_parent) {
			if (current == ancestor) {
				return true;
			}
		}
		return false;
	}

	bool Surface::IsShownInTree(Element* element)
	{
		for (Element* current = element; current != nullptr; current = current->_parent) {
			if (!current->IsShown()) {
				return false;
			}
		}
		return true;
	}

	bool Surface::LeavesKeyToShortcuts(const KeyboardEvent& event)
	{
		if (IsKeyLeftByText) {
			return IsKeyLeftByText(event);
		}
		return (event.sym == Keys::F1 || event.sym == Keys::F5);
	}

	StringView Surface::GetTooltipText() const
	{
		if (!_pointerTooltip.empty()) {
			return _pointerTooltip;
		}
		if (_tooltipShown && _hover != nullptr && (_capture == nullptr || _touchTooltip)) {
			return _hover->GetTooltip(_pointerPos);
		}
		return {};
	}

	Vector2f Surface::GetTooltipSize(StringView text) const
	{
		Context context = GetContext();
		float padding = context.Px(Theme::Padding * 1.5f);
		// Less above and below the text than at its sides, the line itself is a little higher than the text already
		float paddingY = std::round(padding * 0.5f);
		// As wide as its longest line and as high as all of its lines, a tooltip may have more of them
		std::int32_t lineCount = 1;
		for (char c : text) {
			if (c == '\n') {
				lineCount++;
			}
		}
		float lineHeight = std::max(context.GetTextHeight(), context.Px(Theme::RowHeight) * 0.75f);
		return Vector2f(std::round(context.MeasureText(text) + padding * 2.0f), std::round(lineHeight * float(lineCount) + paddingY * 2.0f));
	}

	bool Surface::PlaceTooltip(Vector2f size, const Rectf& safe, Rectf& rect) const
	{
		// Below the arrows shown by the pointer, if there are any, not over them
		Vector2f pos(_pointerPos.X + std::round(8.0f * _scale), _pointerPos.Y + GetPointerLabelOffset(std::round(14.0f * _scale)));
		if (TouchInput) {
			// Above the finger, which would cover it below
			pos = Vector2f(_pointerPos.X - size.X * 0.5f, _pointerPos.Y - size.Y - std::round(24.0f * _scale));
		}
		Vector2f preferred = pos;
		bool fits = (size.X <= safe.W && size.Y <= safe.H);
		if (pos.X + size.X > safe.X + safe.W) {
			pos.X = safe.X + safe.W - size.X;
		}
		pos.X = std::max(pos.X, safe.X);
		if (pos.Y + size.Y > safe.Y + safe.H) {
			// Above the pointer then, also above the arrows, which go there too near the bottom of the screen
			pos.Y = _pointerPos.Y - size.Y - GetPointerLabelOffset(std::round(4.0f * _scale));
			fits = (fits && pos.Y >= safe.Y);
		}
		pos.Y = std::max(pos.Y, safe.Y);
		// Where it doesn't fit, it's where it would be without the edges, for a window of its own
		Vector2f placed = (fits ? pos : preferred);
		rect = Rectf(std::round(placed.X), std::round(placed.Y), size.X, size.Y);
		return fits;
	}

	void Surface::DrawTooltipBox(Painter& painter, const Rectf& rect, StringView text)
	{
		float padding = painter.Px(Theme::Padding * 1.5f);
		painter.Panel(rect, true, Theme::PanelHeader, Theme::PanelBorder);
		painter.TextInRect(text, Rectf(rect.X + padding, rect.Y, rect.W - padding * 2.0f, rect.H), Theme::Text, Alignment::Left);
	}

	void Surface::DrawTooltip(Painter& painter, StringView text)
	{
		if (text.empty()) {
			return;
		}
		Rectf safe = (_detachedDrawBounds.W > 0.0f ? _detachedDrawBounds : GetSafeBounds());
		Rectf rect;
		PlaceTooltip(GetTooltipSize(text), safe, rect);
		DrawTooltipBox(painter, rect, text);
	}

	void Surface::UpdateTooltipWindow()
	{
		// A tooltip of an element in a window of its own that doesn't fit in that window gets a window of its own too
		_tooltipWindowText = {};
		DetachedWindow* parent = (CanDetach() && !TouchInput ? FindDetachedWindow(_pointerWindow) : nullptr);
		Rectf rect;
		if (parent != nullptr) {
			StringView text = GetTooltipText();
			if (!text.empty() && !PlaceTooltip(GetTooltipSize(text), parent->GetBounds(), rect)) {
				_tooltipWindowText = text;
			}
		}
		if (_tooltipWindowText.empty()) {
			if (_tooltipWindow != nullptr) {
				// Kept for the next one while there are windows it could be shown over
				bool anyDetached = false;
				for (const Overlay& overlay : _overlays) {
					anyDetached = (anyDetached || overlay.Detached.Window != nullptr);
				}
				for (const DetachableElement& detachable : _detachables) {
					anyDetached = (anyDetached || detachable.Detached.Window != nullptr);
				}
				if (anyDetached) {
					_tooltipWindow->Hide();
				} else {
					_tooltipWindow = nullptr;
				}
			}
			return;
		}

		Vector2i mainPos = theApplication().GetGfxDevice().windowPosition();
		float toDesktop = GetDesktopScale();
		Vector2i desktopPos = parent->ToDesktop(Vector2f(rect.X, rect.Y));
		desktopPos = KeepOnDesktop(desktopPos, Vector2i(std::int32_t(std::round(rect.W * toDesktop)), std::int32_t(std::round(rect.H * toDesktop))), true);
		Rectf bounds(std::round(float(desktopPos.X - mainPos.X) / toDesktop), std::round(float(desktopPos.Y - mainPos.Y) / toDesktop), rect.W, rect.H);
		if (_tooltipWindow == nullptr) {
			// The pointer goes through it, it's never pointed at
			_tooltipWindow = DetachedWindow::Create(nullptr, bounds, desktopPos, toDesktop, true, true, [this](UI::Canvas& canvas, DetachedWindow& window) {
				DrawTooltipWindow(canvas, window);
			});
			if (_tooltipWindow == nullptr) {
				return;
			}
		}
		_tooltipWindow->Place(bounds, desktopPos, toDesktop);
		_tooltipWindow->Show(false);
	}

	void Surface::DrawTooltipWindow(UI::Canvas& canvas, DetachedWindow& window)
	{
		canvas.ViewSize = Vector2i(0, 0);
		canvas.ClipRect = Rectf(0.0f, 0.0f, 0.0f, 0.0f);
		float raster = window.GetRaster();
		canvas.PixelScale = raster;
		window.GetLines()->BeginFrame();
		if (_tooltipWindowText.empty()) {
			return;
		}
		Painter painter(canvas, GetRasterSkin(raster), GetRasterFont(raster), _scale, window.GetLines());
		painter.SetRaster(raster);
		if (!window.HasRoundedCorners()) {
			painter.SetSquareWindow(window.GetBounds());
		}
		painter.SetSlot(FirstOverlaySlot);
		DrawTooltipBox(painter, window.GetBounds(), _tooltipWindowText);
	}

	void Surface::DrawToasts(Painter& painter)
	{
		Rectf safe = GetSafeBounds();
		float padding = painter.Px(Theme::Padding * 1.5f);
		float iconSize = painter.Px(Theme::IconSize);
		float y = safe.Y + safe.H - painter.Px(Theme::StatusBarHeight + 8.0f);

		for (std::size_t i = _toasts.size(); i > 0; i--) {
			const Toast& toast = _toasts[i - 1];
			float alpha = std::min(1.0f, toast.TimeLeft / ToastFadeTime);
			float age = ToastDuration - toast.TimeLeft;
			float slide = (Theme::ReducedMotion ? 0.0f : std::max(0.0f, 1.0f - age / ToastFadeTime));

			Vector2f size(padding * 3.0f + iconSize + painter.MeasureText(toast.Text), std::max(iconSize, painter.Px(Theme::RowHeight)) + padding * 2.0f);
			Rectf rect(std::round(safe.X + (safe.W - size.X) * 0.5f), std::round(y - size.Y + slide * painter.Px(12.0f)), std::round(size.X), std::round(size.Y));

			painter.PushOpacity(alpha);
			painter.Panel(rect, true, Theme::PanelHeader, Theme::PanelBorder);
			// Red for a warning, a message in the colors of its own icon
			Colorf iconColor = (toast.Icon == StockIcon::Warning ? Theme::Danger : Colorf(1.0f, 1.0f, 1.0f, 1.0f));
			painter.Icon(toast.Icon, Rectf(rect.X + padding, rect.Y, iconSize, rect.H), iconColor);
			painter.TextInRect(toast.Text, Rectf(rect.X + padding * 2.0f + iconSize, rect.Y, rect.W, rect.H), Theme::Text);
			painter.PopOpacity();

			y = rect.Y - painter.Px(4.0f);
		}
	}
}

#endif
