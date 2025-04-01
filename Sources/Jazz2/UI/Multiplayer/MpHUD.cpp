#include "MpHUD.h"

#if defined(WITH_MULTIPLAYER)

#include "../../Actors/Multiplayer/PlayerOnServer.h"
#include "../../Multiplayer/MpLevelHandler.h"
#include "../../PreferencesCache.h"

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

		if (_countdownTimeLeft > 0.0f) {
			float textScale = 2.0f - std::min(_countdownTimeLeft / FrameTimer::FramesPerSecond, 1.0f);
			Colorf textColor = Font::DefaultColor;
			textColor.A = std::min(_countdownTimeLeft / FrameTimer::FramesPerSecond, 0.5f) * 2.0f;
			_mediumFont->DrawString(this, _countdownText, charOffset, ViewSize.X * 0.5f, ViewSize.Y * 0.5f, FontLayer + 20,
				Alignment::Center, textColor, textScale, 0.0f, 0.0f, 0.0f);
		}

		return true;
	}

	void MpHUD::ShowCountdown(StringView text)
	{
		_countdownText = text;
		_countdownTimeLeft = FrameTimer::FramesPerSecond;
	}

	void MpHUD::OnDrawScore(const Rectf& view, Actors::Player* player)
	{
#if defined(WITH_ANGELSCRIPT)
		/*if (_levelHandler->_scripts != nullptr && _levelHandler->_scripts->OnDraw(this, player, view, Scripting::DrawType::Score)) {
			return;
		}*/
#endif

		char stringBuffer[32];
		std::int32_t charOffset = 0;
		std::int32_t charOffsetShadow = 0;

		auto* mpLevelHandler = static_cast<MpLevelHandler*>(_levelHandler);
		if (mpLevelHandler->_levelState == MpLevelHandler::LevelState::PreGame) {
			float sinceLapStarted = mpLevelHandler->_gameTimeLeft / FrameTimer::FramesPerSecond;
			std::int32_t minutes = (std::int32_t)(sinceLapStarted / 60);
			std::int32_t seconds = (std::int32_t)fmod(sinceLapStarted, 60);
			std::int32_t milliseconds = (std::int32_t)(fmod(sinceLapStarted, 1) * 100);

			formatString(stringBuffer, sizeof(stringBuffer), "%d:%02d:%02d", minutes, seconds, milliseconds);
			auto gameStartsInText = _f("Game starts in %s", stringBuffer);
			_smallFont->DrawString(this, gameStartsInText, charOffsetShadow, view.X + 17.0f, view.Y + 8.0f + 1.0f, FontShadowLayer,
				Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
			_smallFont->DrawString(this, gameStartsInText, charOffset, view.X + 17.0f, view.Y + 8.0f, FontLayer,
				Alignment::TopLeft, Font::DefaultColor, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);

			return;
		} else if (mpLevelHandler->_levelState != MpLevelHandler::LevelState::Running) {
			return;
		}

		const auto& serverConfig = mpLevelHandler->_networkManager->GetServerConfiguration();
		auto* mpPlayer = static_cast<MpPlayer*>(player);
		auto peerDesc = mpPlayer->GetPeerDescriptor();

		switch (serverConfig.GameMode) {
			case MpGameMode::Battle:
			case MpGameMode::TeamBattle: {

				formatString(stringBuffer, sizeof(stringBuffer), "K: %u  D: %u  /%u", peerDesc->Kills, peerDesc->Deaths, serverConfig.TotalKills);
				_mediumFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + 14.0f, view.Y + 5.0f + 1.0f, FontShadowLayer,
					Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f);
				_mediumFont->DrawString(this, stringBuffer, charOffset, view.X + 14.0f, view.Y + 5.0f, FontLayer,
					Alignment::TopLeft, Font::DefaultColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f);

				break;
			}
			case MpGameMode::Race:
			case MpGameMode::TeamRace: {

				formatString(stringBuffer, sizeof(stringBuffer), "%u/%u", peerDesc->Laps + 1, serverConfig.TotalLaps);
				_mediumFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + 20.0f, view.Y + 5.0f + 1.4f, FontShadowLayer,
					Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.88f, 0.0f, 0.0f, 0.0f, 0.0f);
				_mediumFont->DrawString(this, stringBuffer, charOffset, view.X + 20.0f, view.Y + 5.0f, FontLayer,
					Alignment::TopLeft, Font::DefaultColor, 0.88f, 0.8f, 0.0f, 0.0f, 0.0f);

				float sinceLapStarted = peerDesc->LapStarted.secondsSince();
				std::int32_t minutes = (std::int32_t)(sinceLapStarted / 60);
				std::int32_t seconds = (std::int32_t)fmod(sinceLapStarted, 60);
				std::int32_t milliseconds = (std::int32_t)(fmod(sinceLapStarted, 1) * 100);

				formatString(stringBuffer, sizeof(stringBuffer), "%d:%02d:%02d", minutes, seconds, milliseconds);
				_mediumFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + 14.0f + 80.0f, view.Y + 8.0f + 1.4f, FontShadowLayer,
					Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.7f, 0.0f, 0.0f, 0.0f, 0.0f);
				_mediumFont->DrawString(this, stringBuffer, charOffset, view.X + 14.0f + 80.0f, view.Y + 8.0f, FontLayer,
					Alignment::TopLeft, Font::DefaultColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f);

				break;
			}
			case MpGameMode::TreasureHunt:
			case MpGameMode::TeamTreasureHunt: {

				formatString(stringBuffer, sizeof(stringBuffer), "%u/%u", peerDesc->TreasureCollected, serverConfig.TotalTreasureCollected);
				_mediumFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + 14.0f, view.Y + 5.0f + 1.0f, FontShadowLayer,
					Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f);
				_mediumFont->DrawString(this, stringBuffer, charOffset, view.X + 14.0f, view.Y + 5.0f, FontLayer,
					Alignment::TopLeft, Font::DefaultColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f);

				break;
			}
			case MpGameMode::CaptureTheFlag: {

				// TODO
				/*formatString(stringBuffer, sizeof(stringBuffer), "%u / %u", peerDesc->FlagsCaptured, serverConfig.TotalFlagsCaptured);
				_smallFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + 14.0f, view.Y + 5.0f + 1.0f, FontShadowLayer,
					Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
				_smallFont->DrawString(this, stringBuffer, charOffset, view.X + 14.0f, view.Y + 5.0f, FontLayer,
					Alignment::TopLeft, Font::DefaultColor, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);*/

				break;
			}
			default: {
				if (PreferencesCache::EnableReforgedHUD) {
					DrawElement(PickupFood, -1, view.X + 3.0f, view.Y + 3.0f + 1.6f, ShadowLayer, Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.4f));
					DrawElement(PickupFood, -1, view.X + 3.0f, view.Y + 3.0f, MainLayer, Alignment::TopLeft, Colorf::White);

					formatString(stringBuffer, sizeof(stringBuffer), "%08i", player->GetScore());
					_smallFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + 14.0f, view.Y + 5.0f + 1.0f, FontShadowLayer,
						Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
					_smallFont->DrawString(this, stringBuffer, charOffset, view.X + 14.0f, view.Y + 5.0f, FontLayer,
						Alignment::TopLeft, Font::DefaultColor, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
				} else {
					formatString(stringBuffer, sizeof(stringBuffer), "%08i", player->GetScore());
					_smallFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + 4.0f, view.Y + 1.0f + 1.0f, FontShadowLayer,
						Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
					_smallFont->DrawString(this, stringBuffer, charOffset, view.X + 4.0f, view.Y + 1.0f, FontLayer,
						Alignment::TopLeft, Font::DefaultColor, 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
				}
				break;
			}
		}
	}
}

#endif