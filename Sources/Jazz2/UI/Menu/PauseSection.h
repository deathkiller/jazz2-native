#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	/** @brief In-game menu root section */
	class PauseSection : public MenuSection
	{
	public:
		PauseSection();

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
		enum class Item {
			Resume,
#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)
			Spectate,
#endif
			Options,
			Exit,

			Count
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ItemData {
			Item Type;
			String Name;
			float TouchY;

			ItemData(Item type, StringView name)
				: Type(type), Name(name), TouchY(0.0f) {}
		};
#endif

		SmallVector<ItemData, (std::int32_t)Item::Count> _items;
		std::int32_t _selectedIndex;
		float _animation;

		void ExecuteSelected();
	};
}