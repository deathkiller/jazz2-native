#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "UiMenu.h"

namespace Jazz2::Editor
{
	class UiSurface;

	/**
		@brief Menu of items in a ring around a point, as the weapon wheel of the game

		It's made for a finger held on the screen or a gamepad: an item is picked by pointing in its direction, the
		farther from the middle the surer, or by a tap on it. The middle of the ring closes it without picking anything.
		It opens growing out of its middle.
	*/
	class UiRadialMenu : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a menu of entries around a point in pixels of the screen */
		UiRadialMenu(SmallVectorImpl<UiMenuEntry>&& entries, Vector2f center);

		/** @brief Shows a menu around a point, @p onClosed is called when it's closed, picked from or not */
		static UiRadialMenu* Show(UiSurface& surface, Vector2f center, SmallVectorImpl<UiMenuEntry>&& entries, Function<void()>&& onClosed = {});

		/** @brief Picks the item in a direction from the middle, a short one picks none */
		void PointTo(Vector2f direction);
		/** @brief Runs the item picked and closes the menu, or only closes it if none is picked */
		void Confirm();

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Arrange(const UiContext& context, const Rectf& bounds) override;
		void Draw(UiPainter& painter) override;
		void Update(float timeMult) override;
		UiElement* HitTest(Vector2f pos) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;

	private:
		SmallVector<UiMenuEntry, 0> _entries;
		Vector2f _center;
		std::int32_t _selected;
		float _appear;
		float _radius;
		float _itemRadius;

		Vector2f GetItemCenter(std::int32_t index) const;
		std::int32_t GetItemAt(Vector2f pos);
	};
}

#endif
