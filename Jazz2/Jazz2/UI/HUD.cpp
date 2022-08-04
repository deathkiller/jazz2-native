#include "HUD.h"

#include "../LevelHandler.h"

#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/IO/IFileStream.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Application.h"

namespace Jazz2::UI
{
	HUD::HUD(LevelHandler* levelHandler)
		:
		_levelHandler(levelHandler),
		_graphics(nullptr),
		_levelTextTime(-1.0f),
		_coins(0), _gems(0),
		_coinsTime(-1.0f), _gemsTime(-1.0f)
	{
		Metadata* metadata = ContentResolver::Current().RequestMetadata("UI/HUD");
		if (metadata != nullptr) {
			_graphics = &metadata->Graphics;
		}

		_smallFont = std::make_unique<Font>(fs::joinPath({ "Content"_s, "Animations"_s, "_custom"_s, "font_small.png"_s }));
	}

	void HUD::OnUpdate(float timeMult)
	{
		Canvas::OnUpdate(timeMult);

		if (_levelTextTime >= 0.0f) {
			_levelTextTime += timeMult;
		}
		if (_coinsTime >= 0.0f) {
			_coinsTime += timeMult;
		}
		if (_gemsTime >= 0.0f) {
			_gemsTime += timeMult;
		}
	}

	bool HUD::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		if (_graphics == nullptr) {
			return false;
		}

		ViewSize = _levelHandler->GetViewSize();

		Rectf view = Rectf(0, 0, ViewSize.X, ViewSize.Y);
		Rectf adjustedView = view;
		//AdjustVisibleZone(ref view);

		float right = adjustedView.X + adjustedView.W;
		float bottom = adjustedView.Y + adjustedView.H;

		int charOffset = 0;
		int charOffsetShadow = 0;
		char stringBuffer[32];

