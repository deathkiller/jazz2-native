#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Canvas.h"
#include "../Font.h"
#include "../../Multiplayer/MpLevelHandler.h"

namespace Jazz2::UI::Multiplayer
{
	/** @brief In-game canvas on sprite layer for multiplayer */
	class MpInGameCanvasLayer : public Canvas
	{
	public:
		MpInGameCanvasLayer(Jazz2::Multiplayer::MpLevelHandler* levelHandler);

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		static constexpr std::uint16_t FontLayer = 550;
		static constexpr std::uint16_t FontShadowLayer = 546;
		
		Jazz2::Multiplayer::MpLevelHandler* _levelHandler;
		Font* _smallFont;

		void DrawStringShadow(StringView text, float x, float y, Colorf color);
	};
}

#endif