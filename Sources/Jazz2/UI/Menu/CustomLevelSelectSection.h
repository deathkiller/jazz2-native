#pragma once

#include "MenuSection.h"

#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
#	include "../../../nCine/Threading/Thread.h"
#endif

namespace Jazz2::UI::Menu
{
	class CustomLevelSelectSection : public MenuSection
	{
	public:
		CustomLevelSelectSection(bool multiplayer = false);
		~CustomLevelSelectSection();

		Recti GetClipRectangle(const Vector2i& viewSize) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const TouchEvent& event, const Vector2i& viewSize) override;

	private:
		struct ItemData {
			String LevelName;
			String DisplayName;
			float Y;
		};

		static constexpr float ItemHeight = 20.0f;
		static constexpr float TopLine = 131.0f;
		static constexpr float BottomLine = 42.0f;

		SmallVector<ItemData> _items;
		bool _multiplayer;
		int32_t _selectedIndex;
		float _animation;
		float _y;
		float _height;
		Vector2f _touchStart;
		Vector2f _touchLast;
		float _touchTime;
		int32_t _pressedCount;
		float _noiseCooldown;
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		Thread _indexingThread;
#endif

		void ExecuteSelected();
		void EnsureVisibleSelected();
		void AddCustomLevels();
		void AddLevel(const StringView& levelFile);
	};
}