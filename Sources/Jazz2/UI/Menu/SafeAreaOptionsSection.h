#pragma once

#include "WidgetSection.h"
#include "../../PreferencesCache.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Safe area options menu section

		Interactive editor for @relativeref{Jazz2,PreferencesCache::SafeArea}, the margin the interface keeps from
		each edge of a display that does not show the whole picture. Alongside the four rows that set the edges it
		draws what they mean on the very screen they are being set for: the part of the picture that is being given
		up, and the rectangle that is left of it. Each edge can also be dragged directly with a finger.

		While the menu is being driven by touch the on-screen controls are drawn too, together with a second
		rectangle for what the HUD is left with once they have had their share. They are not moved by the safe
		area - the player places them wherever they can be reached (see @ref TouchControlsOptionsSection) - but
		the HUD keeps clear of them, so that inner rectangle is the margin measured from the buttons rather than
		from the edge of the screen.
	*/
	class SafeAreaOptionsSection : public WidgetSection
	{
	public:
		~SafeAreaOptionsSection() override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		// An axis-aligned rectangle held as its four edges, which is what everything here is expressed in
		struct Edges {
			float Left, Top, Right, Bottom;
		};
#endif

		// Half a percent of the view per keypress: a whole percent is too coarse to line a rectangle up with the
		// edge of a television, and a tenth (what the value is stored in, so that dragging stays smooth) would
		// take hundreds of presses to cross the range
		static constexpr std::int32_t StepSize = 5;
		// How close to an edge a finger has to land to take hold of it
		static constexpr float GrabDistance = 14.0f;
		// Auto-repeat while Left/Right is held. The wait before the first repeat is the one the menu's own
		// list navigation uses (see MenuSection::UpdateNavigation), so holding a direction feels the same
		// everywhere; the repeats themselves are faster, and faster again once held a while, because a margin
		// is walked across a range rather than stepped through a handful of rows
		static constexpr float AdjustInitialDelay = 26.0f;
		static constexpr float AdjustRepeatInterval = 5.0f;
		static constexpr float AdjustFastInterval = 2.0f;
		static constexpr float AdjustFastAfter = 60.0f;

		bool _isDirty = false;
		float _pulseTime = 0.0f;
		ScrollView* _list = nullptr;
		std::int32_t _draggedEdge = -1;
		std::int32_t _draggedPointerId = -1;
		float _adjustRepeat = 0.0f;		// Frames until the next auto-repeat fires while a direction is held
		float _adjustHeld = 0.0f;		// How long that direction has been held, which is what speeds it up
		// Backing storage for the formatted values, so the choice rows can hand out a stable view
		String _edgeValues[(std::int32_t)Jazz2::SafeAreaEdge::Count];

		// The safe rectangle itself, and the rectangle the HUD is left with once the touch controls have had
		// their share of it (the same one HUD::OnDraw() arrives at)
		Edges GetSafeEdges(Vector2i viewSize) const;
		Edges GetHudEdges(Vector2i viewSize) const;
		// The edge the selected row belongs to, or the one being dragged; -1 while neither
		std::int32_t GetFocusedEdge() const;

		void AddEdgeRow(ScrollView* list, Jazz2::SafeAreaEdge edge, StringView label);
		StringView FormatEdge(Jazz2::SafeAreaEdge edge);
		void ChangeEdge(Jazz2::SafeAreaEdge edge, std::int32_t direction);
		void UpdateHeldAdjustment(float timeMult);

		void DrawCroppedRegion(const Edges& safe, Vector2i viewSize);
		void DrawSafeOutline(const Edges& safe, std::int32_t focusedEdge, float pulseAlpha);
		void DrawEdgeLabels(const Edges& safe, std::int32_t focusedEdge, std::int32_t& charOffset);
		void DrawOutline(const Edges& r, std::uint16_t z, float thickness, const Colorf& color);
#if defined(NCINE_HAS_TOUCH_CONTROLS)
		void DrawTouchButtonGhosts(Vector2i viewSize);
#endif
	};
}
