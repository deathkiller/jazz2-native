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
		CustomLevelSelectSection(bool multiplayer = false, bool privateServer = false);
		~CustomLevelSelectSection();

		Recti GetClipRectangle(const Recti& contentBounds) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const TouchEvent& event, Vector2i viewSize) override;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ItemData {
			String LevelName;
			String DisplayName;
			float Y;
		};
#endif

		static constexpr std::int32_t ItemHeight = 20;
		static constexpr std::int32_t TopLine = 31;
		static constexpr std::int32_t BottomLine = 42;

		SmallVector<ItemData> _items;
		bool _multiplayer;
		bool _privateServer;
		std::int32_t _selectedIndex;
		float _animation;
		float _y;
		float _height;
		float _availableHeight;
		Vector2f _touchStart;
		Vector2f _touchLast;
		float _touchTime;
		float _touchSpeed;
		std::int32_t _pressedCount;
		float _noiseCooldown;
		std::int8_t _touchDirection;
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		Thread _indexingThread;
#endif

		void ExecuteSelected();
		void EnsureVisibleSelected(std::int32_t offset = 0);
		void AddCustomLevels();
		void AddLevel(StringView levelFile);
	};
}