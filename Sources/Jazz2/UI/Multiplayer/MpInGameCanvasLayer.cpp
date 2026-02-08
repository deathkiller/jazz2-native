#include "MpInGameCanvasLayer.h"

#if defined(WITH_MULTIPLAYER)

#include "../../ContentResolver.h"
#include "../../LevelHandler.h"
#include "../../Actors/Multiplayer/MpPlayer.h"
#include "../../Actors/Multiplayer/RemotePlayerOnServer.h"

#include "../../../nCine/Graphics/RenderQueue.h"

using namespace Jazz2::Multiplayer;
using namespace Jazz2::Actors::Multiplayer;

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
					Colorf color = Font::DefaultColor;
					if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(peerDesc->Player)) {
						// If player is in menu or console, show their name in red
						// If player is idle for more than 60 seconds, show their name in blue
						if ((remotePlayerOnServer->Flags & (RemotePlayerOnServer::PlayerFlags::InMenu | RemotePlayerOnServer::PlayerFlags::InConsole)) != RemotePlayerOnServer::PlayerFlags::None) {
							color = Colorf(0.6f, 0.42f, 0.42f);
						} else if (peerDesc->IdleElapsedFrames * FrameTimer::SecondsPerFrame > 60.0f) {
							color = Colorf(0.42f, 0.48f, 0.54f);
						}
					}

					DrawStringShadow(peerDesc->PlayerName, pos.X, pos.Y - 42.0f, color);
				}
			}
		} else {
			for (auto& [actorId, playerName] : _levelHandler->_playerNames) {
				auto it = _levelHandler->_remoteActors.find(actorId);
				if (it != _levelHandler->_remoteActors.end() && it->second->_renderer.isEnabled()) {
					auto pos = it->second->GetPos();
					Colorf color = Font::DefaultColor;
					// If player is in menu or console, show their name in red
					if (playerName.Flags & 0x01) {
						color = Colorf(0.6f, 0.42f, 0.42f);
					}
					DrawStringShadow(playerName.Name, pos.X, pos.Y - 42.0f, color);
				}
			}
		}

		return true;
	}

	void MpInGameCanvasLayer::DrawStringShadow(StringView text, float x, float y, Colorf color)
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
			color, textScale, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f);
	}
}

#endif