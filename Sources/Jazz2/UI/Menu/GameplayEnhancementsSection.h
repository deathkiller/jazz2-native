#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
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
#endif

	/**
		@brief Gameplay enhancements menu section
		
		Lets the player toggle the individual "Reforged" gameplay enhancements, such as the Reforged gameplay, HUD,
		main menu, ledge climbing, and weapon wheel.
	*/
	class GameplayEnhancementsSection : public ScrollableMenuSection<GameplayEnhancementsItem>
	{
	public:
		/** @brief Creates a new instance */
		GameplayEnhancementsSection();
		~GameplayEnhancementsSection();

		Recti GetClipRectangle(const Recti& contentBounds) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;

	protected:
		void OnHandleInput() override;
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;

	private:
		float _transition;
		bool _isDirty;
		bool _isInGame;

		bool IsItemReadOnly(GameplayEnhancementsItemType type);
	};
}