#include "MpHUD.h"

#if defined(WITH_MULTIPLAYER)

#include "../../Actors/Multiplayer/PlayerOnServer.h"
#include "../../Multiplayer/MpLevelHandler.h"
#include "../../ContentResolver.h"
#include "../../PreferencesCache.h"
#include "../../Tiles/TileSet.h"

#include "../../../nCine/I18n.h"
#include "../../../nCine/Graphics/RenderQueue.h"
#include "../../../nCine/Graphics/Texture.h"

using namespace Jazz2::Multiplayer;
using namespace Jazz2::Actors::Multiplayer;

namespace Jazz2::UI::Multiplayer
{
	namespace Resources
	{
		static constexpr AnimState WeaponBlasterJazz = (AnimState)0;
		static constexpr AnimState WeaponBlasterSpaz = (AnimState)1;
		static constexpr AnimState WeaponBlasterLori = (AnimState)2;
		static constexpr AnimState WeaponBouncer = (AnimState)3;
		static constexpr AnimState WeaponFreezer = (AnimState)4;
		static constexpr AnimState WeaponSeeker = (AnimState)5;
		static constexpr AnimState WeaponRF = (AnimState)6;
		static constexpr AnimState WeaponToaster = (AnimState)7;
		static constexpr AnimState WeaponTNT = (AnimState)8;
		static constexpr AnimState WeaponPepper = (AnimState)9;
		static constexpr AnimState WeaponElectro = (AnimState)10;
		static constexpr AnimState WeaponThunderbolt = (AnimState)11;
		static constexpr AnimState WeaponPowerUpBlasterJazz = (AnimState)20;
		static constexpr AnimState WeaponPowerUpBlasterSpaz = (AnimState)21;
		static constexpr AnimState WeaponPowerUpBlasterLori = (AnimState)22;
		static constexpr AnimState WeaponPowerUpBouncer = (AnimState)23;
		static constexpr AnimState WeaponPowerUpFreezer = (AnimState)24;
		static constexpr AnimState WeaponPowerUpSeeker = (AnimState)25;
		static constexpr AnimState WeaponPowerUpRF = (AnimState)26;
		static constexpr AnimState WeaponPowerUpToaster = (AnimState)27;
		static constexpr AnimState WeaponPowerUpTNT = (AnimState)28;
		static constexpr AnimState WeaponPowerUpPepper = (AnimState)29;
		static constexpr AnimState WeaponPowerUpElectro = (AnimState)30;
		static constexpr AnimState WeaponPowerUpThunderbolt = (AnimState)31;
		static constexpr AnimState WeaponToasterDisabled = (AnimState)47;
		static constexpr AnimState CharacterJazz = (AnimState)60;
		static constexpr AnimState CharacterSpaz = (AnimState)61;
		static constexpr AnimState CharacterLori = (AnimState)62;
		static constexpr AnimState CharacterFrog = (AnimState)63;
		static constexpr AnimState Heart = (AnimState)70;
		static constexpr AnimState PickupGemRed = (AnimState)71;
		static constexpr AnimState PickupGemGreen = (AnimState)72;
		static constexpr AnimState PickupGemBlue = (AnimState)73;
		static constexpr AnimState PickupGemPurple = (AnimState)74;
		static constexpr AnimState PickupCoin = (AnimState)75;
		static constexpr AnimState PickupFood = (AnimState)76;
		static constexpr AnimState PickupCarrot = (AnimState)77;
		static constexpr AnimState PickupStopwatch = (AnimState)78;
		static constexpr AnimState BossHealthBar = (AnimState)79;
		static constexpr AnimState WeaponWheel = (AnimState)80;
		static constexpr AnimState WeaponWheelInner = (AnimState)81;
		static constexpr AnimState WeaponWheelDim = (AnimState)82;
		static constexpr AnimState TouchDpad = (AnimState)100;
		static constexpr AnimState TouchFire = (AnimState)101;
		static constexpr AnimState TouchJump = (AnimState)102;
		static constexpr AnimState TouchRun = (AnimState)103;
		static constexpr AnimState TouchChange = (AnimState)104;
		static constexpr AnimState TouchPause = (AnimState)105;
	}

	using namespace Jazz2::UI::Multiplayer::Resources;

	MpHUD::MpHUD(Jazz2::Multiplayer::MpLevelHandler* levelHandler)
		: HUD(levelHandler), _countdownTimeLeft(0.0f), _minimapCacheKey(0), _minimapVertexCount(0), _minimapCommandsCount(0)
	{
		auto& resolver = ContentResolver::Get();
		_mediumFont = resolver.GetFont(FontType::Medium);

		_minimapVertices = std::make_unique<float[]>(MinimapMaxVertexFloats);
	}

