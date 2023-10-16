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
			PlayEpisodes,
#if defined(SHAREWARE_DEMO_ONLY) && defined(DEATH_TARGET_EMSCRIPTEN)
			Import,
#else
			PlayCustomLevels,
#endif

#if defined(WITH_MULTIPLAYER)
			// TODO: Multiplayer
			TODO_ConnectTo,
			TODO_CreateServer,
#endif

			Options,
			About,
#if !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
			Quit,
#endif
			Count
		};

		struct ItemData {
			String Name;
			float Y;
		};

		static constexpr float DisabledItem = -1024.0f;

		ItemData _items[(int32_t)Item::Count];
		int32_t _selectedIndex;
		float _animation;
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		String _sourcePath;
#endif

		void ExecuteSelected();
	};
}