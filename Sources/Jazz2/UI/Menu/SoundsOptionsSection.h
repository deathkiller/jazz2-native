#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class SoundsOptionsSection : public MenuSection
	{
	public:
		SoundsOptionsSection();
		~SoundsOptionsSection() override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		enum class Item {
			MasterVolume,
			SfxVolume,
			MusicVolume,

			Count
		};

		struct ItemData {
			String Name;
			float TouchY;
		};

		ItemData _items[(int32_t)Item::Count];
		int32_t _selectedIndex;
		float _animation;
		bool _isDirty;
		float _pressedCooldown;
		int32_t _pressedCount;
	};
}