	void MpHUD::OnUpdate(float timeMult)
	{
		HUD::OnUpdate(timeMult);

		if (_countdownTimeLeft > 0.0f) {
			_countdownTimeLeft -= timeMult;
		}
	}

	bool MpHUD::OnDraw(RenderQueue& renderQueue)
	{
		// Reset the per-frame minimap mesh pools before the base class triggers OnDrawScore (which draws the minimap)
		_minimapVertexCount = 0;
		_minimapCommandsCount = 0;

		if (!HUD::OnDraw(renderQueue)) {
			return false;
		}

		std::int32_t charOffset = 0;

		if (_countdownTimeLeft > 0.0f) {
			float textScale = 2.0f - std::min(_countdownTimeLeft / FrameTimer::FramesPerSecond, 1.0f);
			Colorf textColor = Font::DefaultColor;
			textColor.A = std::min(_countdownTimeLeft / FrameTimer::FramesPerSecond, 0.5f) * 2.0f;
			_mediumFont->DrawString(this, _countdownText, charOffset, ViewSize.X * 0.5f, ViewSize.Y * 0.5f, FontLayer + 20,
				Alignment::Center, textColor, textScale, 0.0f, 0.0f, 0.0f);
		}

		if (PreferencesCache::ShowPerformanceMetrics) {
			std::int32_t debugCharOffset = 0;
			auto* mpLevelHandler = static_cast<MpLevelHandler*>(_levelHandler);
			if (mpLevelHandler->_isServer) {
#if defined(DEATH_DEBUG)
				char debugBuffer[64];
				std::size_t length = formatInto(debugBuffer, "{} b |", mpLevelHandler->_debugAverageUpdatePacketSize);
				_smallFont->DrawString(this, { debugBuffer, length }, debugCharOffset, ViewSize.X - 44.0f, 1.0f,
					200, Alignment::TopRight, Font::DefaultColor, 0.8f);
#endif
			} else {
				auto rtt = mpLevelHandler->_networkManager->GetRoundTripTimeMs();
				if (rtt > 0) {
					char debugBuffer[64];
					std::size_t length = formatInto(debugBuffer, "{} ms |", rtt);
					_smallFont->DrawString(this, { debugBuffer, length }, debugCharOffset, ViewSize.X - 44.0f, 1.0f,
						200, Alignment::TopRight, Font::DefaultColor, 0.8f);
				}
			}
		}

		return true;
	}

	void MpHUD::ShowCountdown(std::int32_t secsLeft)
	{
		StringView sfxName;
		if (secsLeft > 0) {
			char buffer[16];
			u32tos(secsLeft, buffer);
			_countdownText = buffer;
			sfxName = "Countdown"_s;
		} else {
			_countdownText = "Go!"_s;
			sfxName = "CountdownEnd"_s;

		}
		_countdownTimeLeft = FrameTimer::FramesPerSecond;

#if defined(WITH_AUDIO)
		auto it = _metadata->Sounds.find(String::nullTerminatedView(sfxName));
		if (it != _metadata->Sounds.end() && !it->second.Buffers.empty()) {
			_levelHandler->PlaySfx(nullptr, sfxName, &it->second.Buffers[0]->Buffer, Vector3f::Zero, true, 1.0f, 1.0f);
		}
#endif
	}

