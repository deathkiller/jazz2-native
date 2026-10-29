#pragma once

#include "WeaponType.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

using namespace Death::Containers;

namespace Jazz2
{
	/**
		@brief How often a weapon fires while the button is held, as JJ2+ scripts know it

		The weapons of the game keep how they fire, except that tapping fire of a @ref WeaponStyle::Capped one isn't
		faster than holding it. A custom weapon fires as its style says.
	*/
	enum class WeaponStyle : std::int8_t
	{
		Normal,				/**< Fires as long as the button is held, tapping fires faster */
		Missile,			/**< Fires once per press */
		Popcorn,			/**< Fires as long as the button is held, at least as often as without fast fire */
		Capped				/**< Fires as long as the button is held, tapping doesn't fire faster */
	};

	/**
		@brief How many shots a weapon fires at once and how, as JJ2+ scripts know it

		The values are those of `SPREAD::Spread` of JJ2+.
	*/
	enum class WeaponSpread : std::int32_t
	{
		Normal = 0,					/**< One shot */
		Double = 1,					/**< Two shots, angled up and down */
		Triple = 2,					/**< Three shots */
		DoubleOrTriple = 3,			/**< Two shots, three when powered up */
		DirectionAndAim = 4,		/**< Two shots, one where the player faces and one where it aims */
		NormalOrDirectionAndAim = 5,	/**< One shot, two when powered up */
		Pepper = 6,					/**< A spray of shots */
		NormalOrPepper = 7,			/**< One shot, a spray when powered up */
		Toaster = 8					/**< One shot, faster with fast fire */
	};

	/**
		@brief Options of a weapon a level can change, as JJ2+ scripts know them

		Scripts read and write the fields by their offsets (`jjWEAPON`), so a script changes the options the game itself
		plays by. The fields a script sees as `int` or as an enum stay 32 bits wide.
	*/
	struct WeaponOptions
	{
		/** @brief How many times as much ammo a pickup gives, the ammo shown is divided by it */
		std::int32_t Multiplier;
		/** @brief Most ammo a player can have, -1 for the default */
		std::int32_t Maximum;
		/** @brief Gems a player hit by a shot loses in Treasure Hunt */
		std::int32_t GemsLost;
		/** @brief Gems a player hit by a powered-up shot loses in Treasure Hunt */
		std::int32_t GemsLostPowerup;
		/** @brief How many shots it fires at once */
		WeaponSpread Spread;
		/** @brief How it fires */
		WeaponStyle Style;
		/** @brief Whether the ammo never runs out */
		bool Infinite;
		/** @brief Whether the ammo is full again when the level starts */
		bool Replenishes;
		/** @brief Whether a shield fires in place of the weapon while it lasts */
		bool ReplacedByShield;
		/** @brief Whether a shot under water is an air bubble */
		bool ReplacedByBubbles;
		/** @brief Whether a gun crate or barrel of random ammo may give its ammo */
		bool ComesFromGunCrates;
		/** @brief Whether the shots turn towards where the player aims over a few shots, scripts only */
		bool GradualAim;
		/** @brief Whether the sound of firing is played */
		bool DefaultSample;
		/** @brief Whether the weapon can be used in the level */
		bool Allowed;
		/** @brief Whether the weapon can be powered up in the level */
		bool AllowedPowerup;
		/** @brief Whether a bird helping the player fires it */
		bool ComesFromBirds;
		/** @brief Whether a bird helping the player fires it powered up */
		bool ComesFromBirdsPowerup;

		/** @brief Returns the options of a weapon of the game */
		static WeaponOptions MakeDefault(WeaponType weaponType);

		/** @brief Returns the most ammo a player can have, in whole rounds before @ref Multiplier */
		std::int32_t GetMaximumAmmo() const;

		bool operator==(const WeaponOptions& other) const;
		bool operator!=(const WeaponOptions& other) const {
			return !operator==(other);
		}
	};

	/** @brief What a level makes of one of the weapons */
	enum class WeaponKind : std::uint8_t
	{
		Stock,				/**< The weapon of the game, maybe with other options */
		Custom,				/**< A weapon of the level, which fires shots it describes */
		Script				/**< A weapon the script of the level sets up */
	};

	/** @brief When a custom shot goes off */
	enum class ShotTrigger : std::uint8_t
	{
		Contact,			/**< When it hits something */
		Proximity,			/**< When something comes close, it doesn't move once placed */
		Timer				/**< When its time runs out */
	};

	/** @brief How a custom shot is drawn */
	enum class ShotLook : std::uint8_t
	{
		Sprite,				/**< Its sprite */
		Glow,				/**< Its sprite added to what's behind, as the pepper spray */
		Electro,			/**< A swirl of sparks of its sprite left behind it, as the electro blaster */
		Beam				/**< A beam from the muzzle, as the thunderbolt, as long as it would fly in its time */
	};

