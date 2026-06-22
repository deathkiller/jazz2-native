#pragma once

#include "MenuSection.h"

#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
#	include "../../../nCine/Threading/Thread.h"
#endif

namespace Jazz2::UI::Menu
{
	/**
		@brief Custom level selection menu section
		
		Lists the installed custom (standalone) levels and lets the player pick one to play or host as a multiplayer
		server.
	*/
	class CustomLevelSelectSection : public MenuSection
	{
	public:
		/**
		 * @brief Creates a new instance
		 *
		 * @param multiplayer    Whether the selected level should be played as a multiplayer game (online or local)
		 * @param privateServer  Whether the hosted online server should be private (not publicly listed)
		 * @param localGame      Whether the selected level should start as a local splitscreen multiplayer match
		 *                       instead of hosting an online server
		 */
		CustomLevelSelectSection(bool multiplayer = false, bool privateServer = false, bool localGame = false);
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
		bool _localGame;
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