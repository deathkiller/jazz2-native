#include "MpHUD.h"

#if defined(WITH_MULTIPLAYER)

#include "../../Actors/Multiplayer/PlayerOnServer.h"
#include "../../Multiplayer/MpLevelHandler.h"
#include "../../ContentResolver.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"

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
		: HUD(levelHandler), _countdownTimeLeft(0.0f)
	{
		auto& resolver = ContentResolver::Get();
		_mediumFont = resolver.GetFont(FontType::Medium);
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
		if (!HUD::OnDraw(renderQueue)) {
			return false;
		}

		std::int32_t charOffset = 0;

		// Debug information
		std::int32_t debugCharOffset = 0, debugShadowCharOffset = 0;
		_smallFont->DrawString(this, "This is online multiplayer preview, not final release!"_s, debugShadowCharOffset, ViewSize.X / 2, 1.0f + 1.0f,
			180, Alignment::Top, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.76f, 0.7f, 0.7f, 0.7f, 0.2f, 0.9f);
		_smallFont->DrawString(this, "This is online multiplayer preview, not final release!"_s, debugCharOffset, ViewSize.X / 2, 1.0f,
			190, Alignment::Top, Colorf(0.62f, 0.44f, 0.34f, 0.46f), 0.76f, 0.7f, 0.7f, 0.7f, 0.2f, 0.9f);

		if (_countdownTimeLeft > 0.0f) {
			float textScale = 2.0f - std::min(_countdownTimeLeft / FrameTimer::FramesPerSecond, 1.0f);
			Colorf textColor = Font::DefaultColor;
			textColor.A = std::min(_countdownTimeLeft / FrameTimer::FramesPerSecond, 0.5f) * 2.0f;
			_mediumFont->DrawString(this, _countdownText, charOffset, ViewSize.X * 0.5f, ViewSize.Y * 0.5f, FontLayer + 20,
				Alignment::Center, textColor, textScale, 0.0f, 0.0f, 0.0f);
		}

		if (PreferencesCache::ShowPerformanceMetrics) {
			auto* mpLevelHandler = static_cast<MpLevelHandler*>(_levelHandler);
			if (mpLevelHandler->_isServer) {
#if defined(DEATH_DEBUG)
				char debugBuffer[64];
				std::size_t length = formatInto(debugBuffer, "{} b |", mpLevelHandler->_debugAverageUpdatePacketSize);
				_smallFont->DrawString(this, { debugBuffer, length }, debugCharOffset, ViewSize.X - 44.0f, 1.0f,
					200, Alignment::TopRight, Font::DefaultColor, 0.8f);
#endif
			} else {
				char debugBuffer[64];
				std::size_t length = formatInto(debugBuffer, "{} ms |", mpLevelHandler->_networkManager->GetRoundTripTimeMs());
				_smallFont->DrawString(this, { debugBuffer, length }, debugCharOffset, ViewSize.X - 44.0f, 1.0f,
					200, Alignment::TopRight, Font::DefaultColor, 0.8f);
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

		if (serverConfig.GameMode != MpGameMode::Cooperation && serverConfig.TotalPlayerPoints > 0 && peerDesc->Points > 0) {
			auto pointsText = _f("Points: {}", peerDesc->Points);
			_smallFont->DrawString(this, pointsText, charOffsetShadow, view.X + view.W - 14.0f, view.Y + 30.0f + 1.0f, FontShadowLayer,
				Alignment::TopRight, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
			_smallFont->DrawString(this, pointsText, charOffset, view.X + view.W - 14.0f, view.Y + 30.0f, FontLayer,
				Alignment::TopRight, Font::DefaultColor, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
		}

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

		char stringBuffer[32];
		std::int32_t charOffset = 0, charOffsetShadow = 0;

		auto* mpLevelHandler = static_cast<MpLevelHandler*>(_levelHandler);
		const auto& serverConfig = mpLevelHandler->_networkManager->GetServerConfiguration();
		auto* mpPlayer = static_cast<MpPlayer*>(player);
		auto peerDesc = mpPlayer->GetPeerDescriptor();

		if DEATH_UNLIKELY(mpLevelHandler->_levelState != MpLevelHandler::LevelState::Running) {
			return;
		}

		switch (serverConfig.GameMode) {
			case MpGameMode::Battle:
			case MpGameMode::TeamBattle: {
				std::size_t length = formatInto(stringBuffer, "K: {}  D: {}  /{}", peerDesc->Kills, peerDesc->Deaths, serverConfig.TotalKills);
				_mediumFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + 14.0f, view.Y + 5.0f + 2.0f, FontShadowLayer,
					Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f);
				_mediumFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + 14.0f, view.Y + 5.0f, FontLayer,
					Alignment::TopLeft, Font::DefaultColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f);

				DrawPositionInRound(view, player);
				break;
			}
			case MpGameMode::Race:
			case MpGameMode::TeamRace: {
				std::size_t length = formatInto(stringBuffer, "{}/{}", peerDesc->Laps + 1, serverConfig.TotalLaps);
				_mediumFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + 65.0f, view.Y + 7.0f + 2.0f, FontShadowLayer,
					Alignment::TopRight, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.88f, 0.0f, 0.0f, 0.0f, 0.0f);
				_mediumFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + 65.0f, view.Y + 7.0f, FontLayer,
					Alignment::TopRight, Font::DefaultColor, 0.88f, 0.8f, 0.0f, 0.0f, 0.0f);

				float sinceLapStarted = peerDesc->LapStarted.secondsSince();
				std::int32_t minutes = std::max(0, (std::int32_t)(sinceLapStarted / 60));
				std::int32_t seconds = std::max(0, (std::int32_t)fmod(sinceLapStarted, 60));
				std::int32_t milliseconds = std::max(0, (std::int32_t)(fmod(sinceLapStarted, 1) * 100));

				length = formatInto(stringBuffer, "{}:{:.2}:{:.2}", minutes, seconds, milliseconds);
				_mediumFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + 14.0f + 80.0f, view.Y + 10.0f + 1.4f, FontShadowLayer,
					Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.7f, 0.0f, 0.0f, 0.0f, 0.0f);
				_mediumFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + 14.0f + 80.0f, view.Y + 10.0f, FontLayer,
					Alignment::TopLeft, Font::DefaultColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f);

				DrawPositionInRound(view, player);
				break;
			}
			case MpGameMode::TreasureHunt:
			case MpGameMode::TeamTreasureHunt: {
				bool hasEnoughTreasure = (peerDesc->TreasureCollected >= serverConfig.TotalTreasureCollected);
				Colorf textColor = (hasEnoughTreasure ? Colorf(0.34f, 0.5f, 0.38f, 0.5f) : Font::DefaultColor);

				AnimState animState = (hasEnoughTreasure ? PickupGemGreen : PickupGemRed);
				DrawElement(animState, -1, view.X + 8.0f, view.Y + 8.0f + 2.5f, ShadowLayer, Alignment::TopLeft,
					Colorf(0.0f, 0.0f, 0.0f, 0.4f), 0.8f, 0.8f);
				DrawElement(animState, -1, view.X + 8.0f, view.Y + 8.0f, MainLayer, Alignment::TopLeft,
					Colorf(1.0f, 1.0f, 1.0f, 0.8f), 0.8f, 0.8f);

				std::size_t length = formatInto(stringBuffer, "{}/{}", peerDesc->TreasureCollected, serverConfig.TotalTreasureCollected);
				_mediumFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + 38.0f, view.Y + 10.0f + 2.0f, FontShadowLayer,
					Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f);
				_mediumFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + 38.0f, view.Y + 10.0f, FontLayer,
					Alignment::TopLeft, textColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f);

				if (hasEnoughTreasure) {
					auto fintExitTest = _("Find exit!");
					_smallFont->DrawString(this, fintExitTest, charOffsetShadow, view.X + view.W * 0.5f, view.Y + 36.0f + 2.5f, FontShadowLayer,
						Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 1.0f, 0.72f, 0.8f, 0.8f);
					_smallFont->DrawString(this, fintExitTest, charOffset, view.X + view.W * 0.5f, view.Y + 36.0f, FontLayer,
						Alignment::Center, Font::DefaultColor, 1.0f, 0.72f, 0.8f, 0.8f);
				}

				DrawPositionInRound(view, player);
				break;
			}
			case MpGameMode::CaptureTheFlag: {

				// TODO
				/*std::size_t length = formatInto(stringBuffer, "{} / {}", peerDesc->FlagsCaptured, serverConfig.TotalFlagsCaptured);
				_smallFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + 14.0f, view.Y + 5.0f + 1.0f, FontShadowLayer,
					Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
				_smallFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + 14.0f, view.Y + 5.0f, FontLayer,
					Alignment::TopLeft, Font::DefaultColor, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);*/

				break;
			}
			default: {
				if (PreferencesCache::EnableReforgedHUD) {
					DrawElement(PickupFood, -1, view.X + 3.0f, view.Y + 3.0f + 1.6f, ShadowLayer, Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.4f));
					DrawElement(PickupFood, -1, view.X + 3.0f, view.Y + 3.0f, MainLayer, Alignment::TopLeft, Colorf::White);

					std::size_t length = formatInto(stringBuffer, "{:.8}", player->GetScore());
					_smallFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + 14.0f, view.Y + 5.0f + 1.0f, FontShadowLayer,
						Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
					_smallFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + 14.0f, view.Y + 5.0f, FontLayer,
						Alignment::TopLeft, Font::DefaultColor, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
				} else {
					std::size_t length = formatInto(stringBuffer, "{:.8}", player->GetScore());
					_smallFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + 4.0f, view.Y + 1.0f + 1.0f, FontShadowLayer,
						Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
					_smallFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + 4.0f, view.Y + 1.0f, FontLayer,
						Alignment::TopLeft, Font::DefaultColor, 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
				}
				break;
			}
		}
	}

	struct PositionInRoundItem
	{
		StringView PlayerName;
		std::uint32_t PositionInRound;
		std::uint32_t PointsInRound;
		bool IsLocal;

		PositionInRoundItem(StringView playerName, std::uint32_t position, std::uint32_t points, bool isLocal)
			: PlayerName(playerName), PositionInRound(position), PointsInRound(points), IsLocal(isLocal) {}
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

					positions.emplace_back(peerDesc->PlayerName, peerDesc->PositionInRound, peerDesc->PointsInRound, !peerDesc->RemotePeer);
				}
			}
		} else {
			for (const auto& pos : mpLevelHandler->_positionsInRound) {
				if ((pos.PositionInRound >= 1 && pos.PositionInRound <= 3) || pos.ActorID == mpLevelHandler->_lastSpawnedActorId) {
					StringView playerName;
					if (pos.ActorID == mpLevelHandler->_lastSpawnedActorId) {
						localPoints = pos.PointsInRound;
						playerName = mpLevelHandler->_networkManager->GetPeerDescriptor(LocalPeer)->PlayerName;
					} else {
						auto it = mpLevelHandler->_playerNames.find(pos.ActorID);
						if (it != mpLevelHandler->_playerNames.end()) {
							playerName = it->second.Name;
						}
					}

					positions.emplace_back(playerName, pos.PositionInRound, pos.PointsInRound, pos.ActorID == mpLevelHandler->_lastSpawnedActorId);
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
			_smallFont->DrawString(this, item.PlayerName, charOffsetShadow, view.X + 38.0f, view.Y + offset + 1.0f, FontShadowLayer,
				Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			_smallFont->DrawString(this, item.PlayerName, charOffset, view.X + 38.0f, view.Y + offset, FontLayer,
				Alignment::TopLeft, item.IsLocal ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : Font::DefaultColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

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
}

#endif