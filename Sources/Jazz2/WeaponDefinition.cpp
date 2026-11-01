#include "WeaponDefinition.h"

#include <Base/Format.h>
#include <Cryptography/xxHash.h>

#include <algorithm>

using namespace Death::Containers::Literals;

namespace Jazz2
{
	WeaponOptions WeaponOptions::MakeDefault(WeaponType weaponType)
	{
		// As JJ2+ starts a level, but gun crates and gems lost as the game always had them
		WeaponOptions options = {};
		options.Infinite = (weaponType == WeaponType::Blaster);
		options.ReplacedByShield = (weaponType == WeaponType::Blaster);
		options.ComesFromGunCrates = (weaponType >= WeaponType::Bouncer && weaponType < WeaponType::Count);
		options.Multiplier = 1;
		options.Maximum = -1;
		options.GemsLost = 3;
		options.GemsLostPowerup = 3;
		options.Style = WeaponStyle::Normal;
		options.Spread = WeaponSpread::Normal;
		options.DefaultSample = true;
		options.Allowed = true;
		options.AllowedPowerup = true;
		switch (weaponType) {
			case WeaponType::Freezer: options.Spread = WeaponSpread::NormalOrDirectionAndAim; break;
			case WeaponType::Seeker: options.Style = WeaponStyle::Missile; break;
			case WeaponType::RF: options.Style = WeaponStyle::Missile; options.Spread = WeaponSpread::DoubleOrTriple; break;
			case WeaponType::Toaster: options.Style = WeaponStyle::Popcorn; options.Spread = WeaponSpread::Toaster; break;
			case WeaponType::TNT: options.Style = WeaponStyle::Capped; break;
			case WeaponType::Pepper: options.Spread = WeaponSpread::NormalOrPepper; break;
			default: break;
		}
		return options;
	}

	std::int32_t WeaponOptions::GetMaximumAmmo() const
	{
		// The game has always given 99, in multiplayer too, where JJ2+ gives 50
		if (Maximum <= 0) {
			return 99;
		}
		return std::min<std::int32_t>(Maximum, 255);
	}

	bool WeaponOptions::operator==(const WeaponOptions& other) const
	{
		return (Infinite == other.Infinite && Replenishes == other.Replenishes && ReplacedByShield == other.ReplacedByShield &&
			ReplacedByBubbles == other.ReplacedByBubbles && ComesFromGunCrates == other.ComesFromGunCrates &&
			GradualAim == other.GradualAim && Multiplier == other.Multiplier && Maximum == other.Maximum &&
			GemsLost == other.GemsLost && GemsLostPowerup == other.GemsLostPowerup && Style == other.Style &&
			Spread == other.Spread && DefaultSample == other.DefaultSample && Allowed == other.Allowed &&
			AllowedPowerup == other.AllowedPowerup && ComesFromBirds == other.ComesFromBirds &&
			ComesFromBirdsPowerup == other.ComesFromBirdsPowerup);
	}

	bool WeaponSprite::operator==(const WeaponSprite& other) const
	{
		return (Metadata == other.Metadata && AnimState == other.AnimState && Image == other.Image);
	}

	bool WeaponFireOptions::operator==(const WeaponFireOptions& other) const
	{
		return (Cooldown == other.Cooldown && FastFireStep == other.FastFireStep && AmmoPerShot == other.AmmoPerShot &&
			ShotsPerFire == other.ShotsPerFire && SpreadAngle == other.SpreadAngle && RandomSpread == other.RandomSpread &&
			AllowedInWater == other.AllowedInWater && StandStill == other.StandStill && Recoil == other.Recoil && Sound == other.Sound &&
			SoundIndex == other.SoundIndex && LoopSound == other.LoopSound && Flare == other.Flare);
	}

	bool WeaponShotOptions::operator==(const WeaponShotOptions& other) const
	{
		return (Sprite == other.Sprite && AltSprite == other.AltSprite && FlipX == other.FlipX && FlipY == other.FlipY && Speed == other.Speed &&
			Acceleration == other.Acceleration && InheritPlayerSpeed == other.InheritPlayerSpeed &&
			Gravity == other.Gravity && Lifetime == other.Lifetime && Strength == other.Strength && Pierce == other.Pierce &&
			Bounces == other.Bounces && Elasticity == other.Elasticity && PassesWalls == other.PassesWalls &&
			HomingTurnRate == other.HomingTurnRate && HomingRange == other.HomingRange &&
			FreezeDuration == other.FreezeDuration && Thaws == other.Thaws && DestroysBlocksAs == other.DestroysBlocksAs &&
			Trigger == other.Trigger && TriggerRadius == other.TriggerRadius && AreaRadius == other.AreaRadius &&
			AreaStrength == other.AreaStrength && Knockback == other.Knockback && Explosion == other.Explosion &&
			LightRadius == other.LightRadius && LightIntensity == other.LightIntensity &&
			RotatesWithSpeed == other.RotatesWithSpeed && Look == other.Look);
	}

