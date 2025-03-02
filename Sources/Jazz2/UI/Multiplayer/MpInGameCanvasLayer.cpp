#include "MpInGameCanvasLayer.h"

#if defined(WITH_MULTIPLAYER)

#include "../../LevelHandler.h"
#include "../../Actors/Multiplayer/RemotePlayerOnServer.h"

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

		std::int32_t charOffset = 0;

		if (_levelHandler->_isServer) {
			for (auto& [peer, peerDesc] : _levelHandler->_peerDesc) {
				if (peerDesc.Player != nullptr) {
					auto pos = peerDesc.Player->GetPos();
					_smallFont->DrawString(this, peerDesc.PlayerName, charOffset, pos.X - 4.0f, pos.Y - 42.0f, MainLayer, Alignment::Center,
						Font::DefaultColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f);
				}
			}
		} else {
			for (auto& [actorId, playerName] : _levelHandler->_playerNames) {
				auto it = _levelHandler->_remoteActors.find(actorId);
				if (it != _levelHandler->_remoteActors.end()) {
					auto pos = it->second->GetPos();
					_smallFont->DrawString(this, playerName, charOffset, pos.X - 4.0f, pos.Y - 42.0f, MainLayer, Alignment::Center,
						Font::DefaultColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f);
				}
			}
		}

		return true;
	}
}

#endif