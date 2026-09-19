#include "SafeAreaOptionsSection.h"
#include "MenuResources.h"
#include "../HUD.h"
#include "../Font.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>
#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	using Jazz2::SafeAreaEdge;

	// Must match HUD::DefaultRef - the unit the touch button layout is stored in
	static constexpr float DefaultRef = 360.0f;

	// The cropped region is dimmed underneath the menu, so it tints the background without hiding anything; the
	// rectangle and everything that explains it goes on top of the whole menu, because that is what this screen
	// is for
	static constexpr std::uint16_t CroppedLayer = IMenuContainer::BackgroundLayer + 20;
	static constexpr std::uint16_t OverlayLayer = IMenuContainer::FontLayer + 100;

	static constexpr Colorf CroppedColor = Colorf(0.32f, 0.0f, 0.0f, 0.45f);
	static constexpr Colorf SafeColor = Colorf(1.0f, 1.0f, 1.0f, 0.55f);

	SafeAreaOptionsSection::~SafeAreaOptionsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
			if (_root != nullptr) {
				// The menu lays itself out inside the safe area as well (see MenuContainerBase::UpdateContentBounds),
				// but only once the editing is over - the rows would walk away under the finger otherwise
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Layout);
			}
		}
	}

	void SafeAreaOptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		// TRANSLATORS: Header of Options > Graphics > Safe Area section
		SetTitle(_("Safe Area"));

		auto list = std::make_unique<ScrollView>();
		// TRANSLATORS: Menu item in Options > Graphics > Safe Area section
		AddEdgeRow(list.get(), SafeAreaEdge::Left, _("Left Margin"));
		// TRANSLATORS: Menu item in Options > Graphics > Safe Area section
		AddEdgeRow(list.get(), SafeAreaEdge::Top, _("Top Margin"));
		// TRANSLATORS: Menu item in Options > Graphics > Safe Area section
		AddEdgeRow(list.get(), SafeAreaEdge::Right, _("Right Margin"));
		// TRANSLATORS: Menu item in Options > Graphics > Safe Area section
		AddEdgeRow(list.get(), SafeAreaEdge::Bottom, _("Bottom Margin"));
		// TRANSLATORS: Menu item in Options > Graphics > Safe Area section
		list->Add<ListItem>(_("Reset"), [this]() {
			PreferencesCache::ResetSafeArea();
			_isDirty = true;
		}, 36.0f);

		_list = list.get();
		SetContent(std::move(list));
	}

	void SafeAreaOptionsSection::AddEdgeRow(ScrollView* list, SafeAreaEdge edge, StringView label)
	{
		auto* item = list->Add<ChoiceItem>(label,
			[this, edge]() -> StringView { return FormatEdge(edge); },
			[this, edge](std::int32_t direction) { ChangeEdge(edge, direction); });
		// The value reads as "5.0% (36 px)", which is wider than a plain Enabled/Disabled
		item->ArrowSpacing = 16.0f;
		// Tighter than the default row, so the four of them and the Reset below leave as much of the screen
		// showing as possible - the screen is what is being looked at here
		item->Height = 44.0f;
	}

	StringView SafeAreaOptionsSection::FormatEdge(SafeAreaEdge edge)
	{
		std::uint16_t value = PreferencesCache::SafeArea[(std::int32_t)edge];
		if (value == 0) {
			// TRANSLATORS: Value of a margin in Options > Graphics > Safe Area section
			return _("None");
		}

		// The pixel count is the one this display will actually lose, so it comes from the live view size
		std::int32_t pixels = (std::int32_t)PreferencesCache::GetSafeAreaInset(edge, _root->GetViewSize());
		String& result = _edgeValues[(std::int32_t)edge];
		result = format("{}.{}% ({} px)", value / 10, value % 10, pixels);
		return result;
	}

	void SafeAreaOptionsSection::ChangeEdge(SafeAreaEdge edge, std::int32_t direction)
	{
		std::int32_t value = (std::int32_t)PreferencesCache::SafeArea[(std::int32_t)edge] + direction * StepSize;
		// Clamped at both ends rather than wrapped around: rolling a maximised margin back to nothing (or the
		// other way) with a single press is never what was meant
		value = std::clamp<std::int32_t>(value, 0, PreferencesCache::MaxSafeArea);

		if (PreferencesCache::SafeArea[(std::int32_t)edge] != (std::uint16_t)value) {
			PreferencesCache::SafeArea[(std::int32_t)edge] = (std::uint16_t)value;
			_isDirty = true;
		}
	}

	std::int32_t SafeAreaOptionsSection::GetFocusedEdge() const
	{
		if (_draggedEdge >= 0) {
			return _draggedEdge;
		}
		if (_list != nullptr) {
			std::int32_t selected = _list->GetSelectedIndex();
			if (selected >= 0 && selected < (std::int32_t)SafeAreaEdge::Count) {
				return selected;
			}
		}
		return -1;
	}

	void SafeAreaOptionsSection::UpdateHeldAdjustment(float timeMult)
	{
		std::int32_t focusedEdge = GetFocusedEdge();
		bool leftHeld = _root->ActionPressed(PlayerAction::Left);
		bool rightHeld = _root->ActionPressed(PlayerAction::Right);

		// Both directions at once has no sensible answer, and neither has a row that isn't an edge
		if (focusedEdge < 0 || leftHeld == rightHeld) {
			_adjustRepeat = 0.0f;
			_adjustHeld = 0.0f;
			return;
		}

		if (_root->ActionHit(PlayerAction::Left) || _root->ActionHit(PlayerAction::Right)) {
			// The first step of a press belongs to the row itself (ChoiceItem fires on the hit, and plays the
			// sound); this only waits out the delay and then carries it on
			_adjustRepeat = AdjustInitialDelay;
			_adjustHeld = 0.0f;
			return;
		}

		_adjustHeld += timeMult;
		_adjustRepeat -= timeMult;
		if (_adjustRepeat <= 0.0f) {
			_adjustRepeat = (_adjustHeld >= AdjustFastAfter ? AdjustFastInterval : AdjustRepeatInterval);
			// Silently: at this rate the row's own click would be a rattle
			ChangeEdge((SafeAreaEdge)focusedEdge, leftHeld ? -1 : 1);
		}
	}

	void SafeAreaOptionsSection::OnUpdate(float timeMult)
	{
		_pulseTime += timeMult * 0.07f;
		UpdateHeldAdjustment(timeMult);

		// Goes last: the back action leaves the section, which destroys this, so nothing of it may be touched
		// afterwards
		WidgetSection::OnUpdate(timeMult);
	}

	void SafeAreaOptionsSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		Edges safe = GetSafeEdges(viewSize);
		std::int32_t focusedEdge = GetFocusedEdge();
		float pulseAlpha = 0.5f + 0.5f * sinf(_pulseTime * fPiOver2);

		DrawCroppedRegion(safe, viewSize);

		WidgetSection::OnDraw(canvas);

