#include "TouchControlsOptionsSection.h"
#include "MenuResources.h"
#include "../HUD.h"
#include "../../PreferencesCache.h"
#include "../../LevelHandler.h"

#include "../../../nCine/I18n.h"

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	using Jazz2::TouchButtonSlot;
	using Jazz2::TouchButtonAnchor;
	using Jazz2::TouchButtonLayout;

	static constexpr float DefaultRef = 360.0f;			// = LevelHandler::DefaultWidth * 0.5f
	static constexpr float DockThreshold = 0.5f;
	static constexpr float HandleRadius = 15.0f;

	TouchControlsOptionsSection::TouchControlsOptionsSection()
		: _focusedSlot(-1), _editMode(EditMode::None), _pulseTime(0.0f),
			_primaryPointerId(-1), _primaryStartX(0.0f), _primaryStartY(0.0f),
			_slotEdgeOffsetAtStart(0.0f, 0.0f), _slotAnchorAtStart(TouchButtonAnchor::BottomLeft),
			_resizeCenterX(0.0f), _resizeCenterY(0.0f),
			_secondaryPointerId(-1), _pinchStartDist(1.0f), _pinchStartScale(1.0f),
			_resizingViaCorner(false), _cornerHandleX(0.0f), _cornerHandleY(0.0f),
			_tapDownTime(-1.0f), _tapDownX(0.0f), _tapDownY(0.0f), _isDirty(false)
	{
		for (std::int32_t i = 0; i < (std::int32_t)TouchButtonSlot::Count; i++) {
			_bounceAnim[i] = 0.0f;
		}
	}

	TouchControlsOptionsSection::~TouchControlsOptionsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
			if (_root != nullptr) {
				_root->ApplyPreferencesChanges(ChangedPreferencesType::TouchButtons);
			}
		}
	}

	void TouchControlsOptionsSection::OnUpdate(float timeMult)
	{
		if (_root->ActionHit(PlayerAction::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		}

		_pulseTime += timeMult * 0.07f;

		for (std::int32_t i = 0; i < (std::int32_t)TouchButtonSlot::Count; i++) {
			if (_bounceAnim[i] > 0.0f) {
				_bounceAnim[i] = std::max(0.0f, _bounceAnim[i] - timeMult * 0.06f);
			}
		}

		// Update tap detection timer
		if (_tapDownTime >= 0.0f) {
			_tapDownTime += timeMult;
		}
	}

	void TouchControlsOptionsSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;

		// Dark full-screen background
		_root->DrawSolid(0.0f, 0.0f, IMenuContainer::FontLayer + 225, Alignment::TopLeft,
			Vector2f((float)viewSize.X, (float)viewSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.6f));

		// Top bar
		_root->DrawSolid(0.0f, 0.0f, IMenuContainer::FontLayer + 230, Alignment::TopLeft,
			Vector2f((float)viewSize.X, 68.0f), Colorf(0.0f, 0.0f, 0.0f, 0.6f));

		std::int32_t charOffset = 0;

		// Title (centered, tap to go back)
		_root->DrawStringShadow(_("Touch Controls"), charOffset, (float)viewSize.X * 0.5f, 14.0f,
			IMenuContainer::FontLayer + 235, Alignment::Center,
			Colorf(0.46f, 0.46f, 0.46f, 0.9f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		// Reset and Joystick buttons — centered horizontally, below title
		// Layout: [Reset 60px] [8px gap] [Joystick 96px]  total = 164px
		float btnCenterX = (float)viewSize.X * 0.5f;
		float resetLeft = btnCenterX - 82.0f;
		float joyLeft   = btnCenterX - 82.0f + 68.0f;	// 60 + 8 gap
		constexpr float BtnY = 30.0f;
		constexpr float BtnH = 16.0f;

		_root->DrawSolid(resetLeft, BtnY, IMenuContainer::FontLayer + 240, Alignment::TopLeft,
			Vector2f(60.0f, BtnH), Colorf(0.7f, 0.2f, 0.2f, 0.85f));
		_root->DrawStringShadow(_("Reset"), charOffset, resetLeft + 30.0f, BtnY + BtnH * 0.5f + 1.0f,
			IMenuContainer::FontLayer + 245, Alignment::Center,
			Colorf(0.6f, 0.46f, 0.46f, 0.5f), 0.72f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

		bool joystickMode = PreferencesCache::EnableTouchJoystick;
		Colorf joyCol = joystickMode ? Colorf(0.2f, 0.65f, 0.2f, 0.85f) : Colorf(0.28f, 0.28f, 0.28f, 0.85f);
		_root->DrawSolid(joyLeft, BtnY, IMenuContainer::FontLayer + 240, Alignment::TopLeft,
			Vector2f(96.0f, BtnH), joyCol);
		StringView joyLabel = joystickMode ? _("Joystick ON") : _("Joystick OFF");
		_root->DrawStringShadow(joyLabel, charOffset, joyLeft + 48.0f, BtnY + BtnH * 0.5f + 1.0f,
			IMenuContainer::FontLayer + 245, Alignment::Center,
			Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.72f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

		// Hint
		_root->DrawStringShadow(_("Drag to move · Pinch or corner to resize"), charOffset,
			(float)viewSize.X * 0.5f, 64.0f, IMenuContainer::FontLayer + 235,
			Alignment::Bottom, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.66f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

		// Draw all button previews
		float pulseAlpha = 0.5f + 0.5f * sinf(_pulseTime * fPiOver2);
		for (std::int32_t i = 0; i < (std::int32_t)TouchButtonSlot::Count; i++) {
			DrawButtonPreview(canvas, (TouchButtonSlot)i, viewSize, (i == _focusedSlot));
		}

		// Focused button extras
		if (_focusedSlot >= 0) {
			ButtonRect r = GetButtonRect((TouchButtonSlot)_focusedSlot, viewSize);

			// Pulsing outline (2px thick)
			Colorf outlineColor(1.0f, 1.0f, 1.0f, 0.30f + 0.55f * pulseAlpha);
			DrawOutlineRect(r.CenterX, r.CenterY, r.HalfW, r.HalfH,
				IMenuContainer::FontLayer + 260, 2.0f, outlineColor);
			// Outer glow ring
			DrawOutlineRect(r.CenterX, r.CenterY, r.HalfW + 4.0f, r.HalfH + 4.0f,
				IMenuContainer::FontLayer + 258, 4.0f, Colorf(1.0f, 1.0f, 1.0f, 0.2f * pulseAlpha));

			// Corner resize handle (smaller)
			_root->DrawSolid(_cornerHandleX - 6.0f, _cornerHandleY - 6.0f,
				IMenuContainer::FontLayer + 265, Alignment::TopLeft,
				Vector2f(12.0f, 12.0f), Colorf(1.0f, 1.0f, 1.0f, 0.95f));
			_root->DrawSolid(_cornerHandleX - 4.0f, _cornerHandleY - 4.0f,
				IMenuContainer::FontLayer + 267, Alignment::TopLeft,
				Vector2f(8.0f, 8.0f), Colorf(0.35f, 0.65f, 1.0f, 0.95f));
		}
	}

	void TouchControlsOptionsSection::DrawButtonPreview(Canvas* /*canvas*/, TouchButtonSlot slot,
		Vector2i viewSize, bool isFocused) const
	{
		ButtonRect r = GetButtonRect(slot, viewSize);
		if (r.HalfW < 4.0f) {
			return;
		}

		// Bounce scale
		float bAnim = _bounceAnim[(std::int32_t)slot];
		float bs = 1.0f + 0.12f * sinf(bAnim * fPi) * bAnim;
		float hw = r.HalfW * bs;

		// Map slot to the matching touch button animation in MenuResources
		AnimState anim;
		float nativeHalf;	// half of the sprite's native pixel size
		switch (slot) {
			case TouchButtonSlot::Fire:			anim = TouchFire;   nativeHalf = 35.0f; break;
			case TouchButtonSlot::Jump:			anim = TouchJump;   nativeHalf = 35.0f; break;
			case TouchButtonSlot::Run:			anim = TouchRun;    nativeHalf = 35.0f; break;
			case TouchButtonSlot::ChangeWeapon:	anim = TouchChange; nativeHalf = 20.0f; break;
			case TouchButtonSlot::Menu:			anim = TouchPause;  nativeHalf = 20.0f; break;
			case TouchButtonSlot::Console:		anim = TouchClose;  nativeHalf = 17.5f; break;
			default: /* D-pad */				anim = TouchDpad;   nativeHalf = 46.0f; break;
		}

		float spriteScale = hw / nativeHalf;
		float alpha = (isFocused ? 1.0f : 0.9f);
		_root->DrawElement(anim, -1, r.CenterX, r.CenterY, IMenuContainer::FontLayer + 250,
			Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, alpha), spriteScale, spriteScale);
	}

	void TouchControlsOptionsSection::DrawOutlineRect(float cx, float cy, float hw, float hh,
		std::uint16_t z, float thickness, Colorf color)
	{
		_root->DrawSolid(cx - hw, cy - hh,          z, Alignment::TopLeft, Vector2f(hw * 2.0f, thickness), color);
		_root->DrawSolid(cx - hw, cy + hh - thickness, z, Alignment::TopLeft, Vector2f(hw * 2.0f, thickness), color);
		_root->DrawSolid(cx - hw, cy - hh + thickness,          z, Alignment::TopLeft, Vector2f(thickness, (hh - thickness) * 2.0f), color);
		_root->DrawSolid(cx + hw - thickness, cy - hh + thickness, z, Alignment::TopLeft, Vector2f(thickness, (hh - thickness) * 2.0f), color);
	}

	TouchControlsOptionsSection::ButtonRect TouchControlsOptionsSection::GetButtonRect(
		TouchButtonSlot slot, Vector2i viewSize) const
	{
		const auto& layout = PreferencesCache::TouchButtons[(std::int32_t)slot];
		float halfSize = GetDefaultHalfSize(slot) * layout.Scale;
		float cx, cy;

		switch (layout.Anchor) {
			case TouchButtonAnchor::BottomLeft:
				cx = layout.EdgeOffset.X + halfSize;
				cy = (float)viewSize.Y - layout.EdgeOffset.Y - halfSize;
				break;
			case TouchButtonAnchor::BottomRight:
				cx = (float)viewSize.X - layout.EdgeOffset.X - halfSize;
				cy = (float)viewSize.Y - layout.EdgeOffset.Y - halfSize;
				break;
			case TouchButtonAnchor::TopLeft:
				cx = layout.EdgeOffset.X + halfSize;
				cy = layout.EdgeOffset.Y + halfSize;
				break;
			case TouchButtonAnchor::TopRight:
			default:
				cx = (float)viewSize.X - layout.EdgeOffset.X - halfSize;
				cy = layout.EdgeOffset.Y + halfSize;
				break;
		}
		return { cx, cy, halfSize, halfSize };
	}

	float TouchControlsOptionsSection::GetDefaultHalfSize(TouchButtonSlot slot)
	{
		switch (slot) {
			case TouchButtonSlot::Dpad: return HUD::DpadSize * DefaultRef * 0.5f;
			case TouchButtonSlot::Fire:
			case TouchButtonSlot::Jump:
			case TouchButtonSlot::Run:  return HUD::ButtonSize * DefaultRef * 0.5f;
			default:                    return HUD::SmallButtonSize * DefaultRef * 0.5f;
		}
	}

	void TouchControlsOptionsSection::ClampEdgeOffset(TouchButtonLayout& layout, Vector2i viewSize)
	{
		layout.EdgeOffset.X = std::clamp(layout.EdgeOffset.X, -150.0f, (float)viewSize.X * 0.75f);
		layout.EdgeOffset.Y = std::clamp(layout.EdgeOffset.Y, -150.0f, (float)viewSize.Y * 0.75f);
	}

	void TouchControlsOptionsSection::ReAnchor(TouchButtonLayout& layout, float cx, float cy,
		float hw, float hh, Vector2i viewSize)
	{
		bool right  = (cx >= (float)viewSize.X * DockThreshold);
		bool bottom = (cy >= (float)viewSize.Y / 3.0f);	// top 1/3 snaps to top, rest to bottom

		TouchButtonAnchor newAnchor;
		float ex, ey;

		if (!right && !bottom) {
			newAnchor = TouchButtonAnchor::TopLeft;
			ex = cx - hw;
			ey = cy - hh;
		} else if (right && !bottom) {
			newAnchor = TouchButtonAnchor::TopRight;
			ex = (float)viewSize.X - (cx + hw);
			ey = cy - hh;
		} else if (!right && bottom) {
			newAnchor = TouchButtonAnchor::BottomLeft;
			ex = cx - hw;
			ey = (float)viewSize.Y - (cy + hh);
		} else {
			newAnchor = TouchButtonAnchor::BottomRight;
			ex = (float)viewSize.X - (cx + hw);
			ey = (float)viewSize.Y - (cy + hh);
		}

		layout.Anchor = newAnchor;
		layout.EdgeOffset.X = std::round(ex);
		layout.EdgeOffset.Y = std::round(ey);
	}

	Vector2f TouchControlsOptionsSection::GetCornerHandlePos(TouchButtonSlot slot, Vector2i viewSize)
	{
		ButtonRect r = GetButtonRect(slot, viewSize);
		switch (PreferencesCache::TouchButtons[(int)slot].Anchor) {
			case TouchButtonAnchor::BottomLeft:  return Vector2f(r.CenterX + r.HalfW, r.CenterY - r.HalfH);
			case TouchButtonAnchor::BottomRight: return Vector2f(r.CenterX - r.HalfW, r.CenterY - r.HalfH);
			case TouchButtonAnchor::TopLeft:     return Vector2f(r.CenterX + r.HalfW, r.CenterY + r.HalfH);
			default:                             return Vector2f(r.CenterX - r.HalfW, r.CenterY + r.HalfH);
		}
	}

	void TouchControlsOptionsSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		// Layout constants mirrored from OnDraw
		float btnCenterX = (float)viewSize.X * 0.5f;
		float resetLeft = btnCenterX - 82.0f;
		float joyLeft   = btnCenterX - 82.0f + 68.0f;
		constexpr float BtnY = 30.0f;
		constexpr float BtnH = 16.0f;

		switch (event.type) {
			case TouchEventType::Down:
			case TouchEventType::PointerDown: {
				std::int32_t pi = event.findPointerIndex(event.actionIndex);
				if (pi == -1) break;

				float px = event.pointers[pi].x * (float)viewSize.X;
				float py = event.pointers[pi].y * (float)viewSize.Y;

				// Second finger while dragging → start pinch (any position)
				if (_primaryPointerId != -1 && _secondaryPointerId == -1 && _focusedSlot >= 0) {
					_secondaryPointerId = event.actionIndex;
					std::int32_t pIdx = event.findPointerIndex(_primaryPointerId);
					if (pIdx != -1) {
						float dx = (event.pointers[pi].x - event.pointers[pIdx].x) * (float)viewSize.X;
						float dy = (event.pointers[pi].y - event.pointers[pIdx].y) * (float)viewSize.Y;
						_pinchStartDist = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
						_pinchStartScale = PreferencesCache::TouchButtons[_focusedSlot].Scale;
						_editMode = EditMode::Resizing;
					}
					break;
				}

				if (_primaryPointerId != -1) break;

				// Corner handle hit on focused button (any position, including top bar)
				if (_focusedSlot >= 0) {
					float dx = px - _cornerHandleX;
					float dy = py - _cornerHandleY;
					if (dx * dx + dy * dy <= HandleRadius * HandleRadius) {
						ButtonRect r = GetButtonRect((TouchButtonSlot)_focusedSlot, viewSize);
						float cdx = _cornerHandleX - r.CenterX;
						float cdy = _cornerHandleY - r.CenterY;

						_primaryPointerId = event.actionIndex;
						_primaryStartX = px;
						_primaryStartY = py;
						_pinchStartDist = std::max(1.0f, std::sqrt(cdx * cdx + cdy * cdy));
						_pinchStartScale = PreferencesCache::TouchButtons[_focusedSlot].Scale;
						_resizeCenterX = r.CenterX;
						_resizeCenterY = r.CenterY;
						_resizingViaCorner = true;
						_editMode = EditMode::Resizing;
						_tapDownTime = -1.0f;
						break;
					}
				}

				// Touch button hit test - highest priority, including top bar area
				for (std::int32_t i = (std::int32_t)TouchButtonSlot::Count - 1; i >= 0; i--) {
					ButtonRect r = GetButtonRect((TouchButtonSlot)i, viewSize);
					if (px >= r.CenterX - r.HalfW && px <= r.CenterX + r.HalfW &&
						py >= r.CenterY - r.HalfH && py <= r.CenterY + r.HalfH) {

						_focusedSlot = i;
						Vector2f ch = GetCornerHandlePos((TouchButtonSlot)i, viewSize);
						_cornerHandleX = ch.X;
						_cornerHandleY = ch.Y;

						_primaryPointerId = event.actionIndex;
						_primaryStartX = px;
						_primaryStartY = py;
						_slotEdgeOffsetAtStart = PreferencesCache::TouchButtons[i].EdgeOffset;
						_slotAnchorAtStart = PreferencesCache::TouchButtons[i].Anchor;
						_resizingViaCorner = false;
						_editMode = EditMode::Dragging;
						_tapDownTime = 0.0f;
						_tapDownX = px;
						_tapDownY = py;
						break;
					}
				}
				if (_primaryPointerId != -1) break;	// consumed by a button

				// Fixed UI - only if no touch button was hit
				// Back: tap title row (center portion, top of bar)
				if (py < 26.0f && std::fabs(px - (float)viewSize.X * 0.5f) < (float)viewSize.X * 0.25f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
					return;
				}

				// Reset button
				if (py >= BtnY && py <= BtnY + BtnH && px >= resetLeft && px <= resetLeft + 60.0f) {
					PreferencesCache::ResetTouchButtons();
					_isDirty = true;
					_focusedSlot = -1;
					_root->PlaySfx("MenuSelect"_s, 0.6f);
					break;
				}

				// Joystick toggle
				if (py >= BtnY && py <= BtnY + BtnH && px >= joyLeft && px <= joyLeft + 96.0f) {
					PreferencesCache::EnableTouchJoystick = !PreferencesCache::EnableTouchJoystick;
					_isDirty = true;
					_root->PlaySfx("MenuSelect"_s, 0.6f);
					break;
				}
				break;
			}

			case TouchEventType::Move: {
				if (_primaryPointerId == -1) break;
				std::int32_t pi = event.findPointerIndex(_primaryPointerId);
				if (pi == -1) break;

				float px = event.pointers[pi].x * (float)viewSize.X;
				float py = event.pointers[pi].y * (float)viewSize.Y;

				if (_editMode == EditMode::Resizing && _secondaryPointerId != -1) {
					// Pinch resize
					std::int32_t si = event.findPointerIndex(_secondaryPointerId);
					if (si != -1) {
						float dx = (event.pointers[pi].x - event.pointers[si].x) * (float)viewSize.X;
						float dy = (event.pointers[pi].y - event.pointers[si].y) * (float)viewSize.Y;
						float newDist = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
						PreferencesCache::TouchButtons[_focusedSlot].Scale =
							std::clamp(_pinchStartScale * (newDist / _pinchStartDist), MinScale, MaxScale);
						_isDirty = true;
					}
				} else if (_editMode == EditMode::Resizing && _resizingViaCorner) {
					// Corner drag resize — use the center saved at gesture start to avoid feedback loop
					float dx = px - _resizeCenterX;
					float dy = py - _resizeCenterY;
					float newDist = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
					PreferencesCache::TouchButtons[_focusedSlot].Scale =
						std::clamp(_pinchStartScale * (newDist / _pinchStartDist), MinScale, MaxScale);
					_isDirty = true;
				} else if (_editMode == EditMode::Dragging && _focusedSlot >= 0) {
					auto& layout = PreferencesCache::TouchButtons[_focusedSlot];
					float halfSize = GetDefaultHalfSize((TouchButtonSlot)_focusedSlot) * layout.Scale;

					// Recompute start center from saved anchor + edge offset
					float startCx, startCy;
					switch (_slotAnchorAtStart) {
						case TouchButtonAnchor::BottomLeft:
							startCx = _slotEdgeOffsetAtStart.X + halfSize;
							startCy = (float)viewSize.Y - _slotEdgeOffsetAtStart.Y - halfSize;
							break;
						case TouchButtonAnchor::BottomRight:
							startCx = (float)viewSize.X - _slotEdgeOffsetAtStart.X - halfSize;
							startCy = (float)viewSize.Y - _slotEdgeOffsetAtStart.Y - halfSize;
							break;
						case TouchButtonAnchor::TopLeft:
							startCx = _slotEdgeOffsetAtStart.X + halfSize;
							startCy = _slotEdgeOffsetAtStart.Y + halfSize;
							break;
						default:
							startCx = (float)viewSize.X - _slotEdgeOffsetAtStart.X - halfSize;
							startCy = _slotEdgeOffsetAtStart.Y + halfSize;
							break;
					}

					float newCx = startCx + (px - _primaryStartX);
					float newCy = startCy + (py - _primaryStartY);

					ReAnchor(layout, newCx, newCy, halfSize, halfSize, viewSize);
					ClampEdgeOffset(layout, viewSize);
					_isDirty = true;

					Vector2f ch = GetCornerHandlePos((TouchButtonSlot)_focusedSlot, viewSize);
					_cornerHandleX = ch.X;
					_cornerHandleY = ch.Y;

					// Invalidate tap if moved too much
					if (_tapDownTime >= 0.0f) {
						float tdx = px - _tapDownX;
						float tdy = py - _tapDownY;
						if (tdx * tdx + tdy * tdy > 400.0f) {
							_tapDownTime = -1.0f;
						}
					}
				}
				break;
			}

			case TouchEventType::Up: {
				if (_tapDownTime >= 0.0f && _tapDownTime < 30.0f && _focusedSlot >= 0) {
					_bounceAnim[_focusedSlot] = 1.0f;
					_root->PlaySfx("MenuSelect"_s, 0.35f);
				}
				_primaryPointerId = -1;
				_secondaryPointerId = -1;
				_editMode = EditMode::None;
				_resizingViaCorner = false;
				_tapDownTime = -1.0f;
				break;
			}

			case TouchEventType::PointerUp: {
				if (event.actionIndex == _secondaryPointerId) {
					_secondaryPointerId = -1;
					if (_editMode == EditMode::Resizing && _primaryPointerId != -1) {
						_editMode = EditMode::Dragging;
					}
				} else if (event.actionIndex == _primaryPointerId) {
					if (_tapDownTime >= 0.0f && _tapDownTime < 30.0f && _focusedSlot >= 0) {
						_bounceAnim[_focusedSlot] = 1.0f;
						_root->PlaySfx("MenuSelect"_s, 0.35f);
					}
					_primaryPointerId = -1;
					_resizingViaCorner = false;
					_editMode = EditMode::None;
					_tapDownTime = -1.0f;
				}
				break;
			}
		}
	}
}
