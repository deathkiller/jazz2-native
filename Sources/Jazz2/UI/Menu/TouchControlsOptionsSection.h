#pragma once

#include "MenuSection.h"
#include "../../PreferencesCache.h"

namespace Jazz2::UI::Menu
{
	class TouchControlsOptionsSection : public MenuSection
	{
	public:
		TouchControlsOptionsSection();
		~TouchControlsOptionsSection();

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
		enum class EditMode {
			None,
			Dragging,
			Resizing
		};

		/** @brief Axis-aligned box for a button preview */
		struct ButtonRect {
			float CenterX, CenterY, HalfW, HalfH;
		};

		static constexpr float MinScale = 0.3f;
		static constexpr float MaxScale = 3.0f;

		// Focused slot state
		std::int32_t _focusedSlot;
		EditMode _editMode;
		float _pulseTime;

		// Primary touch (drag or resize-corner)
		std::int32_t _primaryPointerId;
		float _primaryStartX, _primaryStartY;      // normalized touch pos at gesture start
		Vector2f _slotEdgeOffsetAtStart;
		Jazz2::TouchButtonAnchor _slotAnchorAtStart;
		float _resizeCenterX, _resizeCenterY;       // button center saved at corner-resize start

		// Secondary touch for pinch-to-resize
		std::int32_t _secondaryPointerId;
		float _pinchStartDist;
		float _pinchStartScale;
		bool _resizingViaCorner;
		float _cornerHandleX, _cornerHandleY;       // screen-space corner handle center

		// Bounce animation per slot (0 = idle, decays to 0 when bouncing)
		float _bounceAnim[(std::int32_t)Jazz2::TouchButtonSlot::Count];
		float _tapDownTime;
		float _tapDownX, _tapDownY;

		bool _isDirty;

		// Helper: compute button screen rect for a slot
		ButtonRect GetButtonRect(Jazz2::TouchButtonSlot slot, Vector2i viewSize) const;
		// Helper: default half-size (before scale) in reference pixels
		static float GetDefaultHalfSize(Jazz2::TouchButtonSlot slot);
		// Helper: clamp EdgeOffset to reasonable bounds
		static void ClampEdgeOffset(Jazz2::TouchButtonLayout& layout, Vector2i viewSize);
		// Helper: re-anchor button based on center screen position (docking)
		static void ReAnchor(Jazz2::TouchButtonLayout& layout, float centerX, float centerY, float halfW, float halfH, Vector2i viewSize);
		// Helper: get corner handle position (away from anchor)
		Vector2f GetCornerHandlePos(Jazz2::TouchButtonSlot slot, Vector2i viewSize);

		void DrawButtonPreview(Canvas* canvas, Jazz2::TouchButtonSlot slot, Vector2i viewSize, bool isFocused) const;
		void DrawOutlineRect(float cx, float cy, float hw, float hh, std::uint16_t z, float thickness, Colorf color);		void DrawToggle(float left, float top, float w, float h, bool on);	};
}