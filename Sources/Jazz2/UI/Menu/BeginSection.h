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
		void OnDrawOverlay(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
		enum class Item {
#if !defined(SHAREWARE_DEMO_ONLY)
			Continue,
#endif
			PlayEpisodes,
#if defined(SHAREWARE_DEMO_ONLY) && defined(DEATH_TARGET_EMSCRIPTEN)
			Import,
#else
			PlayCustomLevels,
#endif
			Highscores,
			Options,
			About,
			Quit,
			Count
		};

		struct ItemData {
			Item Type;
			String Name;
			float Y;

			ItemData(Item type, StringView name)
				: Type(type), Name(name), Y(0.0f) {}
		};

		SmallVector<ItemData, (std::int32_t)Item::Count> _items;
		std::int32_t _selectedIndex;
		float _animation;
		float _transitionTime;
		bool _isPlayable;
		bool _skipSecondItem;
		bool _shouldStart;
		bool _alreadyStarted;
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		String _sourcePath;
#endif

		void ExecuteSelected();
		void OnAfterTransition();
	};
}