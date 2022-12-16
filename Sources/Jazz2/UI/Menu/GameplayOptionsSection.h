#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class GameplayOptionsSection : public MenuSection
	{
	public:
		GameplayOptionsSection();
		~GameplayOptionsSection();

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		enum class Item {
			Enhancements,
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			EnableDiscordIntegration,
#endif
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_WINDOWS_RT)
			EnableRgbLights,
#endif
#if defined(WITH_ANGELSCRIPT)
			AllowUnsignedScripts,
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
			RefreshCache,
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
		bool _isDirty;

		void ExecuteSelected();
	};
}