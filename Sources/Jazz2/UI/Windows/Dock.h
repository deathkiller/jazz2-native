#pragma once

#if defined(WITH_UI_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Widgets.h"

namespace Jazz2::UI::Windows
{
	/** @brief Edge of the screen a panel is docked at */
	enum class DockSide : std::uint8_t
	{
		Left,			/**< Left edge */
		Right			/**< Right edge */
	};

	/**
		@brief Panel that is docked at an edge of the screen or floats over everything else

		The header of the panel is dragged to move it --- out of its place, which makes it float, around the screen
		and back to the edge, which docks it again. The button at the end of the header does the same in one click.
		A floating panel is resized by its edges. On its way between the two places the panel glides, while the room
		it takes when docked opens or closes, see @ref GetDockedAmount().

		The owner places the panel by @ref Arrange() with the bounds it has when docked, whether it's docked or not.
		Elements added to the frame after the panel (not into it) are drawn over the panel and placed by the owner.
	*/
	class DockFrame : public Element
	{
		DEATH_RUNTIME_OBJECT(Element);

	public:
		/** @brief Smallest width of a floating panel in units */
		static constexpr float MinFloatingWidth = 100.0f;
		/** @brief Smallest height of a floating panel in units */
		static constexpr float MinFloatingHeight = 80.0f;

		/** @brief Creates a new instance */
		DockFrame(DockSide side, StringView tooltip);

		/** @brief Returns the panel, its first child is the header */
		Panel* GetPanel() const {
			return _panel;
		}
		/** @brief Returns the stack in the header between the grip and the button, for the title or tabs */
		Stack* GetHeaderContent() const {
			return _headerContent;
		}

		/** @brief Returns `true` if the panel floats, or is going to once it got there */
		bool IsFloating() const {
			return _floating;
		}
		/** @brief Makes the panel float or docks it, it glides there unless @p animate is `false` */
		void SetFloating(bool floating, bool animate = true);
		/** @brief Returns how much of the room the panel takes when docked it takes now, from 0 to 1 */
		float GetDockedAmount() const;
		/** @brief Returns `true` if the panel is drawn over the other panels --- it floats, or it's on its way */
		bool IsLifted() const;
		/** @brief Returns the bounds the panel will have, as @ref Arrange() would place it */
		Rectf GetTargetBounds(const Context& context, const Rectf& dockBounds) const;

		/**
		 * @brief Makes the panel a drawer, for a small screen, or docks it again
		 *
		 * A drawer takes no room, it slides over the view from its edge when it's opened and out when it's closed. It
		 * doesn't float then, the header only closes it.
		 */
		void SetDrawerMode(bool drawer);
		/** @brief Returns `true` if the panel is a drawer */
		bool IsDrawer() const {
			return _drawer;
		}
		/** @brief Opens or closes the drawer, it slides */
		void SetDrawerOpen(bool open);
		/** @brief Returns `true` if the drawer is open, or opening */
		bool IsDrawerOpen() const {
			return _drawerOpen;
		}

		/** @brief Bounds of the floating panel in units, the width is 0 if it hasn't floated yet */
		Rectf FloatingBounds;
		/** @brief Title of the panel, shown on the taskbar while it's in a window of its own, see @ref Surface::PlaceDetachable() */
		String Title;
		/**
		 * @brief Whether the floating panel is in a window of its own outside of the main window
		 *
		 * Kept while the panel is hidden or the interface is built again, so it gets its window back where @ref FloatingBounds
		 * is, also after a restart if it's remembered with the bounds.
		 */
		bool Detached = false;
		/** @brief Called when the panel was docked, made float, moved or resized */
		Function<void()> OnChanged;
		/** @brief Called when the panel is pressed anywhere, it should come in front of the other floating panels */
		Function<void()> OnActivated;

		/** @brief Draws where the panel would be docked if it was let go now, under all floating panels */
		void DrawDockPreview(Painter& painter);

		StringView GetTitle() const override;
		void Arrange(const Context& context, const Rectf& bounds) override;
		void Draw(Painter& painter) override;
		void Update(float timeMult) override;
		Element* HitTest(Vector2f pos) override;
		/** @brief With the edges a floating panel is resized by, which reach a little outside of it */
		Rectf GetHitBounds() const override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const PointerEvent& event) override;
		void OnPointerDownInside(const PointerEvent& event) override;
		void OnPointerMove(const PointerEvent& event) override;
		void OnPointerUp(const PointerEvent& event, bool inside) override;

	private:
		DockSide _side;
		Panel* _panel;
		DragBar* _header;
		Stack* _headerContent;
		Button* _dockButton;
		bool _floating;
		bool _buttonShowsFloating;
		// How far the panel is from its docked place to its floating place, 0 to 1, animated
		float _floatAmount;
		// How much the place it would be docked at is highlighted, animated
		float _previewAmount;
		bool _moving;
		bool _undockPending;
		bool _overDockZone;
		Vector2f _moveStart;
		Rectf _moveStartBounds;
		// Where it floated before it was dragged to the edge, it floats there again the next time
		Rectf _boundsBeforeMove;
		bool _restoreBounds;
		std::uint8_t _resizeEdges;
		std::uint8_t _hoverEdges;
		Vector2f _resizeStart;
		Rectf _resizeStartBounds;
		Rectf _dockBounds;
		float _scale;
		bool _drawer = false;
		bool _drawerOpen = false;
		// How far the drawer is open, 0 to 1, animated
		float _drawerAmount = 0.0f;

		Rectf GetFloatingRect(float scale, const Rectf& dockBounds) const;
		Rectf GetDefaultFloatingBounds(float scale, const Rectf& dockBounds) const;
		std::uint8_t GetEdgesAt(Vector2f pos) const;
		// Whether it floats where it's resized by its edges, it's not on its way and not being moved
		bool IsResizable() const;
		bool IsInDockZone(Vector2f pos) const;
		void BeginMove(Vector2f pos);
		void Move(Vector2f offset, Vector2f pos);
		void EndMove(Vector2f pos);
		void NotifyChanged();
	};
}

#endif
