#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"
#include "../../../nCine/I18n.h"

using namespace nCine;

namespace Jazz2::Editor::EventCatalogParts
{
	ArrayView<const EventDescriptor> GetItemEvents()
	{
		// Built on the first call, after the language is loaded, only what's translated here by _x() is translated, see CatalogText
		// Values of WeaponType, names of weapons aren't translated
		static const ParamOption WeaponTypes[] = {
			{ "Blaster", 0 }, { "Bouncer", 1 }, { "Freezer", 2 }, { "Seeker", 3 }, { "RF", 4 },
			{ "Toaster", 5 }, { "TNT", 6 }, { "Pepper", 7 }, { "Electro", 8 }, { "Thunderbolt", 9 }
		};
		// Blaster has no ammo, so it's left out
		static const ParamOption AmmoTypes[] = {
			{ "Bouncer", 1 }, { "Freezer", 2 }, { "Seeker", 3 }, { "RF", 4 }, { "Toaster", 5 },
			{ "TNT", 6 }, { "Pepper", 7 }, { "Electro", 8 }, { "Thunderbolt", 9 }
		};
		// Blaster (0) means ammo of a random weapon the players already have
		static const ParamOption AmmoContainerTypes[] = {
			// TRANSLATORS: Choice of a parameter of an event, ammo of a random weapon the players have instead of a chosen one
			{ _x("editor", "Random"), 0 }, { "Bouncer", 1 }, { "Freezer", 2 }, { "Seeker", 3 }, { "RF", 4 },
			{ "Toaster", 5 }, { "TNT", 6 }, { "Pepper", 7 }, { "Electro", 8 }, { "Thunderbolt", 9 }
		};

		// TRANSLATORS: Choices of a parameter of an event (coin metals)
		static const ParamOption CoinTypes[] = { { _x("editor", "Silver"), 0 }, { _x("editor", "Gold"), 1 } };
		static const EventParamField CoinFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Type"), 0, 8, CoinTypes).WithDescription(_x("editor", "Silver is worth 1 coin, gold 5 coins")),
		};
		// CoinCollectible uses the type as the animation, Coins.res has only silver (0) and gold (1)
		static const EventPreviewCase CoinPreview[] = {
			{ 0, "Collectible/Coins", 0 }, { 1, "Collectible/Coins", 1 }
		};