	/** @brief Whether the sprite of a custom shot is flipped, beyond turning to where it's fired */
	enum class ShotFlip : std::uint8_t
	{
		Normal,				/**< Not flipped */
		Flipped,			/**< Flipped */
		Random				/**< Each shot at random */
	};

	/**
		@brief Sprite of a custom weapon

		Either an animation of the game, or an image the level has (see @ref WeaponsDescription::Images). Empty means
		the sprite of the weapon of the game.
	*/
	struct WeaponSprite
	{
		/** @brief Path of the metadata of the animation of the game, empty for none */
		String Metadata;
		/** @brief Animation state of the animation of the game */
		std::uint32_t AnimState = 0;
		/** @brief Index of an image of the level, -1 for none */
		std::int32_t Image = -1;

		/** @brief Returns `true` if it's the sprite of the weapon of the game */
		bool IsEmpty() const {
			return (Metadata.empty() && Image < 0);
		}

		bool operator==(const WeaponSprite& other) const;
		bool operator!=(const WeaponSprite& other) const {
			return !operator==(other);
		}
	};

	/** @brief How a custom weapon fires */
	struct WeaponFireOptions
	{
		/** @brief Name of the sound of firing, from the metadata of the shot, or of the player */
		String Sound;
		/** @brief Frames between shots */
		float Cooldown = 30.0f;
		/** @brief Frames each fast fire takes from the cooldown */
		float FastFireStep = 2.76f;
		/** @brief Angle between the shots fired at once, in radians */
		float SpreadAngle = 0.0f;
		/** @brief Random angle added to each shot, in radians */
		float RandomSpread = 0.0f;
		/** @brief How hard firing pushes the player back */
		float Recoil = 0.0f;
		/** @brief Ammo a shot takes, 256 is one round */
		std::uint16_t AmmoPerShot = 256;
		/** @brief Shots fired at once */
		std::uint8_t ShotsPerFire = 1;
		/** @brief Whether it fires under water */
		bool AllowedInWater = true;
		/** @brief Whether the sound plays in a loop while it fires */
		bool LoopSound = false;
		/** @brief Whether the muzzle flashes */
		bool Flare = true;

		bool operator==(const WeaponFireOptions& other) const;
		bool operator!=(const WeaponFireOptions& other) const {
			return !operator==(other);
		}
	};

	/** @brief Shot of a custom weapon */
	struct WeaponShotOptions
	{
		/** @brief Sprite of the shot */
		WeaponSprite Sprite;
		/** @brief Second sprite, each shot takes one of the two at random, empty for none */
		WeaponSprite AltSprite;
		/** @brief Speed in pixels a frame */
		float Speed = 10.0f;
		/** @brief Speed added downwards each frame */
		float Gravity = 0.0f;
		/** @brief Frames it lasts */
		float Lifetime = 60.0f;
		/** @brief Speed kept after a bounce */
		float Elasticity = 0.8f;
		/** @brief How fast it turns towards an enemy, in radians a frame, 0 for never */
		float HomingTurnRate = 0.0f;
		/** @brief How far it sees an enemy to turn to */
		float HomingRange = 160.0f;
		/** @brief Frames it freezes what it hits for, 0 for none */
		float FreezeDuration = 0.0f;
		/** @brief How close something must come for @ref ShotTrigger::Proximity */
		float TriggerRadius = 48.0f;
		/** @brief Radius of the damage it does around where it goes off, 0 for none */
		float AreaRadius = 0.0f;
		/** @brief How hard it pushes away what's around where it goes off */
		float Knockback = 0.0f;
		/** @brief Radius of its light, 0 for none */
		float LightRadius = 0.0f;
		/** @brief Intensity of its light */
		float LightIntensity = 0.8f;
		/** @brief Damage */
		std::int32_t Strength = 1;
		/** @brief Enemies it passes through before it stops */
		std::int32_t Pierce = 0;
		/** @brief Walls it bounces off before it stops */
		std::int32_t Bounces = 0;
		/** @brief Damage it does around where it goes off */
		std::int32_t AreaStrength = 0;
		/** @brief Explosion where it goes off (see @ref Actors::Explosion::Type), -1 for none */
		std::int32_t Explosion = -1;
		/** @brief Weapon of the game whose blocks it destroys, @ref WeaponType::Unknown for its own */
		WeaponType DestroysBlocksAs = WeaponType::Unknown;
		/** @brief When it goes off */
		ShotTrigger Trigger = ShotTrigger::Contact;
		/** @brief How it's drawn */
		ShotLook Look = ShotLook::Sprite;
		/** @brief Whether the sprite is flipped horizontally */
		ShotFlip FlipX = ShotFlip::Normal;
		/** @brief Whether the sprite is flipped vertically */
		ShotFlip FlipY = ShotFlip::Normal;
		/** @brief Whether the speed of the player is added */
		bool InheritPlayerSpeed = true;
		/** @brief Whether it flies through walls */
		bool PassesWalls = false;
		/** @brief Whether it thaws what's frozen */
		bool Thaws = false;
		/** @brief Whether it turns where it flies */
		bool RotatesWithSpeed = true;

