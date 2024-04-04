#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
	enum class GameplayEnhancementsItemType {
		ReforgedGameplay,
		ReforgedHUD,
		ReforgedMainMenu,
		LedgeClimb,
		WeaponWheel
	};

	struct GameplayEnhancementsItem {
		GameplayEnhancementsItemType Type;
		String DisplayName;
	};

	class GameplayEnhancementsSection : public ScrollableMenuSection<GameplayEnhancementsItem>
	{
	public:
		GameplayEnhancementsSection();
		~GameplayEnhancementsSection();

		Recti GetClipRectangle(const Recti& contentBounds) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;

	private:
		float _transition;
		bool _isDirty;
		bool _isInGame;

		void OnHandleInput() override;
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;
	};
}