	bool CustomWeaponOptions::operator==(const CustomWeaponOptions& other) const
	{
		return (Fire == other.Fire && Shot == other.Shot && ShotPowered == other.ShotPowered && HudIcon == other.HudIcon &&
			HudIconPowered == other.HudIconPowered && Pickup == other.Pickup && BasedOn == other.BasedOn);
	}

	WeaponDefinition::WeaponDefinition(const WeaponDefinition& other)
	{
		*this = other;
	}

	WeaponDefinition& WeaponDefinition::operator=(const WeaponDefinition& other)
	{
		if (this == &other) {
			return *this;
		}
		Name = other.Name;
		ScriptParams = other.ScriptParams;
		Options = other.Options;
		StartAmmo = other.StartAmmo;
		Kind = other.Kind;
		AmmoCrateEvent = other.AmmoCrateEvent;
		_custom = (other._custom != nullptr ? std::make_unique<CustomWeaponOptions>(*other._custom) : nullptr);
		return *this;
	}

	const CustomWeaponOptions& WeaponDefinition::GetCustom() const
	{
		static const CustomWeaponOptions Defaults;
		return (_custom != nullptr ? *_custom : Defaults);
	}

	CustomWeaponOptions& WeaponDefinition::GetCustom()
	{
		if (_custom == nullptr) {
			_custom = std::make_unique<CustomWeaponOptions>();
		}
		return *_custom;
	}

	WeaponDefinition WeaponDefinition::MakeDefault(WeaponType weaponType)
	{
		WeaponDefinition definition;
		definition.Options = WeaponOptions::MakeDefault(weaponType);
		return definition;
	}