		auto& players = _levelHandler->GetPlayers();
		if (!players.empty()) {
			Actors::Player* player = players[0];
			PlayerType playerType = player->_playerType;

			// Bottom left
			StringView playerIcon;
			switch (playerType) {
				default:
				case PlayerType::Jazz: playerIcon = "CharacterJazz"_s; break;
				case PlayerType::Spaz: playerIcon = "CharacterSpaz"_s; break;
				case PlayerType::Lori: playerIcon = "CharacterLori"_s; break;
				case PlayerType::Frog: playerIcon = "CharacterFrog"_s; break;
			}

			DrawElement(playerIcon, -1, adjustedView.X + 36, bottom + 1.6f, ShadowLayer, Alignment::BottomRight, Colorf(0.0f, 0.0f, 0.0f, 0.4f));
			DrawElement(playerIcon, -1, adjustedView.X + 36, bottom, MainLayer, Alignment::BottomRight, Colorf::White);

			for (int i = 0; i < player->_health; i++) {
				stringBuffer[i] = '|';
			}
			stringBuffer[player->_health] = '\0';

			if (player->_lives > 0) {
				_smallFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + 36 - 3 - 0.5f, bottom - 16 + 0.5f, FontShadowLayer,
					Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.42f), 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 1.1f);
				_smallFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + 36 - 3 + 0.5f, bottom - 16 - 0.5f, FontShadowLayer,
					Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.42f), 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 1.1f);
				_smallFont->DrawString(this, stringBuffer, charOffset, view.X + 36 - 3, bottom - 16, FontLayer,
					Alignment::BottomLeft, Colorf::White, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 1.1f);

				snprintf(stringBuffer, _countof(stringBuffer), "x%i", player->_lives);
				_smallFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + 36 - 4, bottom + 1.0f, FontShadowLayer,
					Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f));
				_smallFont->DrawString(this, stringBuffer, charOffset, view.X + 36 - 4, bottom, FontLayer,
					Alignment::BottomLeft, Colorf::White);
			} else {
				_smallFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + 36 - 3 - 0.5f, bottom - 3 + 0.5f, FontShadowLayer,
					Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.42f), 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 1.1f);
				_smallFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + 36 - 3 + 0.5f, bottom - 3 - 0.5f, FontShadowLayer,
					Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.42f), 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 1.1f);
				_smallFont->DrawString(this, stringBuffer, charOffset, view.X + 36 - 3, bottom - 3, FontLayer,
					Alignment::BottomLeft, Colorf::White, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 1.1f);
			}

			// Top left
			DrawElement("PickupFood"_s, -1, view.X + 3, view.Y + 3 + 1.6f, ShadowLayer, Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.4f));
			DrawElement("PickupFood"_s, -1, view.X + 3, view.Y + 3, MainLayer, Alignment::TopLeft, Colorf::White);

			snprintf(stringBuffer, _countof(stringBuffer), "%08i", player->_score);
			_smallFont->DrawString(this, stringBuffer, charOffsetShadow, 14, 5 + 1, FontShadowLayer,
				Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
			_smallFont->DrawString(this, stringBuffer, charOffset, 14, 5, FontLayer,
				Alignment::TopLeft, Colorf::White, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);

			// Bottom right
			if (playerType != PlayerType::Frog) {
				WeaponType weapon = player->_currentWeapon;
				Vector2f pos = Vector2f(right - 40, bottom);
				StringView currentWeaponString = GetCurrentWeapon(player, weapon, pos);

				StringView ammoCount;
				if (player->_weaponAmmo[(int)weapon] < 0) {
					ammoCount = "x\u221E"_s;
				} else {
					snprintf(stringBuffer, _countof(stringBuffer), "x%i", player->_weaponAmmo[(int)weapon] / 100);
					ammoCount = stringBuffer;
				}
				_smallFont->DrawString(this, ammoCount, charOffsetShadow, right - 40, bottom + 1.0f, FontShadowLayer,
					Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.96f);
				_smallFont->DrawString(this, ammoCount, charOffset, right - 40, bottom, FontLayer,
					Alignment::BottomLeft, Colorf::White, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.96f);

				auto it = _graphics->find(String::nullTerminatedView(currentWeaponString));
				if (it != _graphics->end()) {
					if (it->second.Base->FrameDimensions.Y < 20) {
						pos.Y -= std::round((20 - it->second.Base->FrameDimensions.Y) * 0.5f);
					}

					DrawElement(currentWeaponString, -1, pos.X, pos.Y + 1.6f, ShadowLayer, Alignment::BottomRight, Colorf(0.0f, 0.0f, 0.0f, 0.4f));
					DrawElement(currentWeaponString, -1, pos.X, pos.Y, MainLayer, Alignment::BottomRight, Colorf::White);
				}
			}

			// Active Boss (health bar)
			// TODO

			// Misc
			DrawLevelText(charOffset);
			DrawCoins(charOffset);
			DrawGems(charOffset);

			// TODO
			//DrawWeaponWheel();

			// FPS
			snprintf(stringBuffer, _countof(stringBuffer), "%i", (int)std::round(theApplication().averageFps()));
			_smallFont->DrawString(this, stringBuffer, charOffset, right - 4, 0, FontLayer,
				Alignment::TopRight, Colorf::White, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.96f);
		}

		return true;
	}

	void HUD::DrawLevelText(int& charOffset)
	{
		constexpr float StillTime = 350.0f;
		constexpr float TransitionTime = 100.0f;
		constexpr float TotalTime = StillTime + TransitionTime * 2.0f;

		if (_levelTextTime < 0.0f) {
			return;
		}

		float offset;
		if (_levelTextTime < TransitionTime) {
			offset = std::powf((TransitionTime - _levelTextTime) / 12.0f, 3);
		} else if (_levelTextTime > TransitionTime + StillTime) {
			offset = -std::powf((_levelTextTime - TransitionTime - StillTime) / 12.0f, 3);
		} else {
			offset = 0;
		}

		int charOffsetShadow = charOffset;
		_smallFont->DrawString(this, _levelText, charOffsetShadow, ViewSize.X * 0.5f + offset, ViewSize.Y * 0.06f + 2.5f, FontShadowLayer,
			Alignment::Top, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 1.0f, 0.72f, 0.8f, 0.8f);

		_smallFont->DrawString(this, _levelText, charOffset, ViewSize.X * 0.5f + offset, ViewSize.Y * 0.06f, FontLayer,
			Alignment::Top, Colorf::White, 1.0f, 0.72f, 0.8f, 0.8f);

		if (_levelTextTime > TotalTime) {
			_levelTextTime = -1.0f;
			_levelText = { };
		}
	}

	void HUD::DrawCoins(int& charOffset)
	{
		constexpr float StillTime = 120.0f;
		constexpr float TransitionTime = 60.0f;
		constexpr float TotalTime = StillTime + TransitionTime * 2.0f;

		if (_coinsTime < 0.0f) {
			return;
		}

		float offset, alpha;
		if (_coinsTime < TransitionTime) {
			offset = (TransitionTime - _coinsTime) / 10.0f;
			offset = -(offset * offset);
			alpha = std::max(_coinsTime / TransitionTime, 0.1f);
		} else if (_coinsTime > TransitionTime + StillTime) {
			offset = (_coinsTime - TransitionTime - StillTime) / 10.0f;
			offset = (offset * offset);
			alpha = (TotalTime - _coinsTime) / TransitionTime;
		} else {
			offset = 0.0f;
			alpha = 1.0f;
		}

		DrawElement("PickupCoin"_s, -1, ViewSize.X * 0.5f, ViewSize.Y * 0.92f + 2.5f + offset, ShadowLayer,
			Alignment::Right, Colorf(0.0f, 0.0f, 0.0f, 0.2f * alpha), 0.8f, 0.8f);
		DrawElement("PickupCoin"_s, -1, ViewSize.X * 0.5f, ViewSize.Y * 0.92f + offset, MainLayer,
			Alignment::Right, Colorf(1.0f, 1.0f, 1.0f, alpha * alpha), 0.8f, 0.8f);

		char stringBuffer[32];
		snprintf(stringBuffer, _countof(stringBuffer), "x%i", _coins);

		int charOffsetShadow = charOffset;
		_smallFont->DrawString(this, stringBuffer, charOffsetShadow, ViewSize.X * 0.5f, ViewSize.Y * 0.92f + 2.5f + offset, FontShadowLayer,
			Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f * alpha), 1.0f, 0.0f, 0.0f, 0.0f);

		_smallFont->DrawString(this, stringBuffer, charOffset, ViewSize.X * 0.5f, ViewSize.Y * 0.92f + offset, FontLayer,
			Alignment::Left, Colorf(1.0f, 1.0f, 1.0f, alpha), 1.0f, 0.0f, 0.0f, 0.0f);

		if (_coinsTime > TotalTime) {
			_coinsTime = -1.0f;
		}
	}

	void HUD::DrawGems(int& charOffset)
	{
		constexpr float StillTime = 120.0f;
		constexpr float TransitionTime = 60.0f;
		constexpr float TotalTime = StillTime + TransitionTime * 2.0f;

		if (_gemsTime < 0.0f) {
			return;
		}

		float offset, alpha;
		if (_gemsTime < TransitionTime) {
			offset = (TransitionTime - _gemsTime) / 10.0f;
			offset = -(offset * offset);
			alpha = std::max(_gemsTime / TransitionTime, 0.1f);
		} else if (_gemsTime > TransitionTime + StillTime) {
			offset = (_gemsTime - TransitionTime - StillTime) / 10.0f;
			offset = (offset * offset);
			alpha = (TotalTime - _gemsTime) / TransitionTime;
		} else {
			offset = 0.0f;
			alpha = 1.0f;
		}

		float animAlpha = alpha * alpha;
		DrawElement("PickupGem"_s, -1, ViewSize.X * 0.5f, ViewSize.Y * 0.92f + 2.5f + offset, ShadowLayer, Alignment::Right,
			Colorf(0.0f, 0.0f, 0.0f, 0.4f * animAlpha), 0.8f, 0.8f);
		DrawElement("PickupGem"_s, -1, ViewSize.X * 0.5f, ViewSize.Y * 0.92f + offset, MainLayer, Alignment::Right,
			Colorf(1.0f, 1.0f, 1.0f, animAlpha), 0.8f, 0.8f);

		char stringBuffer[32];
		snprintf(stringBuffer, _countof(stringBuffer), "x%i", _gems);

		int charOffsetShadow = charOffset;
		_smallFont->DrawString(this, stringBuffer, charOffsetShadow, ViewSize.X * 0.5f, ViewSize.Y * 0.92f + 2.5f + offset, FontShadowLayer,
			Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f * alpha), 1.0f, 0.0f, 0.0f, 0.0f);

		_smallFont->DrawString(this, stringBuffer, charOffset, ViewSize.X * 0.5f, ViewSize.Y * 0.92f + offset, FontLayer,
			Alignment::Left, Colorf(1.0f, 1.0f, 1.0f, alpha), 1.0f, 0.0f, 0.0f, 0.0f);

		if (_gemsTime > TotalTime) {
			_gemsTime = -1.0f;
		}
	}

	void HUD::DrawElement(const StringView& name, int frame, float x, float y, uint16_t z, Alignment alignment, const Colorf& color, float scaleX, float scaleY)
	{
		auto it = _graphics->find(String::nullTerminatedView(name));
		if (it == _graphics->end()) {
			return;
		}

		if (frame < 0) {
			frame = it->second.FrameOffset + ((int)(AnimTime * it->second.FrameCount / it->second.FrameDuration) % it->second.FrameCount);
		}

		GenericGraphicResource* base = it->second.Base;
		Vector2f size = Vector2f(base->FrameDimensions.X * scaleX, base->FrameDimensions.Y * scaleY);
		Vector2i viewSize = _levelHandler->GetViewSize();
		Vector2f adjustedPos = ApplyAlignment(alignment, Vector2f(x - viewSize.X * 0.5f, viewSize.Y * 0.5f - y), size);

		Vector2i texSize = base->TextureDiffuse->size();
		int col = frame % base->FrameConfiguration.X;
		int row = frame / base->FrameConfiguration.X;
		Vector4f texCoords = Vector4f(
			float(base->FrameDimensions.X) / float(texSize.X),
			float(base->FrameDimensions.X * col) / float(texSize.X),
			float(base->FrameDimensions.Y) / float(texSize.Y),
			float(base->FrameDimensions.Y * row) / float(texSize.Y)
		);

		texCoords.W += texCoords.Z;
		texCoords.Z *= -1;

		DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color);
	}

	StringView HUD::GetCurrentWeapon(Actors::Player* player, WeaponType weapon, Vector2f& offset)
	{
		if (weapon == WeaponType::Toaster && player->_inWater) {
			offset.X += 2;
			offset.Y += 2;
			return "WeaponToasterDisabled"_s;
		} else if (weapon == WeaponType::Seeker) {
			offset.X += 2;
		} else if (weapon == WeaponType::TNT) {
			offset.X += 2;
		} else if (weapon == WeaponType::Electro) {
			offset.X += 6;
		}

		if ((player->_weaponUpgrades[(int)weapon] & 0x01) != 0) {
			switch (weapon) {
				default:
				case WeaponType::Blaster:
					if (player->_playerType == PlayerType::Spaz) {
						return "WeaponPowerUpBlasterSpaz"_s;
					} else if (player->_playerType == PlayerType::Lori) {
						return "WeaponPowerUpBlasterLori"_s;
					} else {
						return "WeaponPowerUpBlasterJazz"_s;
					}

				case WeaponType::Bouncer: return "WeaponPowerUpBouncer"_s;
				case WeaponType::Freezer: return "WeaponPowerUpFreezer"_s;
				case WeaponType::Seeker: return "WeaponPowerUpSeeker"_s;
				case WeaponType::RF: return "WeaponPowerUpRF"_s;
				case WeaponType::Toaster: return "WeaponPowerUpToaster"_s;
				case WeaponType::TNT: return "WeaponPowerUpTNT"_s;
				case WeaponType::Pepper: return "WeaponPowerUpPepper"_s;
				case WeaponType::Electro: return "WeaponPowerUpElectro"_s;
				case WeaponType::Thunderbolt: return "WeaponPowerUpThunderbolt"_s;
			}
		} else {
			switch (weapon) {
				default:
				case WeaponType::Blaster:
					if (player->_playerType == PlayerType::Spaz) {
						return "WeaponBlasterSpaz"_s;
					} else if (player->_playerType == PlayerType::Lori) {
						return "WeaponBlasterLori"_s;
					} else {
						return "WeaponBlasterJazz"_s;
					}

				case WeaponType::Bouncer: return "WeaponBouncer"_s;
				case WeaponType::Freezer: return "WeaponFreezer"_s;
				case WeaponType::Seeker: return "WeaponSeeker"_s;
				case WeaponType::RF: return "WeaponRF"_s;
				case WeaponType::Toaster: return "WeaponToaster"_s;
				case WeaponType::TNT: return "WeaponTNT"_s;
				case WeaponType::Pepper: return "WeaponPepper"_s;
				case WeaponType::Electro: return "WeaponElectro"_s;
				case WeaponType::Thunderbolt: return "WeaponThunderbolt"_s;
			}
		}
	}

	void HUD::ShowLevelText(const StringView& text)
	{
		if (_levelText == text || text.empty()) {
			return;
		}

		_levelText = text;
		_levelTextTime = 0.0f;
	}

	void HUD::ShowCoins(int count)
	{
		constexpr float StillTime = 120.0f;
		constexpr float TransitionTime = 60.0f;

		_coins = count;

		if (_coinsTime < 0.0f) {
			_coinsTime = 0.0f;
		} else if (_coinsTime > TransitionTime) {
			_coinsTime = TransitionTime;
		}

		if (_gemsTime >= 0.0f) {
			if (_gemsTime <= TransitionTime + StillTime) {
				_gemsTime = TransitionTime + StillTime;
			} else {
				_gemsTime = -1.0f;
			}
		}
	}

	void HUD::ShowGems(int count)
	{
		constexpr float StillTime = 120.0f;
		constexpr float TransitionTime = 60.0f;

		_gems = count;

		if (_gemsTime < 0.0f) {
			_gemsTime = 0.0f;
		} else if (_gemsTime > TransitionTime) {
			_gemsTime = TransitionTime;
		}

		if (_coinsTime >= 0.0f) {
			if (_coinsTime <= TransitionTime + StillTime) {
				_coinsTime = TransitionTime + StillTime;
			} else {
				_coinsTime = -1.0f;
			}
		}
	}
}