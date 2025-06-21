#include "MpInGameCanvasLayer.h"

#if defined(WITH_MULTIPLAYER)

#include "../../ContentResolver.h"
#include "../../LevelHandler.h"
#include "../../Actors/Multiplayer/MpPlayer.h"

#include "../../../nCine/Graphics/RenderQueue.h"

using namespace Jazz2::Multiplayer;

namespace Jazz2::UI::Multiplayer
{
	MpInGameCanvasLayer::MpInGameCanvasLayer(MpLevelHandler* levelHandler)
		: _levelHandler(levelHandler)
	{
		auto& resolver = ContentResolver::Get();

		_smallFont = resolver.GetFont(FontType::Small);
	}

	bool MpInGameCanvasLayer::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		if (_levelHandler->_isServer) {
			for (auto& [peer, peerDesc] : *_levelHandler->_networkManager->GetPeers()) {
				if (peerDesc->RemotePeer && peerDesc->Player && peerDesc->Player->_renderer.isEnabled()) {
					auto pos = peerDesc->Player->GetPos();
					DrawStringShadow(peerDesc->PlayerName, pos.X, pos.Y - 42.0f);
				}
			}
		} else {
			for (auto& [actorId, playerName] : _levelHandler->_playerNames) {
				auto it = _levelHandler->_remoteActors.find(actorId);
				if (it != _levelHandler->_remoteActors.end() && it->second->_renderer.isEnabled()) {
					auto pos = it->second->GetPos();
					DrawStringShadow(playerName, pos.X, pos.Y - 42.0f);
				}
			}
		}

		return true;
	}

	void MpInGameCanvasLayer::DrawStringShadow(StringView text, float x, float y)
	{
		x = std::round(x);
		y = std::round(y);

		float textScale = 0.7f;
		if (text == "\xF0\x9D\x94\x87\xF0\x9D\x94\xA2\xF0\x9D\x94\x9E\xF0\x9D\x94\xB1\xF0\x9D\x94\xA5"_s) {
			textScale = 0.8f;
		}

		std::int32_t charOffsetShadow = 0;
		_smallFont->DrawString(this, text, charOffsetShadow, x, y + 1.0f, FontShadowLayer, Alignment::Center,
			Colorf(0.0f, 0.0f, 0.0f, 0.36f), textScale, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f);

		std::int32_t charOffset = 0;
		_smallFont->DrawString(this, text, charOffset, x, y, FontLayer, Alignment::Center,
			Font::DefaultColor, textScale, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f);
	}
}

#endif