	WeaponDefinition WeaponDefinition::MakeCustom(WeaponType weaponType, WeaponType basedOn)
	{
		// Explosion::Type values
		constexpr std::int32_t Small = 3, SmallDark = 4, Large = 5, SmokeWhite = 8, Pepper = 11, RF = 12, RFUpgraded = 13;

		WeaponDefinition definition = MakeDefault(weaponType);
		definition.Kind = WeaponKind::Custom;

		CustomWeaponOptions& custom = definition.GetCustom();
		custom.BasedOn = basedOn;
		WeaponFireOptions& fire = custom.Fire;
		WeaponShotOptions& shot = custom.Shot;
		WeaponShotOptions& powered = custom.ShotPowered;
		shot.LightRadius = 20.0f;
		switch (basedOn) {
			default:
			case WeaponType::Blaster:
				shot.Sprite.Metadata = "Weapon/Blaster"_s;
				shot.Speed = 12.0f;
				shot.Lifetime = 22.0f;
				shot.Explosion = Small;
				fire.Sound = "WeaponBlaster"_s;
				powered = shot;
				powered.Lifetime = 25.0f;
				powered.Strength = 2;
				break;
			case WeaponType::Bouncer:
				shot.Sprite.Metadata = "Weapon/Bouncer"_s;
				shot.Speed = 10.0f;
				shot.Gravity = 0.3f;
				shot.Lifetime = 90.0f;
				shot.Bounces = 32;
				shot.Elasticity = 1.0f;
				shot.Explosion = SmallDark;
				fire.Sound = "Fire"_s;
				powered = shot;
				powered.Lifetime = 130.0f;
				powered.Strength = 2;
				break;
			case WeaponType::Freezer:
				shot.Sprite.Metadata = "Weapon/Freezer"_s;
				shot.Speed = 6.0f;
				shot.Lifetime = 44.0f;
				shot.Strength = 0;
				shot.FreezeDuration = 200.0f * 60.0f / 70.0f;
				shot.Explosion = SmokeWhite;
				fire.Sound = "Fire"_s;
				powered = shot;
				powered.Speed = 8.0f;
				powered.Lifetime = 38.0f;
				powered.FreezeDuration = 280.0f;
				break;
			case WeaponType::Seeker:
				shot.Sprite.Metadata = "Weapon/Seeker"_s;
				shot.Speed = 5.0f;
				shot.InheritPlayerSpeed = false;
				shot.Lifetime = 144.0f;
				shot.Strength = 2;
				shot.HomingTurnRate = 0.08f;
				shot.HomingRange = 300.0f;
				shot.Explosion = Large;
				fire.Cooldown = 120.0f;
				fire.FastFireStep = 0.0f;
				fire.Sound = "Fire"_s;
				powered = shot;
				powered.Lifetime = 188.0f;
				powered.Strength = 3;
				break;
			case WeaponType::RF:
				shot.Sprite.Metadata = "Weapon/RF"_s;
				shot.Speed = 6.0f;
				shot.Lifetime = 30.0f;
				shot.Strength = 2;
				shot.AreaRadius = 36.0f;
				shot.AreaStrength = 1;
				shot.Knockback = 4.0f;
				shot.Explosion = RF;
				fire.Cooldown = 120.0f;
				fire.FastFireStep = 0.0f;
				fire.ShotsPerFire = 2;
				fire.SpreadAngle = 0.52f;
				fire.Sound = "Fire"_s;
				powered = shot;
				powered.Speed = 6.4f;
				powered.Lifetime = 24.0f;
				powered.Explosion = RFUpgraded;
				break;
			case WeaponType::Toaster:
				shot.Sprite.Metadata = "Weapon/Toaster"_s;
				shot.Speed = 1.3f;
				shot.Lifetime = 60.0f;
				shot.Pierce = 255;
				shot.Thaws = true;
				shot.RotatesWithSpeed = false;
				fire.Cooldown = 6.0f;
				fire.FastFireStep = 0.0f;
				fire.AmmoPerShot = 50;
				fire.RandomSpread = 0.1f;
				fire.AllowedInWater = false;
				fire.Sound = "WeaponToaster"_s;
				fire.LoopSound = true;
				fire.Flare = false;
				powered = shot;
				powered.Lifetime = 80.0f;
				break;
			case WeaponType::TNT:
				shot.Sprite.Metadata = "Weapon/TNT"_s;
				shot.Speed = 0.0f;
				shot.InheritPlayerSpeed = false;
				shot.Lifetime = 200.0f;
				shot.Strength = 0;
				shot.Trigger = ShotTrigger::Proximity;
				shot.TriggerRadius = 64.0f;
				shot.AreaRadius = 96.0f;
				shot.AreaStrength = 8;
				shot.Explosion = Large;
				shot.LightRadius = 0.0f;
				shot.RotatesWithSpeed = false;
				fire.Cooldown = 60.0f;
				fire.FastFireStep = 0.0f;
				fire.Flare = false;
				definition.Options.Style = WeaponStyle::Capped;
				powered = shot;
				break;
			case WeaponType::Pepper:
				shot.Sprite.Metadata = "Weapon/Pepper"_s;
				shot.Speed = 5.0f;
				shot.Lifetime = 32.0f;
				shot.Explosion = Pepper;
				shot.Look = ShotLook::Glow;
				fire.ShotsPerFire = 2;
				fire.RandomSpread = 0.2f;
				fire.Sound = "Fire"_s;
				powered = shot;
				powered.Speed = 6.0f;
				powered.Lifetime = 36.0f;
				break;
			case WeaponType::Electro:
				// The sparks, the sprite of the shot itself is only its mask
				shot.Sprite.Metadata = "Weapon/Electro"_s;
				shot.Sprite.AnimState = 1;
				shot.Speed = 4.0f;
				shot.Lifetime = 55.0f;
				shot.Strength = 4;
				shot.PassesWalls = true;
				shot.LightRadius = 40.0f;
				shot.Look = ShotLook::Electro;
				fire.Sound = "Fire"_s;
				powered = shot;
				powered.Speed = 5.0f;
				break;
			case WeaponType::Thunderbolt:
				// Either of two bolts, flipped upside down or not
				shot.Sprite.Metadata = "Weapon/Thunderbolt"_s;
				shot.AltSprite.Metadata = "Weapon/Thunderbolt"_s;
				shot.AltSprite.AnimState = 1;
				shot.FlipY = ShotFlip::Random;
				shot.Speed = 14.0f;
				shot.Lifetime = 10.0f;
				shot.Strength = 2;
				shot.Pierce = 255;
				shot.PassesWalls = true;
				shot.LightRadius = 60.0f;
				shot.Look = ShotLook::Beam;
				fire.Cooldown = 12.0f;
				fire.FastFireStep = 0.0f;
				fire.AmmoPerShot = 80;
				fire.RandomSpread = 0.16f;
				fire.AllowedInWater = false;
				fire.StandStill = true;
				fire.Recoil = 2.0f;
				fire.Sound = "WeaponThunderbolt"_s;
				fire.LoopSound = true;
				fire.Flare = false;
				powered = shot;
				powered.Strength = 3;
				break;
		}
		if (powered.Sprite.Image < 0 && !powered.Sprite.Metadata.empty() && shot.Look != ShotLook::Electro && shot.Look != ShotLook::Beam) {
			powered.Sprite.AnimState = 1;
		}
		return definition;
	}

