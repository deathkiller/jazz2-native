#pragma once

#if defined(WITH_MULTIPLAYER)

#include "MenuSection.h"
#include "../../Multiplayer/ServerDiscovery.h"

namespace Jazz2::UI::Menu
{
	class ServerSelectSection : public MenuSection, public Multiplayer::IServerObserver
	{
	public:
		ServerSelectSection();
		~ServerSelectSection();

		Recti GetClipRectangle(const Vector2i& viewSize) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const TouchEvent& event, const Vector2i& viewSize) override;

		void OnServerFound(Multiplayer::ServerDesc&& desc) override;

	private:
		struct ItemData {
			Multiplayer::ServerDesc Desc;
			float Y;

			ItemData(Multiplayer::ServerDesc&& desc);
		};

		static constexpr float ItemHeight = 20.0f;
		static constexpr float TopLine = 131.0f;
		static constexpr float BottomLine = 42.0f;

		SmallVector<ItemData> _items;
		int32_t _selectedIndex;
		float _animation;
		float _y;
		float _height;
		Vector2f _touchStart;
		Vector2f _touchLast;
		float _touchTime;
		int32_t _pressedCount;
		float _noiseCooldown;
		Multiplayer::ServerDiscovery _discovery;

		void ExecuteSelected();
		void EnsureVisibleSelected();
	};
}

#endif