	void MpHUD::OnDrawOverview(const Rectf& view, const Rectf& adjustedView, Actors::Player* player)
	{
		HUD::OnDrawOverview(view, adjustedView, player);

		char stringBuffer[32];
		std::int32_t charOffset = 0, charOffsetShadow = 0;

		auto* mpLevelHandler = static_cast<MpLevelHandler*>(_levelHandler);
		const auto& serverConfig = mpLevelHandler->_networkManager->GetServerConfiguration();
		auto* mpPlayer = static_cast<MpPlayer*>(player);
		auto peerDesc = mpPlayer->GetPeerDescriptor();

		if (mpLevelHandler->_levelState == MpLevelHandler::LevelState::PreGame) {
			float timeLeftSecs = mpLevelHandler->_gameTimeLeft / FrameTimer::FramesPerSecond;
			std::int32_t minutes = std::max(0, (std::int32_t)(timeLeftSecs / 60));
			std::int32_t seconds = std::max(0, (std::int32_t)fmod(timeLeftSecs, 60));
			std::int32_t milliseconds = std::max(0, (std::int32_t)(fmod(timeLeftSecs, 1) * 100));

			std::size_t length = formatInto(stringBuffer, "{}:{:.2}:{:.2}", minutes, seconds, milliseconds);
			auto gameStartsInText = _f("Game starts in {}", StringView { stringBuffer, length });
			_smallFont->DrawString(this, gameStartsInText, charOffsetShadow, view.X + 17.0f, view.Y + 20.0f + 1.0f, FontShadowLayer,
				Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			_smallFont->DrawString(this, gameStartsInText, charOffset, view.X + 17.0f, view.Y + 20.0f, FontLayer,
				Alignment::TopLeft, Font::DefaultColor, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		} else if (mpLevelHandler->_levelState == MpLevelHandler::LevelState::WaitingForMinPlayers) {
			auto waitingText = _fn("Waiting for {} more player", "Waiting for {} more players", mpLevelHandler->_waitingForPlayerCount, mpLevelHandler->_waitingForPlayerCount);
			_smallFont->DrawString(this, waitingText, charOffsetShadow, view.X + 17.0f, view.Y + 20.0f + 1.0f, FontShadowLayer,
				Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			_smallFont->DrawString(this, waitingText, charOffset, view.X + 17.0f, view.Y + 20.0f, FontLayer,
				Alignment::TopLeft, Font::DefaultColor, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		}
	}

	void MpHUD::OnDrawScore(const Rectf& view, Actors::Player* player)
	{
#if defined(WITH_ANGELSCRIPT)
		/*if (_levelHandler->_scripts != nullptr && _levelHandler->_scripts->OnDraw(this, player, view, Scripting::DrawType::Score)) {
			return;
		}*/
#endif

		std::int32_t charOffset = 0, charOffsetShadow = 0;

		auto* mpLevelHandler = static_cast<MpLevelHandler*>(_levelHandler);
		const auto& serverConfig = mpLevelHandler->_networkManager->GetServerConfiguration();

		if DEATH_UNLIKELY(mpLevelHandler->_levelState != MpLevelHandler::LevelState::Running) {
			return;
		}

		// Team score header (centered at the top) in team game modes
		if (IsTeamGameMode(serverConfig.GameMode)) {
			std::uint8_t teamCount = (std::uint8_t)mpLevelHandler->_teamScores.size();
			if (teamCount >= 2) {
				char teamBuffer[16];
				float spacing = 54.0f;
				float startX = view.X + view.W * 0.5f - spacing * (teamCount - 1) * 0.5f;
				for (std::uint8_t team = 0; team < teamCount; team++) {
					float x = startX + team * spacing;
					std::size_t length = formatInto(teamBuffer, "{}", mpLevelHandler->_teamScores[team]);
					_mediumFont->DrawString(this, { teamBuffer, length }, charOffsetShadow, x, view.Y + 5.0f + 2.0f, FontShadowLayer,
						Alignment::Top, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.9f, 0.0f, 0.0f, 0.0f, 0.0f);
					_mediumFont->DrawString(this, { teamBuffer, length }, charOffset, x, view.Y + 5.0f, FontLayer,
						Alignment::Top, GetTeamColor(team), 0.9f, 0.0f, 0.0f, 0.0f, 0.0f);
				}
			}
		}

		// Each game mode draws its own score HUD via IGameMode::OnDrawHUD (dispatched here); when no game mode is
		// active (MpGameMode::Unknown) there is nothing to draw.
		mpLevelHandler->DrawActiveGameModeHUD(*this, player, view);
	}

	void MpHUD::DrawHudText(GameModeFontType font, StringView text, float x, float y, float shadowOffsetY,
		Alignment alignment, const Colorf& color, float scale, float charSpacing,
		float angleOffset, float variance, float speed)
	{
		Font* selectedFont = (font == GameModeFontType::Medium ? _mediumFont : _smallFont);
		std::int32_t charOffsetShadow = 0, charOffset = 0;
		selectedFont->DrawString(this, text, charOffsetShadow, x, y + shadowOffsetY, FontShadowLayer, alignment,
			Colorf(0.0f, 0.0f, 0.0f, 0.32f), scale, angleOffset, variance, variance, speed, charSpacing, 1.0f);
		selectedFont->DrawString(this, text, charOffset, x, y, FontLayer, alignment, color,
			scale, angleOffset, variance, variance, speed, charSpacing, 1.0f);
	}

	void MpHUD::DrawHudIcon(GameModeHudIcon icon, float x, float y, float shadowOffsetY,
		Alignment alignment, const Colorf& color, float scaleX, float scaleY)
	{
		AnimState state;
		switch (icon) {
			default:
			case GameModeHudIcon::Food: state = Resources::PickupFood; break;
			case GameModeHudIcon::GemRed: state = Resources::PickupGemRed; break;
			case GameModeHudIcon::GemGreen: state = Resources::PickupGemGreen; break;
		}
		DrawElement(state, -1, x, y + shadowOffsetY, ShadowLayer, alignment, Colorf(0.0f, 0.0f, 0.0f, 0.4f), scaleX, scaleY);
		DrawElement(state, -1, x, y, MainLayer, alignment, color, scaleX, scaleY);
	}

	struct PositionInRoundItem
	{
		StringView PlayerName;
		std::uint32_t PositionInRound;
		std::uint32_t PointsInRound;
		bool IsLocal;
		std::uint8_t Team;

		PositionInRoundItem(StringView playerName, std::uint32_t position, std::uint32_t points, bool isLocal, std::uint8_t team)
			: PlayerName(playerName), PositionInRound(position), PointsInRound(points), IsLocal(isLocal), Team(team) {}
	};

	void MpHUD::DrawPositionInRound(const Rectf& view, Actors::Player* player)
	{
		auto* mpLevelHandler = static_cast<MpLevelHandler*>(_levelHandler);
		const auto& serverConfig = mpLevelHandler->_networkManager->GetServerConfiguration();

		if (mpLevelHandler->_console->IsVisible()) {
			return;
		}

		char stringBuffer[32];
		std::int32_t charOffset = 0;
		std::int32_t charOffsetShadow = 0;
		SmallVector<PositionInRoundItem, 8> positions;
		std::uint32_t localPoints = UINT32_MAX;

		if (mpLevelHandler->_isServer) {
			auto peers = mpLevelHandler->_networkManager->GetPeers();
			for (const auto& [peer, peerDesc] : *peers) {
				if ((peerDesc->PositionInRound >= 1 && peerDesc->PositionInRound <= 3) || !peerDesc->RemotePeer) {
					if (!peerDesc->RemotePeer) {
						localPoints = peerDesc->PointsInRound;
					}

					positions.emplace_back(peerDesc->PlayerName, peerDesc->PositionInRound, peerDesc->PointsInRound, !peerDesc->RemotePeer, peerDesc->Team);
				}
			}
		} else {
			for (const auto& pos : mpLevelHandler->_positionsInRound) {
				if ((pos.PositionInRound >= 1 && pos.PositionInRound <= 3) || pos.ActorID == mpLevelHandler->_lastSpawnedActorId) {
					StringView playerName;
					std::uint8_t team = 0;
					if (pos.ActorID == mpLevelHandler->_lastSpawnedActorId) {
						localPoints = pos.PointsInRound;
						auto localPeerDesc = mpLevelHandler->_networkManager->GetPeerDescriptor(LocalPeer);
						playerName = localPeerDesc->PlayerName;
						team = localPeerDesc->Team;
					} else {
						auto it = mpLevelHandler->_playerNames.find(pos.ActorID);
						if (it != mpLevelHandler->_playerNames.end()) {
							playerName = it->second.Name;
							team = it->second.Team;
						}
					}

					positions.emplace_back(playerName, pos.PositionInRound, pos.PointsInRound, pos.ActorID == mpLevelHandler->_lastSpawnedActorId, team);
				}
			}
		}

		nCine::sort(positions.begin(), positions.end(), [](const auto& x, const auto& y) {
			std::uint32_t xPos = (x.PositionInRound > 0 ? x.PositionInRound : UINT32_MAX);
			std::uint32_t yPos = (y.PositionInRound > 0 ? y.PositionInRound : UINT32_MAX);
			return xPos < yPos;
		});

		float offset = 36.0f;
		for (std::int32_t i = 0; i < (std::int32_t)positions.size(); i++) {
			const auto& item = positions[i];
			if (item.PositionInRound > 0) {
				std::size_t length = formatInto(stringBuffer, "{}.", item.PositionInRound);
				_smallFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + 30.0f, view.Y + offset + 1.0f, FontShadowLayer,
					Alignment::TopRight, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
				_smallFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + 30.0f, view.Y + offset, FontLayer,
					Alignment::TopRight, Colorf(0.4f, 0.4f, 0.4f, 1.0f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			} else {
				_smallFont->DrawString(this, "-."_s, charOffsetShadow, view.X + 30.0f, view.Y + offset + 1.0f, FontShadowLayer,
					Alignment::TopRight, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
				_smallFont->DrawString(this, "-."_s, charOffset, view.X + 30.0f, view.Y + offset, FontLayer,
					Alignment::TopRight, Colorf(0.4f, 0.4f, 0.4f, 1.0f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			}
			// In team modes the name is tinted with the player's team color (friend/foe cue); otherwise the local
			// player's name uses the usual highlight and everyone else the default color
			Colorf nameColor;
			if (IsTeamGameMode(serverConfig.GameMode)) {
				nameColor = GetTeamColor(item.Team);
				nameColor.SetAlpha(item.IsLocal ? 0.9f : 0.7f);
			} else {
				nameColor = (item.IsLocal ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : Font::DefaultColor);
			}
			_smallFont->DrawString(this, item.PlayerName, charOffsetShadow, view.X + 38.0f, view.Y + offset + 1.0f, FontShadowLayer,
				Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			_smallFont->DrawString(this, item.PlayerName, charOffset, view.X + 38.0f, view.Y + offset, FontLayer,
				Alignment::TopLeft, nameColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

			if (!item.IsLocal) {
				std::int64_t pointsDiff = (std::int64_t)item.PointsInRound - (std::int64_t)localPoints;
				if (serverConfig.GameMode == MpGameMode::Race || serverConfig.GameMode == MpGameMode::TeamRace) {
					pointsDiff = -pointsDiff / 16;
				}

				Vector2f playerNameSize = _smallFont->MeasureString(item.PlayerName, 0.8f, 0.9f);

				std::size_t length = 0;
				stringBuffer[length++] = (pointsDiff < 0 ? '-' : '+');
				if (std::abs(pointsDiff) > 30000) {
					length += copyStringFirst(&stringBuffer[length], sizeof(stringBuffer) - length, "\u221E");
				} else {
					length += formatInto({ &stringBuffer[length], sizeof(stringBuffer) - length }, "{}", std::abs(pointsDiff));
				}
				
				_smallFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + std::max(130.0f, playerNameSize.X + 48.0f), view.Y + offset + 1.0f, FontShadowLayer,
					Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
				_smallFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + std::max(130.0f, playerNameSize.X + 48.0f), view.Y + offset, FontLayer,
					Alignment::TopLeft, pointsDiff > 0 ? Colorf(0.45f, 0.27f, 0.22f, 0.5f) : Colorf(0.2f, 0.45f, 0.2f, 0.5f),
					0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			}

			offset += 16.0f;
		}
	}

	void MpHUD::DrawMinimap(const Rectf& view, Actors::Player* player)
	{
		auto* mpLevelHandler = static_cast<MpLevelHandler*>(_levelHandler);

		const auto& checkpoints = mpLevelHandler->_orderedRaceCheckpoints;
		if (checkpoints.size() < 2 || !PreferencesCache::ShowMinimap || mpLevelHandler->_console->IsVisible()) {
			return;
		}
		// On the server the checkpoints always exist (used for ranking), so honor AllowMinimap here too; clients
		// only receive them when the server allows the minimap, so their check above already covers it
		if (mpLevelHandler->_isServer && !mpLevelHandler->_networkManager->GetServerConfiguration().AllowMinimap) {
			return;
		}

		constexpr float TS = (float)Tiles::TileSet::DefaultTileSize;
		constexpr float Margin = 8.0f, Pad = 5.0f;

		// The track extent (in world pixels) is the area players can reach, computed server-side and synced
		const Vector2f mn(mpLevelHandler->_raceBoundsMin.X * TS, mpLevelHandler->_raceBoundsMin.Y * TS);
		const Vector2f mx((mpLevelHandler->_raceBoundsMax.X + 1) * TS, (mpLevelHandler->_raceBoundsMax.Y + 1) * TS);
		const float spanX = std::max(1.0f, mx.X - mn.X), spanY = std::max(1.0f, mx.Y - mn.Y);

		// The box matches the track's aspect ratio (so it isn't squished/cropped) and grows for bigger levels,
		// capped to a fraction of the screen
		const float aspect = spanX / spanY;
		const float maxW = std::min(view.W * 0.32f, 180.0f), maxH = std::min(view.H * 0.38f, 132.0f);
		const float minBoxW = 68.0f, minBoxH = 52.0f;
		float boxW, boxH;
		if (maxW / maxH > aspect) {
			boxH = maxH;
			boxW = maxH * aspect;
		} else {
			boxW = maxW;
			boxH = maxW / aspect;
		}
		boxW = std::max(boxW, minBoxW);
		boxH = std::max(boxH, minBoxH);

		float boxX = view.X + view.W - boxW - Margin;
		const float boxY = view.Y + Margin;

		// When the on-screen touch pause button is visible and would overlap the minimap's default top-right
		// position, tuck the minimap to the left of it; otherwise leave it where it is
		Rectf pauseRect;
		if (GetTouchPauseButtonRect(pauseRect)) {
			bool overlaps = (pauseRect.X < boxX + boxW && pauseRect.X + pauseRect.W > boxX &&
							 pauseRect.Y < boxY + boxH && pauseRect.Y + pauseRect.H > boxY);
			if (overlaps) {
				boxX = std::max(view.X + Margin, pauseRect.X - boxW - Margin);
			}
		}

		const float innerW = boxW - 2.0f * Pad, innerH = boxH - 2.0f * Pad;
		const float scale = std::min(innerW / spanX, innerH / spanY);
		const Vector2f drawnSize(spanX * scale, spanY * scale);
		const Vector2f origin(boxX + Pad + (innerW - drawnSize.X) * 0.5f, boxY + Pad + (innerH - drawnSize.Y) * 0.5f);

		auto toMinimap = [&](Vector2f world) -> Vector2f {
			return Vector2f(origin.X + (world.X - mn.X) * scale, origin.Y + (world.Y - mn.Y) * scale);
		};
		auto clampToBox = [&](Vector2f p) -> Vector2f {
			return Vector2f(std::min(std::max(p.X, boxX + Pad), boxX + boxW - Pad),
							std::min(std::max(p.Y, boxY + Pad), boxY + boxH - Pad));
		};

		// Smoothed thick track line drawn as a triangle-strip mesh, with a drop shadow underneath (no background panel)
		RefreshMinimapTrack(mpLevelHandler);

		const Colorf trackColor(0.95f, 0.96f, 1.0f, 0.9f);
		const Colorf shadowColor(0.0f, 0.0f, 0.0f, 0.45f);
		SmallVector<Vector2f, 0> screenPts;
		for (std::int32_t r = 0; r + 1 < (std::int32_t)_minimapRunOffsets.size(); r++) {
			std::int32_t a = _minimapRunOffsets[r], b = _minimapRunOffsets[r + 1];
			if (b - a < 2) {
				continue;
			}
			screenPts.clear();
			for (std::int32_t k = a; k < b; k++) {
				screenPts.push_back(toMinimap(Vector2f(_minimapPoints[k].X * TS, _minimapPoints[k].Y * TS)));
			}
			DrawMinimapTrackLine(&screenPts[0], (std::int32_t)screenPts.size(), Vector2f(0.8f, 1.0f), 2.6f, ShadowLayer, shadowColor);
			DrawMinimapTrackLine(&screenPts[0], (std::int32_t)screenPts.size(), Vector2f(0.0f, 0.0f), 1.8f, MainLayer, trackColor);
		}

		// Start/finish markers (warp "Set Lap" tiles)
		for (const auto& m : mpLevelHandler->_raceStartMarkers) {
			Vector2f p = clampToBox(toMinimap(Vector2f(m.X * TS, m.Y * TS)));
			DrawSolid(Vector2f(p.X - 2.5f, p.Y - 2.5f), MainLayer + 5, Vector2f(5.0f, 5.0f), Colorf(0.0f, 0.0f, 0.0f, 0.5f));
			DrawSolid(Vector2f(p.X - 1.5f, p.Y - 1.5f), MainLayer + 7, Vector2f(3.0f, 3.0f), Colorf(1.0f, 0.92f, 0.4f, 1.0f));
		}

		// Player dots in distinct colors; local player highlighted
		static const Colorf DotPalette[] = {
			Colorf(0.95f, 0.30f, 0.30f), Colorf(0.30f, 0.55f, 0.95f), Colorf(0.35f, 0.85f, 0.40f), Colorf(0.95f, 0.80f, 0.25f),
			Colorf(0.80f, 0.40f, 0.90f), Colorf(0.30f, 0.85f, 0.85f), Colorf(0.95f, 0.55f, 0.25f), Colorf(0.70f, 0.70f, 0.70f)
		};
		constexpr std::int32_t DotPaletteCount = (std::int32_t)(sizeof(DotPalette) / sizeof(DotPalette[0]));

		bool teamMode = IsTeamGameMode(mpLevelHandler->_networkManager->GetServerConfiguration().GameMode);

		auto drawPlayerDot = [&](Vector2f worldPos, std::uint32_t id, bool isLocal, std::uint8_t team) {
			Vector2f p = clampToBox(toMinimap(worldPos));
			float s = (isLocal ? 3.0f : 2.0f);
			Colorf color;
			if (teamMode) {
				// Color by team; the local player stays slightly brighter to stand out
				color = GetTeamColor(team);
				if (isLocal) {
					color = Colorf(std::min(1.0f, color.R + 0.25f), std::min(1.0f, color.G + 0.25f), std::min(1.0f, color.B + 0.25f), 1.0f);
				}
			} else {
				color = (isLocal ? Colorf(0.9f, 1.0f, 1.0f, 1.0f) : DotPalette[id % DotPaletteCount]);
			}
			DrawSolid(Vector2f(p.X - s * 0.5f - 1.0f, p.Y - s * 0.5f - 1.0f), MainLayer + 10, Vector2f(s + 2.0f, s + 2.0f), Colorf(0.0f, 0.0f, 0.0f, 0.7f));
			DrawSolid(Vector2f(p.X - s * 0.5f, p.Y - s * 0.5f), MainLayer + 12, Vector2f(s, s), color);
		};

		if (mpLevelHandler->_isServer) {
			auto peers = mpLevelHandler->_networkManager->GetPeers();
			for (auto& [peer, peerDesc] : *peers) {
				if (peerDesc->Player != nullptr) {
					drawPlayerDot(peerDesc->Player->GetPos(), peerDesc->Player->GetPlayerIndex(), !peerDesc->RemotePeer, peerDesc->Team);
				}
			}
		} else {
			// Local player is the currently viewed one
			std::uint8_t localTeam = 0;
			if (auto localPeerDesc = mpLevelHandler->_networkManager->GetPeerDescriptor(LocalPeer)) {
				localTeam = localPeerDesc->Team;
			}
			drawPlayerDot(player->GetPos(), mpLevelHandler->_lastSpawnedActorId, true, localTeam);

			for (auto& [actorId, playerName] : mpLevelHandler->_playerNames) {
				if (actorId == mpLevelHandler->_lastSpawnedActorId) {
					continue;
				}
				auto it = mpLevelHandler->_remoteActors.find(actorId);
				if (it != mpLevelHandler->_remoteActors.end()) {
					drawPlayerDot(it->second->GetPos(), actorId, false, playerName.Team);
				}
			}
		}
	}

	void MpHUD::RefreshMinimapTrack(Jazz2::Multiplayer::MpLevelHandler* mpLevelHandler)
	{
		const auto& checkpoints = mpLevelHandler->_orderedRaceCheckpoints;

		// A cheap key that changes whenever the checkpoint set changes (level load or game-mode switch), so the
		// (relatively expensive) smoothing is done once rather than every frame
		std::uint32_t key = (std::uint32_t)checkpoints.size();
		if (!checkpoints.empty()) {
			const auto& f = checkpoints[0];
			const auto& l = checkpoints[checkpoints.size() - 1];
			key = key * 2654435761u + (std::uint32_t)((f.Tile.X * 73856093) ^ (f.Tile.Y * 19349663) ^ f.Order);
			key = key * 2654435761u + (std::uint32_t)((l.Tile.X * 83492791) ^ (l.Tile.Y * 19349663) ^ l.Order);
		}
		if (key == _minimapCacheKey && !_minimapRunOffsets.empty()) {
			return;
		}
		_minimapCacheKey = key;
		_minimapPoints.clear();
		_minimapRunOffsets.clear();

		constexpr std::int32_t Subdivisions = 6;
		constexpr std::int32_t MaxPoints = 1024;

		// Split the ordered checkpoints into runs (broken where the group changes or the order isn't consecutive)
		// and smooth each run with a Catmull-Rom spline
		std::size_t i = 0;
		while (i < checkpoints.size()) {
			std::size_t j = i + 1;
			while (j < checkpoints.size()
				&& checkpoints[j].Group == checkpoints[j - 1].Group
				&& checkpoints[j].Order == checkpoints[j - 1].Order + 1) {
				j++;
			}

			std::int32_t runLen = (std::int32_t)(j - i);
			if (runLen >= 2) {
				// Simplify the run's control points first: drop points closer than MinSpacing tiles to the last kept
				// one so small wiggles merge into a smoother curve (first and last points are always kept)
				constexpr float MinSpacingSq = 3.0f * 3.0f;
				SmallVector<Vector2f, 0> ctrl;
				ctrl.push_back(Vector2f((float)checkpoints[i].Tile.X, (float)checkpoints[i].Tile.Y));
				for (std::int32_t s = 1; s < runLen - 1; s++) {
					Vector2f p((float)checkpoints[i + s].Tile.X, (float)checkpoints[i + s].Tile.Y);
					Vector2f last = ctrl[ctrl.size() - 1];
					float dx = p.X - last.X, dy = p.Y - last.Y;
					if (dx * dx + dy * dy >= MinSpacingSq) {
						ctrl.push_back(p);
					}
				}
				ctrl.push_back(Vector2f((float)checkpoints[j - 1].Tile.X, (float)checkpoints[j - 1].Tile.Y));

				if (ctrl.size() >= 2) {
					_minimapRunOffsets.push_back((std::int32_t)_minimapPoints.size());

					std::int32_t m = (std::int32_t)ctrl.size();
					for (std::int32_t s = 0; s < m - 1 && (std::int32_t)_minimapPoints.size() < MaxPoints; s++) {
						Vector2f p0 = ctrl[std::max(0, s - 1)];
						Vector2f p1 = ctrl[s];
						Vector2f p2 = ctrl[s + 1];
						Vector2f p3 = ctrl[std::min(m - 1, s + 2)];
						for (std::int32_t t = 0; t < Subdivisions; t++) {
							float u = (float)t / Subdivisions;
							float u2 = u * u, u3 = u2 * u;
							Vector2f pt = (p1 * 2.0f + (p2 - p0) * u + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * u2
								+ (p1 * 3.0f - p0 - p2 * 3.0f + p3) * u3) * 0.5f;
							_minimapPoints.push_back(pt);
						}
					}
					_minimapPoints.push_back(ctrl[m - 1]);
				}
			}

			i = j;
		}
		_minimapRunOffsets.push_back((std::int32_t)_minimapPoints.size());
	}

	void MpHUD::DrawMinimapTrackLine(const Vector2f* points, std::int32_t count, Vector2f offset, float halfThickness, std::uint16_t z, const Colorf& color)
	{
		if (count < 2) {
			return;
		}

		// Lazily create a 1D alpha-feather texture (opaque core fading to transparent edges). Mapping it across
		// the line's thickness gives smooth, antialiased edges with linear filtering.
		if (_minimapLineTexture == nullptr) {
			constexpr std::int32_t LineTexWidth = 16;
			std::uint8_t texels[LineTexWidth * 4];
			for (std::int32_t x = 0; x < LineTexWidth; x++) {
				float d = std::abs((x + 0.5f) / LineTexWidth - 0.5f) * 2.0f; // 0 at center, ~1 at edges
				float a = std::min(1.0f, std::max(0.0f, 1.0f - (d - 0.65f) / 0.30f));
				texels[x * 4 + 0] = 255;
				texels[x * 4 + 1] = 255;
				texels[x * 4 + 2] = 255;
				texels[x * 4 + 3] = (std::uint8_t)(a * 255.0f);
			}
			_minimapLineTexture = std::make_unique<Texture>("RaceMinimapLine", Texture::Format::RGBA8, LineTexWidth, 1);
			_minimapLineTexture->LoadFromTexels(texels, 0, 0, LineTexWidth, 1);
			_minimapLineTexture->SetMinFiltering(SamplerFilter::Linear);
			_minimapLineTexture->SetMagFiltering(SamplerFilter::Linear);
			_minimapLineTexture->SetWrap(SamplerWrapping::ClampToEdge);
		}

		std::int32_t vertexCount = count * 2; // a left/right vertex pair per centerline point (triangle strip)
		if (_minimapVertexCount + vertexCount * 4 > MinimapMaxVertexFloats) {
			return; // not enough room in the persistent vertex buffer this frame (4 floats per vertex: x, y, u, v)
		}

		std::int32_t startFloat = _minimapVertexCount;
		for (std::int32_t i = 0; i < count; i++) {
			Vector2f dir;
			if (i == 0) {
				dir = points[1] - points[0];
			} else if (i == count - 1) {
				dir = points[count - 1] - points[count - 2];
			} else {
				dir = points[i + 1] - points[i - 1];
			}
			float len = dir.Length();
			Vector2f normal = (len > 0.001f ? Vector2f(-dir.Y / len, dir.X / len) : Vector2f(0.0f, 1.0f));
			Vector2f base = points[i] + offset;
			Vector2f left = base + normal * halfThickness;
			Vector2f right = base - normal * halfThickness;
			// U runs across the thickness (0 = one edge, 1 = the other) to sample the feather texture; V is unused
			_minimapVertices[_minimapVertexCount++] = left.X;
			_minimapVertices[_minimapVertexCount++] = left.Y;
			_minimapVertices[_minimapVertexCount++] = 0.0f;
			_minimapVertices[_minimapVertexCount++] = 0.0f;
			_minimapVertices[_minimapVertexCount++] = right.X;
			_minimapVertices[_minimapVertexCount++] = right.Y;
			_minimapVertices[_minimapVertexCount++] = 1.0f;
			_minimapVertices[_minimapVertexCount++] = 0.0f;
		}

		RenderCommand* command;
		if (_minimapCommandsCount < _minimapCommands.size()) {
			command = _minimapCommands[_minimapCommandsCount].get();
		} else {
			command = _minimapCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::MeshSprite)).get();
			command->GetMaterial().SetBlendingEnabled(true);
		}
		_minimapCommandsCount++;

		if (command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::MeshSprite)) {
			command->GetMaterial().ReserveUniformsDataMemory();
			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
		}
		command->GetMaterial().SetBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		command->GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, vertexCount);
		command->GetGeometry().SetElementsPerVertex(4);
		command->GetGeometry().SetHostVertexPointer(&_minimapVertices[startFloat]);

		auto instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(1.0f, 1.0f);
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());

		command->SetTransformation(Matrix4x4f::Identity);
		command->SetLayer(z);
		command->GetMaterial().SetTexture(0, *_minimapLineTexture);

		DrawRenderCommand(command);
	}
}

#endif