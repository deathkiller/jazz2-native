#include "WeaponTemplates.h"

#if defined(WITH_EDITOR)

#include "../../Utils/JsonAccess.h"

#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cmath>
#include <type_traits>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr float DegreesPerRadian = 57.29578f;

		constexpr StringView WeaponTypeNames[] = { "Blaster"_s, "Bouncer"_s, "Freezer"_s, "Seeker"_s, "RF"_s, "Toaster"_s, "TNT"_s,
			"Pepper"_s, "Electro"_s, "Thunderbolt"_s };
		constexpr StringView StyleNames[] = { "Normal"_s, "Missile"_s, "Popcorn"_s, "Capped"_s };
		constexpr StringView SpreadNames[] = { "Normal"_s, "Double"_s, "Triple"_s, "DoubleOrTriple"_s, "DirectionAndAim"_s,
			"NormalOrDirectionAndAim"_s, "Pepper"_s, "NormalOrPepper"_s, "Toaster"_s };
		constexpr StringView TriggerNames[] = { "Contact"_s, "Proximity"_s, "Timer"_s };
		constexpr StringView LookNames[] = { "Sprite"_s, "Glow"_s, "Electro"_s, "Beam"_s };
		constexpr StringView FlipNames[] = { "Normal"_s, "Flipped"_s, "Random"_s };
		// The blocks a shot breaks are of the weapon it's fired from
		constexpr StringView OwnWeaponName = "Own"_s;

		ArrayView<const StringView> GetEnumNames(WeaponType) { return WeaponTypeNames; }
		ArrayView<const StringView> GetEnumNames(WeaponStyle) { return StyleNames; }
		ArrayView<const StringView> GetEnumNames(WeaponSpread) { return SpreadNames; }
		ArrayView<const StringView> GetEnumNames(ShotTrigger) { return TriggerNames; }
		ArrayView<const StringView> GetEnumNames(ShotLook) { return LookNames; }
		ArrayView<const StringView> GetEnumNames(ShotFlip) { return FlipNames; }

		template<class TOptions, class F>
		void VisitOptions(TOptions& o, F& f)
		{
			f("Allowed", o.Allowed);
			f("AllowedPowerup", o.AllowedPowerup);
			f("Maximum", o.Maximum);
			f("Multiplier", o.Multiplier);
			f("Infinite", o.Infinite);
			f("Replenishes", o.Replenishes);
			f("ComesFromGunCrates", o.ComesFromGunCrates);
			f("ComesFromBirds", o.ComesFromBirds);
			f("ComesFromBirdsPowerup", o.ComesFromBirdsPowerup);
			f("ReplacedByShield", o.ReplacedByShield);
			f("ReplacedByBubbles", o.ReplacedByBubbles);
			f("DefaultSample", o.DefaultSample);
			f("GemsLost", o.GemsLost);
			f("GemsLostPowerup", o.GemsLostPowerup);
			f("Style", o.Style);
			f("Spread", o.Spread);
			f("GradualAim", o.GradualAim);
		}

		// The embedded sound of firing is "SoundFile", see ApplyCustomWeaponTemplate()
		template<class TFire, class F>
		void VisitFire(TFire& o, F& f)
		{
			f("Cooldown", o.Cooldown);
			f("FastFireStep", o.FastFireStep);
			f.Ammo("AmmoPerShot", o.AmmoPerShot);
			f("ShotsPerFire", o.ShotsPerFire);
			f.Angle("SpreadAngle", o.SpreadAngle);
			f.Angle("RandomSpread", o.RandomSpread);
			f("Recoil", o.Recoil);
			f("AllowedInWater", o.AllowedInWater);
			f("StandStill", o.StandStill);
			f("Flare", o.Flare);
			f("Sound", o.Sound);
			f("LoopSound", o.LoopSound);
		}

		template<class TShot, class F>
		void VisitShot(TShot& o, F& f)
		{
			f("Sprite", o.Sprite);
			f("AltSprite", o.AltSprite);
			f("Look", o.Look);
			f("FlipX", o.FlipX);
			f("FlipY", o.FlipY);
			f("RotatesWithSpeed", o.RotatesWithSpeed);
			f("Speed", o.Speed);
			f("Acceleration", o.Acceleration);
			f("InheritPlayerSpeed", o.InheritPlayerSpeed);
			f("Gravity", o.Gravity);
			f("Lifetime", o.Lifetime);
			f("Strength", o.Strength);
			f("Pierce", o.Pierce);
			f("Bounces", o.Bounces);
			f("Elasticity", o.Elasticity);
			f("PassesWalls", o.PassesWalls);
			f.Angle("HomingTurnRate", o.HomingTurnRate);
			f("HomingRange", o.HomingRange);
			f("FreezeDuration", o.FreezeDuration);
			f("Thaws", o.Thaws);
			f("DestroysBlocksAs", o.DestroysBlocksAs);
			f("Trigger", o.Trigger);
			f("TriggerRadius", o.TriggerRadius);
			f("AreaRadius", o.AreaRadius);
			f("AreaStrength", o.AreaStrength);
			f("Knockback", o.Knockback);
			f("Explosion", o.Explosion);
			f("LightRadius", o.LightRadius);
			f("LightIntensity", o.LightIntensity);
		}

		template<class T>
		bool ParseEnum(const Json::Value& value, T& result)
		{
			using namespace Utils::JsonAccess;

			ArrayView<const StringView> names = GetEnumNames(T());
			if (value.isString()) {
				String name = ToString(value);
				for (std::size_t i = 0; i < names.size(); i++) {
					if (StringUtils::equalsIgnoreCase(names[i], name)) {
						result = T(i);
						return true;
					}
				}
				return false;
			}
			std::int32_t number = ToInt(value, -1);
			if (number < 0 || number >= std::int32_t(names.size())) {
				return false;
			}
			result = T(number);
			return true;
		}

		Json::Value ToJson(const String& text)
		{
			return Json::Value(text.data(), text.data() + text.size());
		}

		Json::Value ToJson(StringView text)
		{
			return Json::Value(text.data(), text.data() + text.size());
		}

		struct TemplateReader
		{
			const Json::Value& Object;
			StringView Directory;
			Function<std::int32_t(const WeaponTemplateImage&)>& AddImage;
			bool& Failed;

			template<class T>
			void operator()(const char* name, T& field)
			{
				const Json::Value& value = Utils::JsonAccess::Member(Object, name);
				if (!value.isNull()) {
					Read(value, field);
				}
			}

			void Angle(const char* name, float& field)
			{
				field = Utils::JsonAccess::ToFloat(Utils::JsonAccess::Member(Object, name), field * DegreesPerRadian) / DegreesPerRadian;
			}

			void Ammo(const char* name, std::uint16_t& field)
			{
				float rounds = Utils::JsonAccess::ToFloat(Utils::JsonAccess::Member(Object, name), float(field) / 256.0f);
				field = std::uint16_t(std::clamp<std::int32_t>(std::int32_t(std::round(rounds * 256.0f)), 0, UINT16_MAX));
			}

			void Read(const Json::Value& value, bool& field)
			{
				field = Utils::JsonAccess::ToBool(value, field);
			}

			void Read(const Json::Value& value, float& field)
			{
				field = Utils::JsonAccess::ToFloat(value, field);
			}

			void Read(const Json::Value& value, std::int32_t& field)
			{
				field = Utils::JsonAccess::ToInt(value, field);
			}

			void Read(const Json::Value& value, std::uint8_t& field)
			{
				field = std::uint8_t(std::clamp<std::int32_t>(Utils::JsonAccess::ToInt(value, field), 0, UINT8_MAX));
			}

			void Read(const Json::Value& value, std::uint16_t& field)
			{
				field = std::uint16_t(std::clamp<std::int32_t>(Utils::JsonAccess::ToInt(value, field), 0, UINT16_MAX));
			}

			void Read(const Json::Value& value, String& field)
			{
				if (value.isString()) {
					field = Utils::JsonAccess::ToString(value);
				}
			}

			template<class T, typename std::enable_if<std::is_enum<T>::value, int>::type = 0>
			void Read(const Json::Value& value, T& field)
			{
				ParseEnum(value, field);
			}

			void Read(const Json::Value& value, WeaponType& field)
			{
				if (value.isString() && StringUtils::equalsIgnoreCase(Utils::JsonAccess::ToString(value), OwnWeaponName)) {
					field = WeaponType::Unknown;
				} else {
					ParseEnum(value, field);
				}
			}

			// An animation of the game, or an image file next to the template, empty for the sprite of the weapon of the game
			void Read(const Json::Value& value, WeaponSprite& field)
			{
				using namespace Utils::JsonAccess;

				WeaponSprite sprite;
				String metadata = GetString(value, "Metadata");
				String file = GetString(value, "File");
				if (!metadata.empty()) {
					sprite.Metadata = Death::move(metadata);
					sprite.AnimState = GetUInt(value, "AnimState", 0);
				} else if (!file.empty()) {
					WeaponTemplateImage image;
					image.Path = fs::CombinePath(Directory, file);
					image.FrameColumns = std::max<std::int32_t>(GetInt(value, "Columns", 0), 0);
					image.FrameRows = std::max<std::int32_t>(GetInt(value, "Rows", 0), 0);
					image.FrameCount = std::max<std::int32_t>(GetInt(value, "Frames", 0), 0);
					image.FrameRate = GetFloat(value, "FrameRate", image.FrameRate);
					image.HasHotspot = (Member(value, "HotspotX").isNumeric() || Member(value, "HotspotY").isNumeric());
					image.HotspotX = GetInt(value, "HotspotX", 0);
					image.HotspotY = GetInt(value, "HotspotY", 0);
					sprite.Image = AddImage(image);
					if (sprite.Image < 0) {
						Failed = true;
						return;
					}
				}
				field = Death::move(sprite);
			}
		};

		struct TemplateWriter
		{
			Json::Value& Object;
			const WeaponsDescription& Weapons;
			Function<String(std::int32_t)>& ImageFile;

			template<class T>
			void operator()(const char* name, const T& field)
			{
				Json::Value value = Write(field);
				if (!value.isNull()) {
					Object[name] = Death::move(value);
				}
			}

			void Angle(const char* name, float field)
			{
				Object[name] = double(field * DegreesPerRadian);
			}

			void Ammo(const char* name, std::uint16_t field)
			{
				Object[name] = double(field) / 256.0;
			}

			Json::Value Write(bool field)
			{
				return Json::Value(field);
			}

			Json::Value Write(float field)
			{
				return Json::Value(double(field));
			}

			Json::Value Write(std::int32_t field)
			{
				return Json::Value(Json::Int(field));
			}

			Json::Value Write(std::uint8_t field)
			{
				return Json::Value(Json::Int(field));
			}

			Json::Value Write(std::uint16_t field)
			{
				return Json::Value(Json::Int(field));
			}

			Json::Value Write(const String& field)
			{
				return (field.empty() ? Json::Value() : ToJson(field));
			}

			template<class T, typename std::enable_if<std::is_enum<T>::value, int>::type = 0>
			Json::Value Write(T field)
			{
				ArrayView<const StringView> names = GetEnumNames(T());
				std::size_t index = std::size_t(field);
				return (index < names.size() ? ToJson(names[index]) : Json::Value());
			}

			Json::Value Write(WeaponType field)
			{
				return (field == WeaponType::Unknown ? ToJson(OwnWeaponName) : Write<WeaponType>(field));
			}

			Json::Value Write(const WeaponSprite& field)
			{
				Json::Value value;
				if (!field.Metadata.empty()) {
					value["Metadata"] = ToJson(field.Metadata);
					value["AnimState"] = Json::UInt(field.AnimState);
				} else if (field.Image >= 0 && field.Image < std::int32_t(Weapons.Images.size())) {
					String file = ImageFile(field.Image);
					if (file.empty()) {
						return Json::Value();
					}
					const WeaponImage& image = Weapons.Images[field.Image];
					value["File"] = ToJson(file);
					value["Columns"] = Json::Int(image.FrameColumns);
					value["Rows"] = Json::Int(image.FrameRows);
					value["Frames"] = Json::Int(image.FrameCount);
					value["FrameRate"] = double(image.FrameRate);
					value["HotspotX"] = Json::Int(image.HotspotX);
					value["HotspotY"] = Json::Int(image.HotspotY);
				}
				return value;
			}
		};
	}

	bool ReadCustomWeaponTemplate(const Json::Value& value, StringView sourcePath, CustomWeaponTemplate& result)
	{
		using namespace Utils::JsonAccess;

		if (!StringUtils::equalsIgnoreCase(GetString(value, "Kind"), "Custom"_s)) {
			return false;
		}
		result.Name = GetString(value, "Name");
		result.SourcePath = sourcePath;
		String image = GetString(value, "Image");
		result.Image = (image.empty() ? String() : fs::CombinePath(fs::GetDirectoryName(sourcePath), image));
		result.BasedOn = WeaponType::Blaster;
		ParseEnum(Member(value, "BasedOn"), result.BasedOn);
		result.Description = std::make_shared<Json::Value>(value);
		return true;
	}

	bool ApplyCustomWeaponTemplate(const CustomWeaponTemplate& weaponTemplate, WeaponDefinition& weapon,
		Function<std::int32_t(const WeaponTemplateImage&)>&& addImage, Function<std::int32_t(StringView)>&& addSound)
	{
		using namespace Utils::JsonAccess;

		if (weaponTemplate.Description == nullptr) {
			return false;
		}
		const Json::Value& value = *weaponTemplate.Description;
		String directory = fs::GetDirectoryName(weaponTemplate.SourcePath);
		bool failed = false;

		String name = GetString(value, "Name");
		if (!name.empty()) {
			weapon.Name = Death::move(name);
		}
		CustomWeaponOptions& custom = weapon.GetCustom();
		TemplateReader reader{value, directory, addImage, failed};
		reader("StartAmmo", weapon.StartAmmo);
		reader("HudIcon", custom.HudIcon);
		reader("HudIconPowered", custom.HudIconPowered);
		reader("Pickup", custom.Pickup);
		TemplateReader options{Member(value, "Options"), directory, addImage, failed};
		VisitOptions(weapon.Options, options);

		const Json::Value& fireValue = Member(value, "Fire");
		TemplateReader fire{fireValue, directory, addImage, failed};
		VisitFire(custom.Fire, fire);
		String soundFile = GetString(fireValue, "SoundFile");
		if (!soundFile.empty()) {
			custom.Fire.SoundIndex = addSound(fs::CombinePath(directory, soundFile));
			failed |= (custom.Fire.SoundIndex < 0);
		} else if (Member(fireValue, "Sound").isString()) {
			custom.Fire.SoundIndex = -1;
		}

		// The powered-up shot is as the usual one, with what it changes of it
		const Json::Value& shotValue = Member(value, "Shot");
		const Json::Value& poweredValue = Member(value, "ShotPowered");
		TemplateReader shot{shotValue, directory, addImage, failed};
		VisitShot(custom.Shot, shot);
		if (shotValue.isObject() || poweredValue.isObject()) {
			custom.ShotPowered = custom.Shot;
		}
		TemplateReader powered{poweredValue, directory, addImage, failed};
		VisitShot(custom.ShotPowered, powered);
		return !failed;
	}

	void WriteCustomWeaponTemplate(const WeaponDefinition& weapon, const WeaponsDescription& weapons,
		Function<String(std::int32_t)>&& imageFile, Function<String(std::int32_t)>&& soundFile, Json::Value& result)
	{
		result = Json::Value(Json::objectValue);
		result["Kind"] = "Custom";
		const CustomWeaponOptions& custom = weapon.GetCustom();
		TemplateWriter writer{result, weapons, imageFile};
		writer("Name", weapon.Name);
		if (custom.BasedOn != WeaponType::Unknown) {
			writer("BasedOn", custom.BasedOn);
		}
		writer("StartAmmo", weapon.StartAmmo);
		writer("HudIcon", custom.HudIcon);
		writer("HudIconPowered", custom.HudIconPowered);
		writer("Pickup", custom.Pickup);

		Json::Value& options = result["Options"];
		TemplateWriter optionsWriter{options, weapons, imageFile};
		VisitOptions(weapon.Options, optionsWriter);

		Json::Value& fire = result["Fire"];
		TemplateWriter fireWriter{fire, weapons, imageFile};
		VisitFire(custom.Fire, fireWriter);
		if (custom.Fire.SoundIndex >= 0 && custom.Fire.SoundIndex < std::int32_t(weapons.Sounds.size())) {
			String file = soundFile(custom.Fire.SoundIndex);
			if (!file.empty()) {
				fire["SoundFile"] = ToJson(file);
			}
		}

		Json::Value& shot = result["Shot"];
		TemplateWriter shotWriter{shot, weapons, imageFile};
		VisitShot(custom.Shot, shotWriter);
		Json::Value& powered = result["ShotPowered"];
		TemplateWriter poweredWriter{powered, weapons, imageFile};
		VisitShot(custom.ShotPowered, poweredWriter);
	}

	std::string WriteWeaponTemplateFile(const Json::Value& value)
	{
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "\t";
		// Numbers as short as they're typed, not as the nearest double of a float
		builder["precision"] = 7;
		return Json::writeString(builder, value);
	}
}

#endif
