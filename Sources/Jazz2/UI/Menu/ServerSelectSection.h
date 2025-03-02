#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MenuSection.h"
#include "../../Multiplayer/ServerDiscovery.h"

namespace Jazz2::UI::Menu
{
	class ServerSelectSection : public MenuSection, public Jazz2::Multiplayer::IServerObserver
	{
	public:
		ServerSelectSection();
		~ServerSelectSection();

		Recti GetClipRectangle(const Recti& contentBounds) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const TouchEvent& event, Vector2i viewSize) override;

		void OnServerFound(Jazz2::Multiplayer::ServerDescription&& desc) override;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ItemData {
			Jazz2::Multiplayer::ServerDescription Desc;
			float Y;

			ItemData(Jazz2::Multiplayer::ServerDescription&& desc);
		};
#endif

		static constexpr std::int32_t ItemHeight = 20;
		static constexpr std::int32_t TopLine = 31;
		static constexpr std::int32_t BottomLine = 42;

		SmallVector<ItemData> _items;
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
		Jazz2::Multiplayer::ServerDiscovery _discovery;
		std::int8_t _touchDirection;

		void ExecuteSelected();
		void EnsureVisibleSelected();
	};
}

#endif