		// Names of colors aren't translated
		static const ParamOption GemColors[] = { { "Red", 0 }, { "Green", 1 }, { "Blue", 2 }, { "Purple", 3 } };
		static const EventParamField GemFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Color"), 0, 2, GemColors).WithDescription(_x("editor", "Red is worth 1 gem, green 5, blue 10 and purple 1")),
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(_x("editor", "Disappears"), 1, 0).WithDescription(_x("editor", "Disappears 8 seconds after it's knocked loose")),
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(_x("editor", "Flipped"), 1, 1).WithDescription(_x("editor", "Faces the other way"))
				.MirroredX(MirrorMode::Toggle),
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(_x("editor", "Delayed"), 1, 2).WithDescription(_x("editor", "Can't be collected for the first 40 frames")),
		};

		static const EventParamField GemRingFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Length"), 0, 8, 0, 255, 8).WithDescription(_x("editor", "Number of red gems in the ring, 0 means 8")),
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Speed"), 1, 8, 0, 255, 8).WithDescription(_x("editor", "Speed of rotation, 0 means 8")),
		};

		static const EventParamField CarrotFields[] = {
			// TRANSLATORS: Parameter of a carrot event, a checkbox for a full carrot that restores all health
			BoolField(_x("editor", "Full"), 0).WithDescription(_x("editor", "Restores full health instead of one heart")),
		};
		static const EventPreviewCase CarrotPreview[] = {
			{ 0, "Collectible/Carrot", std::uint32_t(AnimState::Default) },
			{ EventPreviewCase::AnyValue, "Collectible/CarrotFull", std::uint32_t(AnimState::Default) }
		};

		// Values of FoodType, names of food aren't translated
		static const ParamOption FoodTypes[] = {
			{ "Apple", 1 }, { "Banana", 2 }, { "Cherry", 3 }, { "Orange", 4 }, { "Pear", 5 },
			{ "Pretzel", 6 }, { "Strawberry", 7 }, { "Lemon", 8 }, { "Lime", 9 }, { "Thing", 10 },
			{ "Watermelon", 11 }, { "Peach", 12 }, { "Grapes", 13 }, { "Lettuce", 14 }, { "Eggplant", 15 },
			{ "Cucumber", 16 }, { "Pepsi", 17 }, { "Coke", 18 }, { "Milk", 19 }, { "Pie", 20 },
			{ "Cake", 21 }, { "Donut", 22 }, { "Cupcake", 23 }, { "Chips", 24 }, { "Candy", 25 },
			{ "Chocolate", 26 }, { "Ice Cream", 27 }, { "Burger", 28 }, { "Pizza", 29 }, { "Fries", 30 },
			{ "Chicken Leg", 31 }, { "Sandwich", 32 }, { "Taco", 33 }, { "Hot Dog", 34 }, { "Ham", 35 },
			{ "Cheese", 36 }
		};
		static const EventParamField FoodFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties and its description, Pepsi, Coke and Milk are kinds of food, they aren't translated
			EnumField(_x("editor", "Type"), 0, 8, FoodTypes, 1).WithDescription(_x("editor", "Pepsi, Coke and Milk are drinks")),
		};
		// FoodCollectible::OnActivatedAsync(), by FoodType
		constexpr std::uint32_t FoodAnimation = std::uint32_t(AnimState::Default);
		static const EventPreviewCase FoodPreview[] = {
			{ 1, "Collectible/FoodApple", FoodAnimation }, { 2, "Collectible/FoodBanana", FoodAnimation }, { 3, "Collectible/FoodCherry", FoodAnimation },
			{ 4, "Collectible/FoodOrange", FoodAnimation }, { 5, "Collectible/FoodPear", FoodAnimation }, { 6, "Collectible/FoodPretzel", FoodAnimation },
			{ 7, "Collectible/FoodStrawberry", FoodAnimation }, { 8, "Collectible/FoodLemon", FoodAnimation }, { 9, "Collectible/FoodLime", FoodAnimation },
			{ 10, "Collectible/FoodThing", FoodAnimation }, { 11, "Collectible/FoodWaterMelon", FoodAnimation }, { 12, "Collectible/FoodPeach", FoodAnimation },
			{ 13, "Collectible/FoodGrapes", FoodAnimation }, { 14, "Collectible/FoodLettuce", FoodAnimation }, { 15, "Collectible/FoodEggplant", FoodAnimation },
			{ 16, "Collectible/FoodCucumber", FoodAnimation }, { 17, "Collectible/FoodPepsi", FoodAnimation }, { 18, "Collectible/FoodCoke", FoodAnimation },
			{ 19, "Collectible/FoodMilk", FoodAnimation }, { 20, "Collectible/FoodPie", FoodAnimation }, { 21, "Collectible/FoodCake", FoodAnimation },
			{ 22, "Collectible/FoodDonut", FoodAnimation }, { 23, "Collectible/FoodCupcake", FoodAnimation }, { 24, "Collectible/FoodChips", FoodAnimation },
			{ 25, "Collectible/FoodCandy", FoodAnimation }, { 26, "Collectible/FoodChocolate", FoodAnimation }, { 27, "Collectible/FoodIceCream", FoodAnimation },
			{ 28, "Collectible/FoodBurger", FoodAnimation }, { 29, "Collectible/FoodPizza", FoodAnimation }, { 30, "Collectible/FoodFries", FoodAnimation },
			{ 31, "Collectible/FoodChickenLeg", FoodAnimation }, { 32, "Collectible/FoodSandwich", FoodAnimation }, { 33, "Collectible/FoodTaco", FoodAnimation },
			{ 34, "Collectible/FoodHotDog", FoodAnimation }, { 35, "Collectible/FoodHam", FoodAnimation }, { 36, "Collectible/FoodCheese", FoodAnimation }
		};

		static const EventParamField AmmoFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Weapon"), 0, 8, AmmoTypes, 1),
		};
		// AmmoCollectible::OnActivatedAsync(), by WeaponType; the upgraded ammo (animation 1) is shown only when the first
		// local player has the upgrade
		static const EventPreviewCase AmmoPreview[] = {
			{ 1, "Collectible/AmmoBouncer", 0 }, { 2, "Collectible/AmmoFreezer", 0 }, { 3, "Collectible/AmmoSeeker", 0 },
			{ 4, "Collectible/AmmoRF", 0 }, { 5, "Collectible/AmmoToaster", 0 }, { 6, "Collectible/AmmoTNT", 0 },
			{ 7, "Collectible/AmmoPepper", 0 }, { 8, "Collectible/AmmoElectro", 0 }, { 9, "Collectible/AmmoThunderbolt", 0 }
		};

		static const EventParamField PowerUpWeaponFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Weapon"), 0, 8, WeaponTypes).WithDescription(_x("editor", "Gives 25 ammo and the upgrade")),
		};
		// PowerUpWeaponMonitor::OnActivatedAsync(), by WeaponType; the blaster monitor shows the character of the first local
		// player, Jazz (PlayerType::Jazz) without one
		constexpr std::uint32_t MonitorAnimation = std::uint32_t(AnimState::Default);
		static const EventPreviewCase PowerUpWeaponPreview[] = {
			{ 0, "Object/PowerUp/Blaster", 1 }, { 1, "Object/PowerUp/Bouncer", MonitorAnimation }, { 2, "Object/PowerUp/Freezer", MonitorAnimation },
			{ 3, "Object/PowerUp/Seeker", MonitorAnimation }, { 4, "Object/PowerUp/RF", MonitorAnimation }, { 5, "Object/PowerUp/Toaster", MonitorAnimation },
			{ 7, "Object/PowerUp/Pepper", MonitorAnimation }, { 8, "Object/PowerUp/Electro", MonitorAnimation },
			{ EventPreviewCase::AnyValue, "Object/PowerUp/Empty", MonitorAnimation }
		};

		// What's released when it breaks, the event of the type with the bytes from 3 on as its parameters, see
		// GenericContainer::AddContent()
		static const EventParamField ContainerFields[] = {
			// TRANSLATORS: Parameter of a crate or a barrel event, the event inside of it
			EventTypeField(_x("editor", "Content"), 0).WithDescription(_x("editor", "Event spawned when it breaks, bytes 3 to 15 are passed to it as its parameters")),
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Count"), 2, 8, 0, 255, 1),
			// TRANSLATORS: Parameter of a crate or a barrel event, the parameters of the event inside of it
			ContentsField(_x("editor", "Parameters of the content"), 3, 13, 0)
				.WithDescription(_x("editor", "Parameters of the event spawned when it breaks")),
		};

		static const EventParamField AmmoContainerFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Weapon"), 0, 8, AmmoContainerTypes).WithDescription(_x("editor", "Releases 5 ammo of the weapon, Random releases 4 to 6 ammo of the weapons the players have")),
		};
		// AmmoCrate::OnActivatedAsync(), by WeaponType, Blaster and Thunderbolt use the generic crate
		constexpr std::uint32_t CrateAnimation = std::uint32_t(AnimState::Idle);
		static const EventPreviewCase CrateAmmoPreview[] = {
			{ 1, "Object/Crate/AmmoBouncer", CrateAnimation }, { 2, "Object/Crate/AmmoFreezer", CrateAnimation },
			{ 3, "Object/Crate/AmmoSeeker", CrateAnimation }, { 4, "Object/Crate/AmmoRF", CrateAnimation },
			{ 5, "Object/Crate/AmmoToaster", CrateAnimation }, { 6, "Object/Crate/AmmoTNT", CrateAnimation },
			{ 7, "Object/Crate/AmmoPepper", CrateAnimation }, { 8, "Object/Crate/AmmoElectro", CrateAnimation },
			{ EventPreviewCase::AnyValue, "Object/Crate/Generic", CrateAnimation }
		};

		static const EventParamField GemContainerFields[] = {
			// Number of gems of each color, names of colors aren't translated
			UIntField("Red", 0, 8, 0, 255, 5),
			UIntField("Green", 1, 8, 0, 255),
			UIntField("Blue", 2, 8, 0, 255),
			UIntField("Purple", 3, 8, 0, 255),
		};

		// Values of PowerUpMorphMonitor::MorphType
		// Names of characters aren't translated
		// TRANSLATORS: Choice of a parameter of a morph monitor event, it turns the player into a bird
		static const ParamOption MorphTypes[] = { { "Jazz/Spaz", 0 }, { "Jazz/Spaz/Lori", 1 }, { _x("editor", "Bird"), 2 } };
		static const EventParamField PowerUpMorphFields[] = {
			// TRANSLATORS: Parameter of a morph monitor event, what it turns the player into
			EnumField(_x("editor", "Morph"), 0, 8, MorphTypes).WithDescription(_x("editor", "Switches to the next character, Bird turns the player into a flying bird that shoots until a Fly Off or Revert Morph area")),
		};
		// PowerUpMorphMonitor::OnActivatedAsync(), by PowerUpMorphMonitor::MorphType
		static const EventPreviewCase PowerUpMorphPreview[] = {
			{ 0, "Object/PowerUp/Swap2", MonitorAnimation }, { 1, "Object/PowerUp/Swap3", MonitorAnimation }, { 2, "Object/PowerUp/Bird", MonitorAnimation },
			{ EventPreviewCase::AnyValue, "Object/PowerUp/Empty", MonitorAnimation }
		};

		// Values of ShieldType
		// Kinds of shields aren't translated
		static const ParamOption ShieldTypes[] = { { "Fire", 1 }, { "Water", 2 }, { "Lightning", 3 }, { "Laser", 4 } };
		static const EventParamField PowerUpShieldFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Shield"), 0, 8, ShieldTypes, 1),
		};
		// PowerUpShieldMonitor::OnActivatedAsync(), by ShieldType
		static const EventPreviewCase PowerUpShieldPreview[] = {
			{ 1, "Object/PowerUp/ShieldFire", MonitorAnimation }, { 2, "Object/PowerUp/ShieldWater", MonitorAnimation },
			{ 3, "Object/PowerUp/ShieldLightning", MonitorAnimation }, { 4, "Object/PowerUp/ShieldLaser", MonitorAnimation },
			{ EventPreviewCase::AnyValue, "Object/PowerUp/Empty", MonitorAnimation }
		};

		static const EventParamField RaceCheckpointFields[] = {
			// TRANSLATORS: Parameter of a race waypoint event, its position in the sequence of waypoints
			UIntField(_x("editor", "Order"), 0, 8, 0, 255).WithDescription(_x("editor", "Waypoints are connected in ascending order, the ones with the same order and group are merged into their center")),
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Group"), 1, 8, 0, 255).WithDescription(_x("editor", "Split of the track, each group is a separate route")),
		};

		static const EventParamField PassthroughEventFields[] = {
			// TRANSLATORS: Parameter of an event of the original game, its number there
			UIntField(_x("editor", "Event"), 0, 8, 1, 255, 1).WithDescription(_x("editor", "Number of the event in the original game")),
			// TRANSLATORS: Parameter of an event of the original game, all of its parameters as one number
			UIntField(_x("editor", "Parameters"), 1, 20, 0, 0xFFFFF).WithDescription(_x("editor", "Parameters of the event in the original game, as one number")),
		};

		static const EventDescriptor Events[] = {
			// TRANSLATORS: Name of an event, a collectible coin
			DescribeEvent(EventType::Coin, "Coin", _x("editor", "Coin"), EventCategory::Collectible, nullptr,
				CoinFields, "Collectible/Coins", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, CoinPreview),
			// TRANSLATORS: Name of an event, a collectible gem
			DescribeEvent(EventType::Gem, "Gem", _x("editor", "Gem"), EventCategory::Collectible, nullptr,
				GemFields, "Collectible/Gems", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a big gem that breaks into small ones
			DescribeEvent(EventType::GemGiant, "GemGiant", _x("editor", "Giant Gem"), EventCategory::Collectible, _x("editor", "Breaks into 5 to 12 red gems when it's shot or attacked"),
				"Object/GemGiant", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a rotating ring of gems
			DescribeEvent(EventType::GemRing, "GemRing", _x("editor", "Gem Ring"), EventCategory::Collectible, _x("editor", "Rotating ring of red gems collected all at once"),
				GemRingFields, "Collectible/Gems", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a tile that releases gems when a player buttstomps onto it
			DescribeEvent(EventType::GemStomp, "GemStomp", _x("editor", "Gem Stomp"), EventCategory::Collectible, _x("editor", "Releases 8 red gems when a player buttstomps onto this tile, only once"),
				nullptr, std::uint32_t(AnimState::Default), EventTraits::Area),
			// TRANSLATORS: Name of an event, a collectible carrot that restores health
			DescribeEvent(EventType::Carrot, "Carrot", _x("editor", "Carrot"), EventCategory::Collectible, _x("editor", "Restores health"),
				CarrotFields, "Collectible/Carrot", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, CarrotPreview),
			// TRANSLATORS: Name of an event, a carrot that lets the player fly
			DescribeEvent(EventType::CarrotFly, "CarrotFly", _x("editor", "Fly Carrot"), EventCategory::Collectible, _x("editor", "Gives the copter ears"),
				"Collectible/CarrotFly", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a carrot that makes the player invincible
			DescribeEvent(EventType::CarrotInvincible, "CarrotInvincible", _x("editor", "Invincibility Carrot"), EventCategory::Collectible, _x("editor", "Makes the player invincible for 30 seconds"),
				"Collectible/CarrotInvincible", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, an extra life
			DescribeEvent(EventType::OneUp, "OneUp", _x("editor", "1up"), EventCategory::Collectible, _x("editor", "Extra life, a full carrot in cooperation"),
				"Collectible/OneUp", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a collectible that increases the fire rate
			DescribeEvent(EventType::FastFire, "FastFire", _x("editor", "Fast Fire"), EventCategory::Collectible, _x("editor", "Increases the fire rate"),
				"Collectible/FastFireJazz", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a collectible piece of food
			DescribeEvent(EventType::Food, "Food", _x("editor", "Food"), EventCategory::Collectible, _x("editor", "Counts towards the sugar rush"),
				FoodFields, "Collectible/FoodApple", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, FoodPreview),
			// TRANSLATORS: Name of an event, collectible ammunition of a weapon
			DescribeEvent(EventType::Ammo, "Ammo", _x("editor", "Ammo"), EventCategory::Collectible, _x("editor", "Gives 3 ammo of the weapon"),
				AmmoFields, "Collectible/AmmoBouncer", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, AmmoPreview),
			// TRANSLATORS: Name of an event, a monitor that upgrades a weapon
			DescribeEvent(EventType::PowerUpWeapon, "PowerUpWeapon", _x("editor", "Weapon Power-up"), EventCategory::Container, _x("editor", "Monitor that upgrades a weapon when it's broken"),
				PowerUpWeaponFields, "Object/PowerUp/Blaster", std::uint32_t(1) /* PlayerType::Jazz */, EventTraits::Object)
				.WithPreviewCases(0, PowerUpWeaponPreview),

			// TRANSLATORS: Name of an event, a breakable wooden crate
			DescribeEvent(EventType::Crate, "Crate", _x("editor", "Crate"), EventCategory::Container, _x("editor", "Wooden crate that releases another event when it's broken"),
				ContainerFields, "Object/Crate/Generic", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a breakable barrel
			DescribeEvent(EventType::Barrel, "Barrel", _x("editor", "Barrel"), EventCategory::Container, _x("editor", "Barrel that releases another event when it's broken"),
				ContainerFields, "Object/BarrelContainer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a breakable wooden crate with ammunition
			DescribeEvent(EventType::CrateAmmo, "CrateAmmo", _x("editor", "Ammo Crate"), EventCategory::Container, _x("editor", "Wooden crate that releases ammo when it's broken"),
				AmmoContainerFields, "Object/Crate/Generic", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(0, CrateAmmoPreview),
			// TRANSLATORS: Name of an event, a breakable barrel with ammunition
			DescribeEvent(EventType::BarrelAmmo, "BarrelAmmo", _x("editor", "Ammo Barrel"), EventCategory::Container, _x("editor", "Barrel that releases ammo when it's broken"),
				AmmoContainerFields, "Object/BarrelContainer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a breakable wooden crate with gems
			DescribeEvent(EventType::CrateGem, "CrateGem", _x("editor", "Gem Crate"), EventCategory::Container, _x("editor", "Wooden crate that releases gems when it's broken"),
				GemContainerFields, "Object/Crate/Generic", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a breakable barrel with gems
			DescribeEvent(EventType::BarrelGem, "BarrelGem", _x("editor", "Gem Barrel"), EventCategory::Container, _x("editor", "Barrel that releases gems when it's broken"),
				GemContainerFields, "Object/BarrelContainer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a monitor that changes the character
			DescribeEvent(EventType::PowerUpMorph, "PowerUpMorph", _x("editor", "Morph Monitor"), EventCategory::Container, _x("editor", "Monitor that changes the character, or turns the player into a bird, when it's broken"),
				PowerUpMorphFields, "Object/PowerUp/Swap2", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, PowerUpMorphPreview),
			// TRANSLATORS: Name of an event, a monitor that gives a shield
			DescribeEvent(EventType::PowerUpShield, "PowerUpShield", _x("editor", "Shield Monitor"), EventCategory::Container, _x("editor", "Monitor that gives a shield for 30 seconds when it's broken"),
				PowerUpShieldFields, "Object/PowerUp/ShieldFire", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, PowerUpShieldPreview),

			// TRANSLATORS: Name of an event, a point of the track in the race mode
			DescribeEvent(EventType::RaceCheckpoint, "RaceCheckpoint", _x("editor", "Race Waypoint"), EventCategory::Other, _x("editor", "Ordered waypoint of the track for the minimap and the race ranking"),
				RaceCheckpointFields, nullptr, std::uint32_t(AnimState::Default), EventTraits::MultiplayerOnly),

			// TRANSLATORS: Name of an event, one of the original game that this game doesn't use
			DescribeEvent(EventType::PassthroughEvent, "PassthroughEvent", _x("editor", "Passthrough Event"), EventCategory::Other,
				_x("editor", "Event of the original game that the game doesn't use, kept for the scripts of the level, which read the events of the level"),
				PassthroughEventFields, nullptr, std::uint32_t(AnimState::Default), EventTraits::Unused),
		};

		return Events;
	}
}

#endif
