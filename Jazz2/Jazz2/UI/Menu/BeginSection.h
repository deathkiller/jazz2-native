#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class BeginSection : public MenuSection
	{
	public:
		BeginSection();

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		enum class Item {
			PlayStory,
#if defined(SHAREWARE_DEMO_ONLY) && defined(DEATH_TARGET_EMSCRIPTEN)
			Import,
#endif
			Options,
			About,
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN)
			Quit,
#endif
			Count
		};

		struct ItemData {
			String Name;
			float TouchY;
		};

		ItemData _items[(int)Item::Count];
		int _selectedIndex;
		float _animation;
		bool _isVerified;

		void ExecuteSelected();
	};
}