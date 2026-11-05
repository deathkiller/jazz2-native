#pragma once

#if defined(WITH_UI_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Menu.h"

namespace Jazz2::UI::Windows
{
	class Surface;

	/**
		@brief Menu of items in a ring around a point, as the weapon wheel of the game

		It's made for a finger held on the screen or a gamepad: an item is picked by pointing in its direction, the
		farther from the middle the surer, or by a tap on it. The middle of the ring closes it without picking anything.
		It opens growing out of its middle.
	*/
	class RadialMenu : public Element
	{
		DEATH_RUNTIME_OBJECT(Element);

	public:
		/** @brief Creates a menu of entries around a point in pixels of the screen */
		RadialMenu(SmallVectorImpl<MenuEntry>&& entries, Vector2f center);

		/** @brief Shows a menu around a point, @p onClosed is called when it's closed, picked from or not */
		static RadialMenu* Show(Surface& surface, Vector2f center, SmallVectorImpl<MenuEntry>&& entries, Function<void()>&& onClosed = {});

		/** @brief Picks the item in a direction from the middle, a short one picks none */
		void PointTo(Vector2f direction);
		/** @brief Runs the item picked and closes the menu, or only closes it if none is picked */
		void Confirm();

		Vector2f Measure(const Context& context, Vector2f available) override;
		void Arrange(const Context& context, const Rectf& bounds) override;
		void Draw(Painter& painter) override;
		void Update(float timeMult) override;
		Element* HitTest(Vector2f pos) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const PointerEvent& event) override;
		void OnPointerMove(const PointerEvent& event) override;
		void OnPointerUp(const PointerEvent& event, bool inside) override;

	private:
		SmallVector<MenuEntry, 0> _entries;
		Vector2f _center;
		std::int32_t _selected;
		float _appear;
		float _radius;
		float _itemRadius;
		// Whether the press started in the middle, a finger sliding out then picks by direction at any distance
		bool _pressFromCenter = false;
		// Whether the press went out of the middle, letting go in the middle without that keeps the menu open
		bool _movedOut = false;

		Vector2f GetItemCenter(std::int32_t index) const;
		std::int32_t GetItemAt(Vector2f pos);
	};
}

#endif
