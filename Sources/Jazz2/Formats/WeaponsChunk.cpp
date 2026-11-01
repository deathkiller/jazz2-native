#include "WeaponsChunk.h"
#include "VarUint.h"
#include "../Compatibility/JJ2Anims.h"

#include <IO/MemoryStream.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace Jazz2::Formats
{
	namespace
	{
		constexpr std::uint8_t ShotIds = 64;
		constexpr std::uint8_t ShotPoweredIds = 128;
		constexpr std::uint8_t ShotIdCount = 29;

		bool IsShotPoweredId(std::uint8_t id)
		{
			return (id >= ShotPoweredIds && id < ShotPoweredIds + ShotIdCount);
		}

		void Encode(SmallVectorImpl<std::uint8_t>& data, const void* value, std::size_t size)
		{
			data.append(static_cast<const std::uint8_t*>(value), static_cast<const std::uint8_t*>(value) + size);
		}

		void Encode(SmallVectorImpl<std::uint8_t>& data, bool value)
		{
			data.push_back(value ? 1 : 0);
		}

		void Encode(SmallVectorImpl<std::uint8_t>& data, std::uint8_t value)
		{
			data.push_back(value);
		}

		void Encode(SmallVectorImpl<std::uint8_t>& data, std::uint16_t value)
		{
			std::uint8_t bytes[2] = { std::uint8_t(value), std::uint8_t(value >> 8) };
			Encode(data, bytes, sizeof(bytes));
		}

		void Encode(SmallVectorImpl<std::uint8_t>& data, std::int32_t value)
		{
			std::uint32_t v = std::uint32_t(value);
			std::uint8_t bytes[4] = { std::uint8_t(v), std::uint8_t(v >> 8), std::uint8_t(v >> 16), std::uint8_t(v >> 24) };
			Encode(data, bytes, sizeof(bytes));
		}

		void Encode(SmallVectorImpl<std::uint8_t>& data, float value)
		{
			std::uint32_t v;
			std::memcpy(&v, &value, sizeof(v));
			Encode(data, std::int32_t(v));
		}

		void Encode(SmallVectorImpl<std::uint8_t>& data, const String& value)
		{
			Encode(data, value.data(), value.size());
		}

		void Encode(SmallVectorImpl<std::uint8_t>& data, const SmallVector<std::uint8_t, 0>& value)
		{
			Encode(data, value.data(), value.size());
		}

		void Encode(SmallVectorImpl<std::uint8_t>& data, const WeaponSprite& value)
		{
			WriteVarUint(data, std::uint32_t(value.Metadata.size()));
			Encode(data, value.Metadata);
			WriteVarUint(data, value.AnimState);
			Encode(data, value.Image);
		}

		template<typename T, typename std::enable_if<std::is_enum<T>::value, int>::type = 0>
		void Encode(SmallVectorImpl<std::uint8_t>& data, T value)
		{
			using U = typename std::underlying_type<T>::type;
			if constexpr (sizeof(U) == 1) {
				Encode(data, std::uint8_t(value));
			} else {
				Encode(data, std::int32_t(value));
			}
		}

		bool Decode(ArrayView<const std::uint8_t> bytes, bool& value)
		{
			if (bytes.size() != 1) {
				return false;
			}
			value = (bytes[0] != 0);
			return true;
		}

		bool Decode(ArrayView<const std::uint8_t> bytes, std::uint8_t& value)
		{
			if (bytes.size() != 1) {
				return false;
			}
			value = bytes[0];
			return true;
		}

		bool Decode(ArrayView<const std::uint8_t> bytes, std::uint16_t& value)
		{
			if (bytes.size() != 2) {
				return false;
			}
			value = std::uint16_t(bytes[0] | (bytes[1] << 8));
			return true;
		}

		bool Decode(ArrayView<const std::uint8_t> bytes, std::int32_t& value)
		{
			if (bytes.size() != 4) {
				return false;
			}
			value = std::int32_t(std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24));
			return true;
		}

		bool Decode(ArrayView<const std::uint8_t> bytes, float& value)
		{
			std::int32_t v;
			if (!Decode(bytes, v)) {
				return false;
			}
			float f;
			std::memcpy(&f, &v, sizeof(f));
			if (!std::isfinite(f)) {
				return false;
			}
			value = f;
			return true;
		}

		bool Decode(ArrayView<const std::uint8_t> bytes, String& value)
		{
			value = String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			return true;
		}

		bool Decode(ArrayView<const std::uint8_t> bytes, SmallVector<std::uint8_t, 0>& value)
		{
			value.assign(bytes.begin(), bytes.end());
			return true;
		}

		bool Decode(ArrayView<const std::uint8_t> bytes, WeaponSprite& value)
		{
			const std::uint8_t* p = bytes.data();
			const std::uint8_t* end = p + bytes.size();
			std::uint32_t length, animState;
			if (!ReadVarUint(p, end, length) || std::uint32_t(end - p) < length) {
				return false;
			}
			String metadata = String(reinterpret_cast<const char*>(p), length);
			p += length;
			std::int32_t image;
			if (!ReadVarUint(p, end, animState) || !Decode(ArrayView<const std::uint8_t>(p, std::size_t(end - p)), image)) {
				return false;
			}
			value.Metadata = Death::move(metadata);
			value.AnimState = animState;
			value.Image = image;
			return true;
		}

		template<typename T, typename std::enable_if<std::is_enum<T>::value, int>::type = 0>
		bool Decode(ArrayView<const std::uint8_t> bytes, T& value)
		{
			using U = typename std::underlying_type<T>::type;
			if constexpr (sizeof(U) == 1) {
				std::uint8_t v;
				if (!Decode(bytes, v)) {
					return false;
				}
				value = T(v);
			} else {
				std::int32_t v;
				if (!Decode(bytes, v)) {
					return false;
				}
				value = T(v);
			}
			return true;
		}

		// Calls f(id, field, baselineField) for each property of a block
		template<typename TOptions, typename F>
		void VisitOptions(TOptions& o, const WeaponOptions& b, F&& f)
		{
			f(16, o.Infinite, b.Infinite);
			f(17, o.Replenishes, b.Replenishes);
			f(18, o.ReplacedByShield, b.ReplacedByShield);
			f(19, o.ReplacedByBubbles, b.ReplacedByBubbles);
			f(20, o.ComesFromGunCrates, b.ComesFromGunCrates);
			f(21, o.GradualAim, b.GradualAim);
			f(22, o.Multiplier, b.Multiplier);
			f(23, o.Maximum, b.Maximum);
			f(24, o.GemsLost, b.GemsLost);
			f(25, o.GemsLostPowerup, b.GemsLostPowerup);
			f(26, o.Style, b.Style);
			f(27, o.Spread, b.Spread);
			f(28, o.DefaultSample, b.DefaultSample);
			f(29, o.Allowed, b.Allowed);
			f(30, o.AllowedPowerup, b.AllowedPowerup);
			f(31, o.ComesFromBirds, b.ComesFromBirds);
			f(32, o.ComesFromBirdsPowerup, b.ComesFromBirdsPowerup);
		}

		template<typename TFire, typename F>
		void VisitFire(TFire& o, const WeaponFireOptions& b, F&& f)
		{
			f(40, o.Cooldown, b.Cooldown);
			f(41, o.FastFireStep, b.FastFireStep);
			f(42, o.AmmoPerShot, b.AmmoPerShot);
			f(43, o.ShotsPerFire, b.ShotsPerFire);
			f(44, o.SpreadAngle, b.SpreadAngle);
			f(45, o.RandomSpread, b.RandomSpread);
			f(46, o.AllowedInWater, b.AllowedInWater);
			f(47, o.Recoil, b.Recoil);
			f(48, o.Sound, b.Sound);
			f(49, o.LoopSound, b.LoopSound);
			f(50, o.Flare, b.Flare);
			f(51, o.StandStill, b.StandStill);
			f(52, o.SoundIndex, b.SoundIndex);
		}

		template<typename TShot, typename F>
		void VisitShot(TShot& o, const WeaponShotOptions& b, std::uint8_t first, F&& f)
		{
			f(first + 0, o.Sprite, b.Sprite);
			f(first + 1, o.Speed, b.Speed);
			f(first + 2, o.InheritPlayerSpeed, b.InheritPlayerSpeed);
			f(first + 3, o.Gravity, b.Gravity);
			f(first + 4, o.Lifetime, b.Lifetime);
			f(first + 5, o.Strength, b.Strength);
			f(first + 6, o.Pierce, b.Pierce);
			f(first + 7, o.Bounces, b.Bounces);
			f(first + 8, o.Elasticity, b.Elasticity);
			f(first + 9, o.PassesWalls, b.PassesWalls);
			f(first + 10, o.HomingTurnRate, b.HomingTurnRate);
			f(first + 11, o.HomingRange, b.HomingRange);
			f(first + 12, o.FreezeDuration, b.FreezeDuration);
			f(first + 13, o.Thaws, b.Thaws);
			f(first + 14, o.DestroysBlocksAs, b.DestroysBlocksAs);
			f(first + 15, o.Trigger, b.Trigger);
			f(first + 16, o.TriggerRadius, b.TriggerRadius);
			f(first + 17, o.AreaRadius, b.AreaRadius);
			f(first + 18, o.AreaStrength, b.AreaStrength);
			f(first + 19, o.Knockback, b.Knockback);
			f(first + 20, o.Explosion, b.Explosion);
			f(first + 21, o.LightRadius, b.LightRadius);
			f(first + 22, o.LightIntensity, b.LightIntensity);
			f(first + 23, o.RotatesWithSpeed, b.RotatesWithSpeed);
			f(first + 24, o.Look, b.Look);
			f(first + 25, o.AltSprite, b.AltSprite);
			f(first + 26, o.FlipX, b.FlipX);
			f(first + 27, o.FlipY, b.FlipY);
			f(first + 28, o.Acceleration, b.Acceleration);
		}

		// The properties of a custom weapon, besides its powered-up shot, see WeaponDefinition::GetCustom()
		template<typename TCustom, typename F>
		void VisitCustom(TCustom& o, const CustomWeaponOptions& b, F&& f)
		{
			f(5, o.BasedOn, b.BasedOn);
			f(9, o.HudIcon, b.HudIcon);
			f(10, o.HudIconPowered, b.HudIconPowered);
			f(11, o.Pickup, b.Pickup);
			VisitFire(o.Fire, b.Fire, f);
			VisitShot(o.Shot, b.Shot, ShotIds, f);
		}

		template<typename TWeapon, typename F>
		void VisitWeapon(TWeapon& o, const WeaponDefinition& b, F&& f)
		{
			f(1, o.Kind, b.Kind);
			f(2, o.Name, b.Name);
			f(3, o.StartAmmo, b.StartAmmo);
			f(4, o.AmmoCrateEvent, b.AmmoCrateEvent);
			f(8, o.ScriptParams, b.ScriptParams);
			VisitOptions(o.Options, b.Options, f);
		}

		// What only the editor needs, see WeaponEditorData
		template<typename TEditorData, typename F>
		void VisitEditorData(TEditorData& o, const WeaponEditorData& b, F&& f)
		{
			f(6, o.ScriptLibrary, b.ScriptLibrary);
			f(7, o.ScriptConstructor, b.ScriptConstructor);
		}

		bool IsEditorId(std::uint8_t id)
		{
			return (id == 6 || id == 7);
		}

		bool IsCustomId(std::uint8_t id)
		{
			return (id == 5 || (id >= 9 && id <= 11) || (id >= 40 && id <= 52) || (id >= ShotIds && id < ShotIds + ShotIdCount));
		}

		bool IsKnownId(std::uint8_t id)
		{
			return ((id >= 1 && id <= 11) || (id >= 16 && id <= 32) || (id >= 40 && id <= 52) ||
				(id >= ShotIds && id < ShotIds + ShotIdCount) || IsShotPoweredId(id));
		}

		struct Property
		{
			std::uint8_t Id;
			ArrayView<const std::uint8_t> Value;
			ArrayView<const std::uint8_t> Whole;
		};

		bool ReadProperties(const std::uint8_t* p, const std::uint8_t* end, SmallVectorImpl<Property>& properties)
		{
			while (p < end) {
				const std::uint8_t* start = p;
				std::uint8_t id = *p++;
				std::uint32_t size;
				if (!ReadVarUint(p, end, size) || std::uint32_t(end - p) < size) {
					return false;
				}
				properties.push_back({ id, ArrayView<const std::uint8_t>(p, size), ArrayView<const std::uint8_t>(start, std::size_t(p + size - start)) });
				p += size;
			}
			return true;
		}

		// The limits take the type of the value, an int32_t isn't an int everywhere (long on PSP)
		template<typename T>
		T Clamped(T value, typename std::common_type<T>::type min, typename std::common_type<T>::type max)
		{
			return std::clamp<T>(value, min, max);
		}

		void Sanitize(WeaponShotOptions& shot, std::int32_t imageCount)
		{
			if (shot.Sprite.Image >= imageCount || shot.Sprite.Image < -1) {
				shot.Sprite.Image = -1;
			}
			if (shot.AltSprite.Image >= imageCount || shot.AltSprite.Image < -1) {
				shot.AltSprite.Image = -1;
			}
			if (shot.FlipX > ShotFlip::Random) {
				shot.FlipX = ShotFlip::Normal;
			}
			if (shot.FlipY > ShotFlip::Random) {
				shot.FlipY = ShotFlip::Normal;
			}
			shot.Speed = Clamped(shot.Speed, -64.0f, 64.0f);
			shot.Gravity = Clamped(shot.Gravity, -4.0f, 4.0f);
			shot.Acceleration = Clamped(shot.Acceleration, -4.0f, 4.0f);
			shot.Lifetime = Clamped(shot.Lifetime, 1.0f, 3600.0f);
			shot.Strength = Clamped(shot.Strength, 0, 1000);
			shot.Pierce = Clamped(shot.Pierce, 0, 255);
			shot.Bounces = Clamped(shot.Bounces, 0, 255);
			shot.Elasticity = Clamped(shot.Elasticity, 0.0f, 2.0f);
			shot.HomingTurnRate = Clamped(shot.HomingTurnRate, 0.0f, 1.0f);
			shot.HomingRange = Clamped(shot.HomingRange, 0.0f, 1024.0f);
			shot.FreezeDuration = Clamped(shot.FreezeDuration, 0.0f, 3600.0f);
			if (shot.DestroysBlocksAs >= WeaponType::Count) {
				shot.DestroysBlocksAs = WeaponType::Unknown;
			}
			if (shot.Trigger > ShotTrigger::Timer) {
				shot.Trigger = ShotTrigger::Contact;
			}
			if (shot.Look > ShotLook::Beam) {
				shot.Look = ShotLook::Sprite;
			}
			shot.TriggerRadius = Clamped(shot.TriggerRadius, 0.0f, 512.0f);
			shot.AreaRadius = Clamped(shot.AreaRadius, 0.0f, 512.0f);
			shot.AreaStrength = Clamped(shot.AreaStrength, 0, 1000);
			shot.Knockback = Clamped(shot.Knockback, -64.0f, 64.0f);
			shot.Explosion = Clamped(shot.Explosion, -1, 255);
			shot.LightRadius = Clamped(shot.LightRadius, 0.0f, 512.0f);
			shot.LightIntensity = Clamped(shot.LightIntensity, 0.0f, 1.0f);
		}

		void Sanitize(WeaponDefinition& weapon, std::int32_t imageCount, std::int32_t soundCount)
		{
			if (weapon.Kind > WeaponKind::Script) {
				weapon.Kind = WeaponKind::Stock;
			}
			weapon.StartAmmo = Clamped<std::uint16_t>(weapon.StartAmmo, 0, 255);
			weapon.Options.Multiplier = Clamped(weapon.Options.Multiplier, 1, 256);
			weapon.Options.Maximum = Clamped(weapon.Options.Maximum, -1, 255);
			weapon.Options.GemsLost = Clamped(weapon.Options.GemsLost, 0, 255);
			weapon.Options.GemsLostPowerup = Clamped(weapon.Options.GemsLostPowerup, 0, 255);
			if (std::uint8_t(weapon.Options.Style) > std::uint8_t(WeaponStyle::Capped)) {
				weapon.Options.Style = WeaponStyle::Normal;
			}
			if (std::uint32_t(weapon.Options.Spread) > std::uint32_t(WeaponSpread::Toaster)) {
				weapon.Options.Spread = WeaponSpread::Normal;
			}
			if (!weapon.HasCustom()) {
				return;
			}
			CustomWeaponOptions& custom = weapon.GetCustom();
			if (custom.BasedOn >= WeaponType::Count) {
				custom.BasedOn = WeaponType::Unknown;
			}
			for (WeaponSprite* sprite : { &custom.HudIcon, &custom.HudIconPowered, &custom.Pickup }) {
				if (sprite->Image >= imageCount || sprite->Image < -1) {
					sprite->Image = -1;
				}
			}
			custom.Fire.Cooldown = Clamped(custom.Fire.Cooldown, 1.0f, 3600.0f);
			custom.Fire.FastFireStep = Clamped(custom.Fire.FastFireStep, 0.0f, 100.0f);
			custom.Fire.ShotsPerFire = Clamped<std::uint8_t>(custom.Fire.ShotsPerFire, 1, 32);
			custom.Fire.SpreadAngle = Clamped(custom.Fire.SpreadAngle, 0.0f, 6.3f);
			custom.Fire.RandomSpread = Clamped(custom.Fire.RandomSpread, 0.0f, 3.15f);
			custom.Fire.Recoil = Clamped(custom.Fire.Recoil, -32.0f, 32.0f);
			if (custom.Fire.SoundIndex >= soundCount || custom.Fire.SoundIndex < -1) {
				custom.Fire.SoundIndex = -1;
			}
			Sanitize(custom.Shot, imageCount);
			Sanitize(custom.ShotPowered, imageCount);
		}

		bool ReadWeapon(const std::uint8_t* p, const std::uint8_t* end, WeaponDefinition& weapon, WeaponType weaponType, WeaponEditorData* editorData)
		{
			SmallVector<Property, 64> properties;
			if (!ReadProperties(p, end, properties)) {
				return false;
			}

			const WeaponDefinition baseline = WeaponDefinition::MakeDefault(weaponType);
			const CustomWeaponOptions defaults;
			weapon = baseline;
			for (const Property& property : properties) {
				auto decode = [&property](std::uint8_t id, auto& field, const auto&) {
					if (id == property.Id) {
						Decode(property.Value, field);
					}
				};
				if (!IsKnownId(property.Id)) {
					if (editorData != nullptr) {
						editorData->UnknownProperties.append(property.Whole.begin(), property.Whole.end());
					}
				} else if (IsEditorId(property.Id)) {
					if (editorData != nullptr) {
						VisitEditorData(*editorData, WeaponEditorData(), decode);
					}
				} else if (IsCustomId(property.Id)) {
					VisitCustom(weapon.GetCustom(), defaults, decode);
				} else if (!IsShotPoweredId(property.Id)) {
					VisitWeapon(weapon, baseline, decode);
				}
			}

			// Only a custom weapon has the options of one, what the powered-up shot doesn't have is as the shot
			if (weapon.Kind != WeaponKind::Custom) {
				weapon.ResetCustom();
				return true;
			}
			CustomWeaponOptions& custom = weapon.GetCustom();
			custom.ShotPowered = custom.Shot;
			for (const Property& property : properties) {
				if (IsShotPoweredId(property.Id)) {
					VisitShot(custom.ShotPowered, custom.Shot, ShotPoweredIds, [&property](std::uint8_t id, auto& field, const auto&) {
						if (id == property.Id) {
							Decode(property.Value, field);
						}
					});
				}
			}
			return true;
		}

		void WriteWeapon(const WeaponDefinition& weapon, WeaponType weaponType, const WeaponEditorData* editorData, SmallVectorImpl<std::uint8_t>& data)
		{
			SmallVector<std::uint8_t, 64> value;
			auto write = [&data, &value](std::uint8_t id, const auto& field, const auto& baseline) {
				if (field == baseline) {
					return;
				}
				value.clear();
				Encode(value, field);
				data.push_back(id);
				WriteVarUint(data, std::uint32_t(value.size()));
				data.append(value.begin(), value.end());
			};

			const WeaponDefinition baseline = WeaponDefinition::MakeDefault(weaponType);
			VisitWeapon(weapon, baseline, write);
			if (weapon.Kind == WeaponKind::Custom) {
				const CustomWeaponOptions& custom = weapon.GetCustom();
				VisitCustom(custom, CustomWeaponOptions(), write);
				VisitShot(custom.ShotPowered, custom.Shot, ShotPoweredIds, write);
			}
			if (editorData != nullptr) {
				if (weapon.Kind == WeaponKind::Script) {
					VisitEditorData(*editorData, WeaponEditorData(), write);
				}
				data.append(editorData->UnknownProperties.begin(), editorData->UnknownProperties.end());
			}
		}

		bool IsValid(const WeaponImage& image)
		{
			return (image.Width >= 1 && image.Width <= WeaponsChunk::MaxImageSize && image.Height >= 1 && image.Height <= WeaponsChunk::MaxImageSize &&
				image.FrameColumns >= 1 && image.FrameRows >= 1 && image.GetFrameWidth() >= 1 && image.GetFrameWidth() <= WeaponsChunk::MaxFrameSize &&
				image.GetFrameHeight() >= 1 && image.GetFrameHeight() <= WeaponsChunk::MaxFrameSize && image.FrameCount >= 1 &&
				image.FrameCount <= WeaponsChunk::MaxFrameCount && image.FrameCount <= image.FrameColumns * image.FrameRows &&
				image.Pixels.size() == std::size_t(image.Width) * std::size_t(image.Height));
		}

		bool ReadImage(const std::uint8_t* p, const std::uint8_t* end, WeaponImage& image)
		{
			std::uint32_t width, height, columns, rows, count;
			if (!ReadVarUint(p, end, width) || !ReadVarUint(p, end, height) || !ReadVarUint(p, end, columns) || !ReadVarUint(p, end, rows) ||
				!ReadVarUint(p, end, count) || width > std::uint32_t(WeaponsChunk::MaxImageSize) || height > std::uint32_t(WeaponsChunk::MaxImageSize) ||
				columns > width || rows > height || count > std::uint32_t(WeaponsChunk::MaxFrameCount) || end - p < 8) {
				return false;
			}
			image.Width = std::int32_t(width);
			image.Height = std::int32_t(height);
			image.FrameColumns = std::int32_t(columns);
			image.FrameRows = std::int32_t(rows);
			image.FrameCount = std::int32_t(count);
			if (!Decode(ArrayView<const std::uint8_t>(p, 4), image.FrameRate)) {
				image.FrameRate = 10.0f;
			}
			image.FrameRate = std::clamp<float>(image.FrameRate, 0.0f, 120.0f);
			p += 4;
			image.HotspotX = std::int16_t(p[0] | (p[1] << 8));
			image.HotspotY = std::int16_t(p[2] | (p[3] << 8));
			p += 4;
			std::size_t pixelCount = std::size_t(width) * height;
			if (p >= end || pixelCount == 0) {
				return false;
			}
			// The sheet in the image content of the other images of the game, one channel; the decoder writes four bytes a
			// pixel, see JJ2Anims::ReadImageContent()
			image.Pixels.resize_for_overwrite(pixelCount + 3);
			Death::IO::MemoryStream s(p, std::int64_t(end - p));
			Compatibility::JJ2Anims::ReadImageContent(s, image.Pixels.data(), std::int32_t(width), std::int32_t(height), 1);
			image.Pixels.resize(pixelCount);
			return IsValid(image);
		}

		void WriteImage(const WeaponImage& image, SmallVectorImpl<std::uint8_t>& data)
		{
			WriteVarUint(data, std::uint32_t(image.Width));
			WriteVarUint(data, std::uint32_t(image.Height));
			WriteVarUint(data, std::uint32_t(image.FrameColumns));
			WriteVarUint(data, std::uint32_t(image.FrameRows));
			WriteVarUint(data, std::uint32_t(image.FrameCount));
			Encode(data, image.FrameRate);
			Encode(data, std::uint16_t(std::int16_t(image.HotspotX)));
			Encode(data, std::uint16_t(std::int16_t(image.HotspotY)));
			Death::IO::MemoryStream s(std::int64_t(image.Pixels.size() / 2 + 16));
			Compatibility::JJ2Anims::WriteImageContent(s, image.Pixels.data(), image.Width, image.Height, 1);
			Encode(data, s.GetBuffer(), std::size_t(s.GetSize()));
		}
	}

	WeaponsChunk::ReadResult WeaponsChunk::Read(ArrayView<const std::uint8_t> data, WeaponsDescription& weapons, ArrayView<WeaponEditorData> editorData)
	{
		DEATH_DEBUG_ASSERT(editorData.empty() || editorData.size() == std::size_t(WeaponType::Count), "One for each weapon is needed");
		weapons = WeaponsDescription();
		for (WeaponEditorData& item : editorData) {
			item = WeaponEditorData();
		}
		const std::uint8_t* p = data.data();
		const std::uint8_t* end = p + data.size();
		if (end - p < 3 || *p == 0) {
			return ReadResult::Damaged;
		}
		if (*p > Version) {
			return ReadResult::NewerVersion;
		}
		p++;

		WeaponsDescription read;
		read.ScriptHookArgument = std::int16_t(p[0] | (p[1] << 8));
		p += 2;

		std::uint32_t weaponCount;
		if (!ReadVarUint(p, end, weaponCount) || weaponCount > std::uint32_t(WeaponType::Count)) {
			return ReadResult::Damaged;
		}
		bool seen[std::int32_t(WeaponType::Count)] = {};
		WeaponEditorData readEditorData[std::int32_t(WeaponType::Count)];
		for (std::uint32_t i = 0; i < weaponCount; i++) {
			std::uint32_t size;
			if (p >= end) {
				return ReadResult::Damaged;
			}
			std::uint8_t slot = *p++;
			if (!ReadVarUint(p, end, size) || std::uint32_t(end - p) < size) {
				return ReadResult::Damaged;
			}
			// A weapon this version doesn't have is skipped
			if (slot < std::uint8_t(WeaponType::Count)) {
				if (seen[slot] || !ReadWeapon(p, p + size, read.Weapons[slot], WeaponType(slot), editorData.empty() ? nullptr : &readEditorData[slot])) {
					return ReadResult::Damaged;
				}
				seen[slot] = true;
			}
			p += size;
		}

		std::uint32_t imageCount;
		if (!ReadVarUint(p, end, imageCount) || imageCount > MaxImages) {
			return ReadResult::Damaged;
		}
		for (std::uint32_t i = 0; i < imageCount; i++) {
			std::uint32_t size;
			if (!ReadVarUint(p, end, size) || std::uint32_t(end - p) < size) {
				return ReadResult::Damaged;
			}
			WeaponImage& image = read.Images.emplace_back();
			if (!ReadImage(p, p + size, image)) {
				return ReadResult::Damaged;
			}
			p += size;
		}

		// Sounds, a file name and its contents each
		std::uint32_t soundCount;
		if (!ReadVarUint(p, end, soundCount) || soundCount > MaxSounds) {
			return ReadResult::Damaged;
		}
		for (std::uint32_t i = 0; i < soundCount; i++) {
			std::uint32_t size, nameLength;
			if (!ReadVarUint(p, end, size) || std::uint32_t(end - p) < size || size > MaxSoundSize) {
				return ReadResult::Damaged;
			}
			const std::uint8_t* recordEnd = p + size;
			if (!ReadVarUint(p, recordEnd, nameLength) || std::uint32_t(recordEnd - p) < nameLength || nameLength > 255) {
				return ReadResult::Damaged;
			}
			WeaponSound& sound = read.Sounds.emplace_back();
			sound.Name = String(reinterpret_cast<const char*>(p), nameLength);
			p += nameLength;
			sound.Data.assign(p, recordEnd);
			p = recordEnd;
		}

		for (WeaponDefinition& weapon : read.Weapons) {
			Sanitize(weapon, std::int32_t(read.Images.size()), std::int32_t(read.Sounds.size()));
		}
		weapons = Death::move(read);
		for (std::size_t i = 0; i < editorData.size(); i++) {
			editorData[i] = Death::move(readEditorData[i]);
		}
		return ReadResult::Success;
	}

	void WeaponsChunk::Write(const WeaponsDescription& weapons, SmallVectorImpl<std::uint8_t>& data, ArrayView<const WeaponEditorData> editorData)
	{
		DEATH_DEBUG_ASSERT(editorData.empty() || editorData.size() == std::size_t(WeaponType::Count), "One for each weapon is needed");
		data.clear();
		auto isWritten = [&weapons, &editorData](std::int32_t i) {
			return (!weapons.Weapons[i].IsDefault(WeaponType(i)) || (!editorData.empty() && !editorData[i].UnknownProperties.empty()));
		};
		std::uint32_t weaponCount = 0;
		for (std::int32_t i = 0; i < std::int32_t(WeaponType::Count); i++) {
			weaponCount += (isWritten(i) ? 1 : 0);
		}
		if (weaponCount == 0 && weapons.IsDefault()) {
			return;
		}

		data.push_back(Version);
		Encode(data, std::uint16_t(weapons.ScriptHookArgument));
		WriteVarUint(data, weaponCount);
		SmallVector<std::uint8_t, 0> record;
		for (std::int32_t i = 0; i < std::int32_t(WeaponType::Count); i++) {
			if (!isWritten(i)) {
				continue;
			}
			record.clear();
			WriteWeapon(weapons.Weapons[i], WeaponType(i), editorData.empty() ? nullptr : &editorData[i], record);
			data.push_back(std::uint8_t(i));
			WriteVarUint(data, std::uint32_t(record.size()));
			data.append(record.begin(), record.end());
		}

		std::uint32_t imageCount = 0;
		for (const WeaponImage& image : weapons.Images) {
			imageCount += (IsValid(image) && imageCount < MaxImages ? 1 : 0);
		}
		WriteVarUint(data, imageCount);
		std::uint32_t written = 0;
		for (const WeaponImage& image : weapons.Images) {
			if (!IsValid(image) || written >= MaxImages) {
				continue;
			}
			written++;
			record.clear();
			WriteImage(image, record);
			WriteVarUint(data, std::uint32_t(record.size()));
			data.append(record.begin(), record.end());
		}

		std::uint32_t soundCount = std::uint32_t(std::min<std::size_t>(weapons.Sounds.size(), MaxSounds));
		WriteVarUint(data, soundCount);
		for (std::uint32_t i = 0; i < soundCount; i++) {
			const WeaponSound& sound = weapons.Sounds[i];
			StringView name = sound.Name.prefix(std::min<std::size_t>(sound.Name.size(), 255));
			record.clear();
			WriteVarUint(record, std::uint32_t(name.size()));
			Encode(record, name.data(), name.size());
			Encode(record, sound.Data.data(), sound.Data.size());
			WriteVarUint(data, std::uint32_t(record.size()));
			data.append(record.begin(), record.end());
		}
	}
}
