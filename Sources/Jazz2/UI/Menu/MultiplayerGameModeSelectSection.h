#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "WidgetSection.h"
#include "../../Multiplayer/MpGameMode.h"

#include <Containers/SmallVector.h>

namespace Jazz2::UI::Menu
{
	/**
		@brief Multiplayer game mode selection menu section

		Lists the available multiplayer game modes (such as Battle, Race, Treasure Hunt, and Cooperation) and lets the
		player choose one for the server being configured. If the view is tall enough, a brief description of the
		selected mode is shown below the list. Built declaratively on top of @ref WidgetSection.
	*/
	class MultiplayerGameModeSelectSection : public WidgetSection
	{
	public:
		Recti GetClipRectangle(const Recti& contentBounds) override;
		void OnShow(IMenuContainer* root) override;
		void OnDraw(Canvas* canvas) override;

	private:
		static constexpr std::int32_t TopLine = 31;
		static constexpr std::int32_t BottomLine = 42;
		static constexpr float DescriptionHeight = 30.0f;
		static constexpr std::int32_t DescriptionMinViewHeight = 320;
		static constexpr float DescriptionScale = 0.7f;

		SmallVector<Jazz2::Multiplayer::MpGameMode, 0> _modes;
		String _wrappedDescription;
		std::int32_t _wrappedIndex = -1;
		float _wrappedWidth = 0.0f;

		bool ShowsDescription() const;
		static StringView GetDescription(Jazz2::Multiplayer::MpGameMode mode);
		String WrapText(StringView text, float scale, float maxWidth);
	};
}

#endif
