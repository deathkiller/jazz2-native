#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	enum class GameplayOptionsItemType {
		Enhancements,
		Language,
#if defined(WITH_ANGELSCRIPT)
		AllowUnsignedScripts,
#endif
		ContinuousJump,
		SwitchToNewWeapon,
		AllowCheats,
		OverwriteEpisodeEnd,
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_WINDOWS_RT)
		EnableRgbLights,
#endif
#if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_UNIX)
		BrowseSourceDirectory,
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		RefreshCache,
#endif
	};

	struct GameplayOptionsItem {
		GameplayOptionsItemType Type;
		StringView DisplayName;
		bool HasBooleanValue;
		bool IsReadOnly;
	};
#endif

	/**
		@brief Gameplay options menu section
		
		Lists the gameplay-related settings, such as enhancements, language, continuous jump, and cheats, and leads
		into the related sub-screens.
	*/
	class GameplayOptionsSection : public ScrollableMenuSection<GameplayOptionsItem>
	{
	public:
		/** @brief Creates a new instance */
		GameplayOptionsSection();
		~GameplayOptionsSection();

		void OnShow(IMenuContainer* root) override;
		void OnDraw(Canvas* canvas) override;

	protected:
		void OnHandleInput() override;
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;

	private:
		bool _isDirty;
	};
}