		bool operator==(const WeaponShotOptions& other) const;
		bool operator!=(const WeaponShotOptions& other) const {
			return !operator==(other);
		}
	};

	/**
		@brief What a level makes of one of the weapons

		@ref WeaponKind::Stock keeps the weapon of the game and changes only its @ref Options and ammo.
		@ref WeaponKind::Custom fires shots of @ref Shot (@ref ShotPowered when powered up) as @ref Fire says.
		@ref WeaponKind::Script is set up by the script of the level, which gets @ref ScriptParams.
	*/
	struct WeaponDefinition
	{
		/** @brief Name, empty for the name of the weapon of the game */
		String Name;
		/** @brief How it fires, for @ref WeaponKind::Custom */
		WeaponFireOptions Fire;
		/** @brief Shot it fires, for @ref WeaponKind::Custom */
		WeaponShotOptions Shot;
		/** @brief Shot it fires powered up, for @ref WeaponKind::Custom */
		WeaponShotOptions ShotPowered;
		/** @brief Icon on the screen */
		WeaponSprite HudIcon;
		/** @brief Icon on the screen while powered up */
		WeaponSprite HudIconPowered;
		/** @brief Sprite of its ammo */
		WeaponSprite Pickup;
		/** @brief Script file that has the weapon, for @ref WeaponKind::Script */
		String ScriptLibrary;
		/** @brief Script code that creates the weapon, for @ref WeaponKind::Script */
		String ScriptConstructor;
		/** @brief Parameters the script gets, for @ref WeaponKind::Script */
		SmallVector<std::uint8_t, 0> ScriptParams;
		/** @brief Properties of a newer version, kept as they are */
		SmallVector<std::uint8_t, 0> UnknownProperties;
		/** @brief Options as JJ2+ scripts know them */
		WeaponOptions Options = {};
		/** @brief Ammo a player has when the level starts, in whole rounds */
		std::int32_t StartAmmo = 0;
		/** @brief What the level makes of it */
		WeaponKind Kind = WeaponKind::Stock;
		/** @brief Event that becomes an ammo crate of it, 0 for none */
		std::uint8_t AmmoCrateEvent = 0;
		/** @brief Weapon of the game it was made from, it keeps its rules for crates and bosses */
		WeaponType BasedOn = WeaponType::Unknown;

		/** @brief Returns the weapon of the game */
		static WeaponDefinition MakeDefault(WeaponType weaponType);
		/** @brief Returns a custom weapon made from a weapon of the game */
		static WeaponDefinition MakeCustom(WeaponType weaponType, WeaponType basedOn);

		/** @brief Returns `true` if the level doesn't change the weapon */
		bool IsDefault(WeaponType weaponType) const;

		bool operator==(const WeaponDefinition& other) const;
		bool operator!=(const WeaponDefinition& other) const {
			return !operator==(other);
		}
	};

	/** @brief Image of a custom weapon, frames side by side */
	struct WeaponImage
	{
		/** @brief Width of a frame */
		std::int32_t FrameWidth = 0;
		/** @brief Height of a frame */
		std::int32_t FrameHeight = 0;
		/** @brief Number of frames */
		std::int32_t FrameCount = 0;
		/** @brief Speed of the animation, as `FrameRate` of a metadata file */
		float FrameRate = 10.0f;
		/** @brief Point of a frame that's at the position of the actor */
		std::int32_t HotspotX = 0;
		/** @brief Point of a frame that's at the position of the actor */
		std::int32_t HotspotY = 0;
		/** @brief Palette index of each pixel, frames one after another, 0 is transparent */
		SmallVector<std::uint8_t, 0> Pixels;

		bool operator==(const WeaponImage& other) const;
		bool operator!=(const WeaponImage& other) const {
			return !operator==(other);
		}
	};

	/** @brief Weapons of a level */
	struct WeaponsDescription
	{
		/** @brief Weapons by @ref WeaponType */
		WeaponDefinition Weapons[std::int32_t(WeaponType::Count)];
		/** @brief Images the weapons use */
		SmallVector<WeaponImage, 0> Images;
		/** @brief Argument of the hook the weapons of the script share, -1 for none */
		std::int16_t ScriptHookArgument = -1;

		WeaponsDescription();

		/** @brief Returns `true` if the level doesn't change any weapon */
		bool IsDefault() const;

		/**
		 * @brief Returns the path of the metadata an image is available as while a level that has it is loaded
		 *
		 * The path comes from what the image is, so two levels loaded at once share the same image, and a server and
		 * its clients name it the same.
		 */
		static String GetImageMetadataPath(const WeaponImage& image);
		/** @brief Returns the path of the metadata of a sprite, empty if it's the sprite of the weapon of the game */
		String GetSpriteMetadataPath(const WeaponSprite& sprite) const;
	};
}