	bool WeaponDefinition::IsDefault(WeaponType weaponType) const
	{
		return (*this == MakeDefault(weaponType));
	}

	bool WeaponDefinition::operator==(const WeaponDefinition& other) const
	{
		return (Kind == other.Kind && Name == other.Name && Options == other.Options && StartAmmo == other.StartAmmo &&
			AmmoCrateEvent == other.AmmoCrateEvent && GetCustom() == other.GetCustom() &&
			ScriptParams.size() == other.ScriptParams.size() && std::equal(ScriptParams.begin(), ScriptParams.end(), other.ScriptParams.begin()));
	}

	bool WeaponEditorData::operator==(const WeaponEditorData& other) const
	{
		return (ScriptLibrary == other.ScriptLibrary && ScriptConstructor == other.ScriptConstructor &&
			UnknownProperties.size() == other.UnknownProperties.size() &&
			std::equal(UnknownProperties.begin(), UnknownProperties.end(), other.UnknownProperties.begin()));
	}

	bool WeaponSound::operator==(const WeaponSound& other) const
	{
		return (Name == other.Name && Data.size() == other.Data.size() && std::equal(Data.begin(), Data.end(), other.Data.begin()));
	}

	bool WeaponImage::operator==(const WeaponImage& other) const
	{
		return (Width == other.Width && Height == other.Height && FrameColumns == other.FrameColumns && FrameRows == other.FrameRows &&
			FrameCount == other.FrameCount && FrameRate == other.FrameRate && HotspotX == other.HotspotX && HotspotY == other.HotspotY &&
			Pixels.size() == other.Pixels.size() && std::equal(Pixels.begin(), Pixels.end(), other.Pixels.begin()));
	}

	WeaponsDescription::WeaponsDescription()
	{
		for (std::int32_t i = 0; i < std::int32_t(WeaponType::Count); i++) {
			Weapons[i] = WeaponDefinition::MakeDefault(WeaponType(i));
		}
	}

	String WeaponsDescription::GetImageMetadataPath(const WeaponImage& image)
	{
		const std::int32_t header[8] = { image.Width, image.Height, image.FrameColumns, image.FrameRows, image.FrameCount,
			std::int32_t(image.FrameRate * 1000.0f), image.HotspotX, image.HotspotY };
		std::uint64_t hash = Death::Cryptography::xxHash3(reinterpret_cast<const char*>(image.Pixels.data()), image.Pixels.size());
		hash ^= Death::Cryptography::xxHash3(reinterpret_cast<const char*>(header), sizeof(header)) * 0x9E3779B97F4A7C15ull;
		return Death::format("Level/WeaponImage/{:.16x}", hash);
	}

	String WeaponsDescription::GetSpriteMetadataPath(const WeaponSprite& sprite) const
	{
		if (sprite.Image >= 0) {
			return (sprite.Image < std::int32_t(Images.size()) ? GetImageMetadataPath(Images[sprite.Image]) : String());
		}
		return sprite.Metadata;
	}

	String WeaponsDescription::GetSoundIdentifier(const WeaponSound& sound)
	{
		std::uint64_t hash = Death::Cryptography::xxHash3(reinterpret_cast<const char*>(sound.Data.data()), sound.Data.size());
		return Death::format("Level/WeaponSound/{:.16x}", hash);
	}

	String WeaponsDescription::GetFireSoundIdentifier(const WeaponFireOptions& fire) const
	{
		if (fire.SoundIndex >= 0) {
			return (fire.SoundIndex < std::int32_t(Sounds.size()) ? GetSoundIdentifier(Sounds[fire.SoundIndex]) : String());
		}
		return fire.Sound;
	}

	bool WeaponsDescription::IsDefault() const
	{
		if (ScriptHookArgument != -1 || !Images.empty() || !Sounds.empty()) {
			return false;
		}
		for (std::int32_t i = 0; i < std::int32_t(WeaponType::Count); i++) {
			if (!Weapons[i].IsDefault(WeaponType(i))) {
				return false;
			}
		}
		return true;
	}
}