#if defined(NCINE_HAS_TOUCH_CONTROLS)
		if (_root->IsTouchInputActive()) {
			DrawTouchButtonGhosts(viewSize);

			// What the HUD is actually left with, which the safe rectangle alone doesn't say while there are
			// buttons on screen for it to keep clear of. Only worth a line of its own when it differs.
			Edges hud = GetHudEdges(viewSize);
			if (hud.Left > safe.Left + 1.0f || hud.Right < safe.Right - 1.0f) {
				DrawOutline(hud, OverlayLayer, 1.0f, Colorf(0.35f, 0.65f, 1.0f, 0.4f));
			}
		}
#endif

		std::int32_t charOffset = 0;
		DrawSafeOutline(safe, focusedEdge, pulseAlpha);
		DrawEdgeLabels(safe, focusedEdge, charOffset);
	}

	SafeAreaOptionsSection::Edges SafeAreaOptionsSection::GetSafeEdges(Vector2i viewSize) const
	{
		Edges result;
		result.Left = PreferencesCache::GetSafeAreaInset(SafeAreaEdge::Left, viewSize);
		result.Top = PreferencesCache::GetSafeAreaInset(SafeAreaEdge::Top, viewSize);
		result.Right = (float)viewSize.X - PreferencesCache::GetSafeAreaInset(SafeAreaEdge::Right, viewSize);
		result.Bottom = (float)viewSize.Y - PreferencesCache::GetSafeAreaInset(SafeAreaEdge::Bottom, viewSize);
		return result;
	}

	SafeAreaOptionsSection::Edges SafeAreaOptionsSection::GetHudEdges(Vector2i viewSize) const
	{
		float left = 0.0f;
		float right = (float)viewSize.X;

#if defined(NCINE_HAS_TOUCH_CONTROLS)
		// The same margins HUD::OnDraw() carves out while the touch controls are on screen. They are taken off
		// first and the safe area is taken off what is left, so what the player sets here is the distance the
		// HUD keeps from the buttons. Under the same condition the real ones appear under, not merely on a
		// platform that could have a touchscreen - a desktop television box is one of those and has no buttons
		// on screen to keep away from
		if (_root->IsTouchInputActive()) {
			const auto& dpadLayout = PreferencesCache::TouchButtons[(std::size_t)Jazz2::TouchButtonSlot::Dpad];
			left = dpadLayout.EdgeOffset.X + HUD::DpadSize * DefaultRef * dpadLayout.Scale + 8.0f;

			float rightMargin = 0.0f;
			for (Jazz2::TouchButtonSlot slot : { Jazz2::TouchButtonSlot::Fire, Jazz2::TouchButtonSlot::Jump, Jazz2::TouchButtonSlot::Run }) {
				const auto& sl = PreferencesCache::TouchButtons[(std::size_t)slot];
				if (sl.Anchor == Jazz2::TouchButtonAnchor::BottomRight || sl.Anchor == Jazz2::TouchButtonAnchor::TopRight) {
					rightMargin = std::max(rightMargin, sl.EdgeOffset.X + HUD::ButtonSize * DefaultRef * sl.Scale + 8.0f);
				}
			}
			right -= rightMargin;
		}
#endif

		Edges safe = GetSafeEdges(viewSize);
		Edges result;
		result.Left = left + safe.Left;
		result.Top = safe.Top;
		result.Right = right - ((float)viewSize.X - safe.Right);
		result.Bottom = safe.Bottom;

		// A layout that leaves nothing between the buttons would otherwise hand out an inverted rectangle
		if (result.Right < result.Left) {
			result.Right = result.Left;
		}
		if (result.Bottom < result.Top) {
			result.Bottom = result.Top;
		}
		return result;
	}

	void SafeAreaOptionsSection::DrawCroppedRegion(const Edges& safe, Vector2i viewSize)
	{
		float viewW = (float)viewSize.X;
		float viewH = (float)viewSize.Y;

		if (safe.Top > 0.0f) {
			_root->DrawSolid(0.0f, 0.0f, CroppedLayer, Alignment::TopLeft, Vector2f(viewW, safe.Top), CroppedColor);
		}
		if (safe.Bottom < viewH) {
			_root->DrawSolid(0.0f, safe.Bottom, CroppedLayer, Alignment::TopLeft, Vector2f(viewW, viewH - safe.Bottom), CroppedColor);
		}
		float bandHeight = safe.Bottom - safe.Top;
		if (bandHeight > 0.0f) {
			if (safe.Left > 0.0f) {
				_root->DrawSolid(0.0f, safe.Top, CroppedLayer, Alignment::TopLeft, Vector2f(safe.Left, bandHeight), CroppedColor);
			}
			if (safe.Right < viewW) {
				_root->DrawSolid(safe.Right, safe.Top, CroppedLayer, Alignment::TopLeft, Vector2f(viewW - safe.Right, bandHeight), CroppedColor);
			}
		}
	}

	void SafeAreaOptionsSection::DrawSafeOutline(const Edges& safe, std::int32_t focusedEdge, float pulseAlpha)
	{
		DrawOutline(safe, OverlayLayer, 1.0f, SafeColor);

		// Corner brackets, which is what the eye lines up against the corner of the picture
		constexpr float BracketLength = 16.0f;
		constexpr float BracketThickness = 2.0f;
		constexpr Colorf BracketColor = Colorf(1.0f, 1.0f, 1.0f, 0.85f);
		float armX = std::min(BracketLength, (safe.Right - safe.Left) * 0.5f);
		float armY = std::min(BracketLength, (safe.Bottom - safe.Top) * 0.5f);
		if (armX > 0.0f && armY > 0.0f) {
			_root->DrawSolid(safe.Left, safe.Top, OverlayLayer + 2, Alignment::TopLeft, Vector2f(armX, BracketThickness), BracketColor);
			_root->DrawSolid(safe.Left, safe.Top, OverlayLayer + 2, Alignment::TopLeft, Vector2f(BracketThickness, armY), BracketColor);
			_root->DrawSolid(safe.Right - armX, safe.Top, OverlayLayer + 2, Alignment::TopLeft, Vector2f(armX, BracketThickness), BracketColor);
			_root->DrawSolid(safe.Right - BracketThickness, safe.Top, OverlayLayer + 2, Alignment::TopLeft, Vector2f(BracketThickness, armY), BracketColor);
			_root->DrawSolid(safe.Left, safe.Bottom - BracketThickness, OverlayLayer + 2, Alignment::TopLeft, Vector2f(armX, BracketThickness), BracketColor);
			_root->DrawSolid(safe.Left, safe.Bottom - armY, OverlayLayer + 2, Alignment::TopLeft, Vector2f(BracketThickness, armY), BracketColor);
			_root->DrawSolid(safe.Right - armX, safe.Bottom - BracketThickness, OverlayLayer + 2, Alignment::TopLeft, Vector2f(armX, BracketThickness), BracketColor);
			_root->DrawSolid(safe.Right - BracketThickness, safe.Bottom - armY, OverlayLayer + 2, Alignment::TopLeft, Vector2f(BracketThickness, armY), BracketColor);
		}

		if (focusedEdge < 0) {
			return;
		}

		// The edge being changed pulses, so it is clear which of the four lines the row belongs to
		Colorf focusColor = Colorf(0.35f, 0.65f, 1.0f, 0.4f + 0.55f * pulseAlpha);
		switch ((SafeAreaEdge)focusedEdge) {
			case SafeAreaEdge::Left:
				_root->DrawSolid(safe.Left, safe.Top, OverlayLayer + 4, Alignment::TopLeft, Vector2f(2.0f, safe.Bottom - safe.Top), focusColor);
				break;
			case SafeAreaEdge::Top:
				_root->DrawSolid(safe.Left, safe.Top, OverlayLayer + 4, Alignment::TopLeft, Vector2f(safe.Right - safe.Left, 2.0f), focusColor);
				break;
			case SafeAreaEdge::Right:
				_root->DrawSolid(safe.Right - 2.0f, safe.Top, OverlayLayer + 4, Alignment::TopLeft, Vector2f(2.0f, safe.Bottom - safe.Top), focusColor);
				break;
			case SafeAreaEdge::Bottom:
				_root->DrawSolid(safe.Left, safe.Bottom - 2.0f, OverlayLayer + 4, Alignment::TopLeft, Vector2f(safe.Right - safe.Left, 2.0f), focusColor);
				break;
		}
	}

	void SafeAreaOptionsSection::DrawEdgeLabels(const Edges& safe, std::int32_t focusedEdge, std::int32_t& charOffset)
	{
		if (focusedEdge < 0) {
			return;
		}

		std::uint16_t value = PreferencesCache::SafeArea[focusedEdge];
		char buffer[24];
		std::size_t length = formatInto(buffer, "{}.{}%", value / 10, value % 10);
		StringView text = { buffer, length };

		constexpr Colorf LabelColor = Colorf(0.6f, 0.5f, 0.4f, 0.5f);
		// The left and right readouts sit high rather than halfway down the edge, where they would land on top
		// of the rows they belong to
		float sideY = safe.Top + (safe.Bottom - safe.Top) * 0.3f;
		float centerX = (safe.Left + safe.Right) * 0.5f;

		switch ((SafeAreaEdge)focusedEdge) {
			case SafeAreaEdge::Left:
				_root->DrawStringShadow(text, charOffset, safe.Left + 5.0f, sideY, OverlayLayer + 6,
					Alignment::Left, LabelColor, 0.72f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
				break;
			case SafeAreaEdge::Top:
				_root->DrawStringShadow(text, charOffset, centerX, safe.Top + 4.0f, OverlayLayer + 6,
					Alignment::Top, LabelColor, 0.72f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
				break;
			case SafeAreaEdge::Right:
				_root->DrawStringShadow(text, charOffset, safe.Right - 5.0f, sideY, OverlayLayer + 6,
					Alignment::Right, LabelColor, 0.72f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
				break;
			case SafeAreaEdge::Bottom:
				_root->DrawStringShadow(text, charOffset, centerX, safe.Bottom - 4.0f, OverlayLayer + 6,
					Alignment::Bottom, LabelColor, 0.72f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
				break;
		}
	}

	void SafeAreaOptionsSection::DrawOutline(const Edges& r, std::uint16_t z, float thickness, const Colorf& color)
	{
		float width = r.Right - r.Left;
		float height = r.Bottom - r.Top;
		if (width <= 0.0f || height <= 0.0f) {
			return;
		}

		_root->DrawSolid(r.Left, r.Top, z, Alignment::TopLeft, Vector2f(width, thickness), color);
		_root->DrawSolid(r.Left, r.Bottom - thickness, z, Alignment::TopLeft, Vector2f(width, thickness), color);
		_root->DrawSolid(r.Left, r.Top + thickness, z, Alignment::TopLeft, Vector2f(thickness, height - thickness * 2.0f), color);
		_root->DrawSolid(r.Right - thickness, r.Top + thickness, z, Alignment::TopLeft, Vector2f(thickness, height - thickness * 2.0f), color);
	}

#if defined(NCINE_HAS_TOUCH_CONTROLS)
	void SafeAreaOptionsSection::DrawTouchButtonGhosts(Vector2i viewSize)
	{
		constexpr Colorf GhostColor = Colorf(1.0f, 1.0f, 1.0f, 0.22f);

		for (std::int32_t i = 0; i < (std::int32_t)Jazz2::TouchButtonSlot::Count; i++) {
			auto slot = (Jazz2::TouchButtonSlot)i;
			const auto& layout = PreferencesCache::TouchButtons[i];

			AnimState anim;
			float defaultSize;
			switch (slot) {
				case Jazz2::TouchButtonSlot::Dpad: anim = TouchDpad; defaultSize = HUD::DpadSize; break;
				case Jazz2::TouchButtonSlot::Fire: anim = TouchFire; defaultSize = HUD::ButtonSize; break;
				case Jazz2::TouchButtonSlot::Jump: anim = TouchJump; defaultSize = HUD::ButtonSize; break;
				case Jazz2::TouchButtonSlot::Run: anim = TouchRun; defaultSize = HUD::ButtonSize; break;
				case Jazz2::TouchButtonSlot::ChangeWeapon: anim = TouchChange; defaultSize = HUD::SmallButtonSize; break;
				case Jazz2::TouchButtonSlot::Menu: anim = TouchPause; defaultSize = HUD::SmallButtonSize; break;
				default: anim = TouchClose; defaultSize = HUD::SmallButtonSize; break;
			}

			// The native size of the sprite the menu draws, which is not the size the button is laid out at
			float nativeHalf;
			switch (slot) {
				case Jazz2::TouchButtonSlot::Dpad: nativeHalf = 46.0f; break;
				case Jazz2::TouchButtonSlot::Fire:
				case Jazz2::TouchButtonSlot::Jump:
				case Jazz2::TouchButtonSlot::Run: nativeHalf = 35.0f; break;
				case Jazz2::TouchButtonSlot::Console: nativeHalf = 17.5f; break;
				default: nativeHalf = 20.0f; break;
			}

			float halfSize = defaultSize * DefaultRef * layout.Scale * 0.5f;
			float cx, cy;
			switch (layout.Anchor) {
				case Jazz2::TouchButtonAnchor::BottomLeft:
					cx = layout.EdgeOffset.X + halfSize;
					cy = (float)viewSize.Y - layout.EdgeOffset.Y - halfSize;
					break;
				case Jazz2::TouchButtonAnchor::BottomRight:
					cx = (float)viewSize.X - layout.EdgeOffset.X - halfSize;
					cy = (float)viewSize.Y - layout.EdgeOffset.Y - halfSize;
					break;
				case Jazz2::TouchButtonAnchor::TopLeft:
					cx = layout.EdgeOffset.X + halfSize;
					cy = layout.EdgeOffset.Y + halfSize;
					break;
				case Jazz2::TouchButtonAnchor::TopCenter:
					cx = (float)viewSize.X * 0.5f;
					cy = layout.EdgeOffset.Y + halfSize;
					break;
				default:
					cx = (float)viewSize.X - layout.EdgeOffset.X - halfSize;
					cy = layout.EdgeOffset.Y + halfSize;
					break;
			}

			float spriteScale = halfSize / nativeHalf;
			_root->DrawElement(anim, -1, cx, cy, OverlayLayer - 2, Alignment::Center, GhostColor, spriteScale, spriteScale);
		}
	}
#endif

	void SafeAreaOptionsSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		switch (event.type) {
			case TouchEventType::Down:
			case TouchEventType::PointerDown: {
				if (_draggedPointerId != -1) {
					break;
				}
				std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex == -1) {
					break;
				}

				float px = event.pointers[pointerIndex].x * (float)viewSize.X;
				float py = event.pointers[pointerIndex].y * (float)viewSize.Y;
				Edges safe = GetSafeEdges(viewSize);

				// The nearest of the four lines the finger is within reach of, so that a corner takes the edge
				// that was actually aimed at
				std::int32_t bestEdge = -1;
				float bestDistance = GrabDistance;
				bool inRows = (py >= safe.Top - GrabDistance && py <= safe.Bottom + GrabDistance);
				bool inColumns = (px >= safe.Left - GrabDistance && px <= safe.Right + GrabDistance);
				auto consider = [&](SafeAreaEdge edge, float distance, bool inSpan) {
					if (inSpan && distance < bestDistance) {
						bestDistance = distance;
						bestEdge = (std::int32_t)edge;
					}
				};
				consider(SafeAreaEdge::Left, std::abs(px - safe.Left), inRows);
				consider(SafeAreaEdge::Right, std::abs(px - safe.Right), inRows);
				consider(SafeAreaEdge::Top, std::abs(py - safe.Top), inColumns);
				consider(SafeAreaEdge::Bottom, std::abs(py - safe.Bottom), inColumns);

				if (bestEdge >= 0) {
					_draggedEdge = bestEdge;
					_draggedPointerId = event.actionIndex;
					if (_list != nullptr) {
						// Keep the row in step with the line being dragged, so the two never disagree
						_list->SetSelectedIndex(bestEdge, false);
					}
					_root->PlaySfx("MenuSelect"_s, 0.4f);
					return;
				}
				break;
			}
			case TouchEventType::Move: {
				if (_draggedEdge < 0) {
					break;
				}
				std::int32_t pointerIndex = event.findPointerIndex(_draggedPointerId);
				if (pointerIndex == -1) {
					break;
				}

				float px = event.pointers[pointerIndex].x * (float)viewSize.X;
				float py = event.pointers[pointerIndex].y * (float)viewSize.Y;

				float inset;
				switch ((SafeAreaEdge)_draggedEdge) {
					case SafeAreaEdge::Left: inset = px; break;
					case SafeAreaEdge::Top: inset = py; break;
					case SafeAreaEdge::Right: inset = (float)viewSize.X - px; break;
					default: inset = (float)viewSize.Y - py; break;
				}

				float extent = ((SafeAreaEdge)_draggedEdge == SafeAreaEdge::Left || (SafeAreaEdge)_draggedEdge == SafeAreaEdge::Right
					? (float)viewSize.X : (float)viewSize.Y);
				std::int32_t value = (extent > 0.0f ? (std::int32_t)std::round(inset * 1000.0f / extent) : 0);
				value = std::clamp<std::int32_t>(value, 0, PreferencesCache::MaxSafeArea);

				if (PreferencesCache::SafeArea[_draggedEdge] != (std::uint16_t)value) {
					PreferencesCache::SafeArea[_draggedEdge] = (std::uint16_t)value;
					_isDirty = true;
				}
				return;
			}
			case TouchEventType::Up:
			case TouchEventType::PointerUp: {
				if (_draggedEdge >= 0 && event.actionIndex == _draggedPointerId) {
					_draggedEdge = -1;
					_draggedPointerId = -1;
					return;
				}
				break;
			}
			default:
				break;
		}

		if (_draggedEdge < 0) {
			WidgetSection::OnTouchEvent(event, viewSize);
		}
	}
}
