#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"

namespace Jazz2::Editor::EventCatalogParts
{
	namespace
	{
		// Values of WeaponType
		constexpr ParamOption WeaponTypes[] = {
			{ N_("Blaster"), 0 }, { N_("Bouncer"), 1 }, { N_("Freezer"), 2 }, { N_("Seeker"), 3 }, { N_("RF"), 4 },
			{ N_("Toaster"), 5 }, { N_("TNT"), 6 }, { N_("Pepper"), 7 }, { N_("Electro"), 8 }, { N_("Thunderbolt"), 9 }
		};
		// Blaster has no ammo, so it's left out
		constexpr ParamOption AmmoTypes[] = {
			{ N_("Bouncer"), 1 }, { N_("Freezer"), 2 }, { N_("Seeker"), 3 }, { N_("RF"), 4 }, { N_("Toaster"), 5 },
			{ N_("TNT"), 6 }, { N_("Pepper"), 7 }, { N_("Electro"), 8 }, { N_("Thunderbolt"), 9 }
		};
		// Blaster (0) means ammo of a random weapon the players already have
		constexpr ParamOption AmmoContainerTypes[] = {
			{ N_("Random"), 0 }, { N_("Bouncer"), 1 }, { N_("Freezer"), 2 }, { N_("Seeker"), 3 }, { N_("RF"), 4 },
			{ N_("Toaster"), 5 }, { N_("TNT"), 6 }, { N_("Pepper"), 7 }, { N_("Electro"), 8 }, { N_("Thunderbolt"), 9 }
		};

		constexpr ParamOption CoinTypes[] = { { N_("Silver"), 0 }, { N_("Gold"), 1 } };
		constexpr EventParamField CoinFields[] = {
			EnumField(N_("Type"), 0, 8, CoinTypes).WithDescription(N_("Silver is worth 1 coin, gold 5 coins")),
		};

		constexpr ParamOption GemColors[] = { { N_("Red"), 0 }, { N_("Green"), 1 }, { N_("Blue"), 2 }, { N_("Purple"), 3 } };
		constexpr EventParamField GemFields[] = {
			EnumField(N_("Color"), 0, 2, GemColors).WithDescription(N_("Red is worth 1 gem, green 5, blue 10 and purple 1")),
			BoolField(N_("Disappears"), 1, 0).WithDescription(N_("Disappears 8 seconds after it's knocked loose")),
			BoolField(N_("Flipped"), 1, 1).WithDescription(N_("Faces the other way")),
			BoolField(N_("Delayed"), 1, 2).WithDescription(N_("Can't be collected for the first 40 frames")),
		};

		constexpr EventParamField GemRingFields[] = {
			UIntField(N_("Length"), 0, 8, 0, 255, 8).WithDescription(N_("Number of red gems in the ring, 0 means 8")),
			UIntField(N_("Speed"), 1, 8, 0, 255, 8).WithDescription(N_("Speed of rotation, 0 means 8")),
		};

		constexpr EventParamField CarrotFields[] = {
			BoolField(N_("Full"), 0).WithDescription(N_("Restores full health instead of one heart")),
		};

		// Values of FoodType
		constexpr ParamOption FoodTypes[] = {
			{ N_("Apple"), 1 }, { N_("Banana"), 2 }, { N_("Cherry"), 3 }, { N_("Orange"), 4 }, { N_("Pear"), 5 },
			{ N_("Pretzel"), 6 }, { N_("Strawberry"), 7 }, { N_("Lemon"), 8 }, { N_("Lime"), 9 }, { N_("Thing"), 10 },
			{ N_("Watermelon"), 11 }, { N_("Peach"), 12 }, { N_("Grapes"), 13 }, { N_("Lettuce"), 14 }, { N_("Eggplant"), 15 },
			{ N_("Cucumber"), 16 }, { N_("Pepsi"), 17 }, { N_("Coke"), 18 }, { N_("Milk"), 19 }, { N_("Pie"), 20 },
			{ N_("Cake"), 21 }, { N_("Donut"), 22 }, { N_("Cupcake"), 23 }, { N_("Chips"), 24 }, { N_("Candy"), 25 },
			{ N_("Chocolate"), 26 }, { N_("Ice Cream"), 27 }, { N_("Burger"), 28 }, { N_("Pizza"), 29 }, { N_("Fries"), 30 },
			{ N_("Chicken Leg"), 31 }, { N_("Sandwich"), 32 }, { N_("Taco"), 33 }, { N_("Hot Dog"), 34 }, { N_("Ham"), 35 },
			{ N_("Cheese"), 36 }
		};
		constexpr EventParamField FoodFields[] = {
			EnumField(N_("Type"), 0, 8, FoodTypes, 1).WithDescription(N_("Pepsi, Coke and Milk are drinks")),
		};

		constexpr EventParamField AmmoFields[] = {
			EnumField(N_("Weapon"), 0, 8, AmmoTypes, 1),
		};

		constexpr EventParamField PowerUpWeaponFields[] = {
			EnumField(N_("Weapon"), 0, 8, WeaponTypes).WithDescription(N_("Gives 25 ammo and the upgrade, TNT and Thunderbolt show an empty monitor")),
		};

		constexpr EventParamField ContainerFields[] = {
			EventTypeField(N_("Content"), 0).WithDescription(N_("Event spawned when it breaks, bytes 3 to 15 are passed to it as its parameters")),
			UIntField(N_("Count"), 2, 8, 0, 255, 1),
		};

		constexpr EventParamField AmmoContainerFields[] = {
			EnumField(N_("Weapon"), 0, 8, AmmoContainerTypes).WithDescription(N_("Releases 5 ammo of the weapon, Random releases 4 to 6 ammo of the weapons the players have")),
		};

		constexpr EventParamField GemContainerFields[] = {
			UIntField(N_("Red"), 0, 8, 0, 255, 5),
			UIntField(N_("Green"), 1, 8, 0, 255),
			UIntField(N_("Blue"), 2, 8, 0, 255),
			UIntField(N_("Purple"), 3, 8, 0, 255),
		};

		// Values of PowerUpMorphMonitor::MorphType
		constexpr ParamOption MorphTypes[] = { { N_("Jazz/Spaz"), 0 }, { N_("Jazz/Spaz/Lori"), 1 }, { N_("Bird"), 2 } };
		constexpr EventParamField PowerUpMorphFields[] = {
			EnumField(N_("Morph"), 0, 8, MorphTypes).WithDescription(N_("Switches to the next character, Bird isn't implemented and can't be broken")),
		};

		// Values of ShieldType
		constexpr ParamOption ShieldTypes[] = { { N_("Fire"), 1 }, { N_("Water"), 2 }, { N_("Lightning"), 3 }, { N_("Laser"), 4 } };
		constexpr EventParamField PowerUpShieldFields[] = {
			EnumField(N_("Shield"), 0, 8, ShieldTypes, 1),
		};

		constexpr EventParamField RaceCheckpointFields[] = {
			UIntField(N_("Order"), 0, 8, 0, 255).WithDescription(N_("Waypoints are connected in ascending order, the ones with the same order and group are merged into their center")),
			UIntField(N_("Group"), 1, 8, 0, 255).WithDescription(N_("Split of the track, each group is a separate route")),
		};

		constexpr EventDescriptor Events[] = {
			DescribeEvent(EventType::Coin, "Coin", N_("Coin"), EventCategory::Collectible, nullptr,
				CoinFields, "Collectible/Coins", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::Gem, "Gem", N_("Gem"), EventCategory::Collectible, nullptr,
				GemFields, "Collectible/Gems", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::GemGiant, "GemGiant", N_("Giant Gem"), EventCategory::Collectible, N_("Breaks into 5 to 12 red gems when it's shot or attacked"),
				"Object/GemGiant", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::GemRing, "GemRing", N_("Gem Ring"), EventCategory::Collectible, N_("Rotating ring of red gems collected all at once"),
				GemRingFields, "Collectible/Gems", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::GemStomp, "GemStomp", N_("Gem Stomp"), EventCategory::Collectible, N_("Releases 8 red gems when a player buttstomps onto this tile, only once"),
				nullptr, std::uint32_t(AnimState::Default), EventTraits::Area),
			DescribeEvent(EventType::Carrot, "Carrot", N_("Carrot"), EventCategory::Collectible, N_("Restores health"),
				CarrotFields, "Collectible/Carrot", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::CarrotFly, "CarrotFly", N_("Fly Carrot"), EventCategory::Collectible, N_("Gives the copter ears"),
				"Collectible/CarrotFly", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::CarrotInvincible, "CarrotInvincible", N_("Invincibility Carrot"), EventCategory::Collectible, N_("Makes the player invincible for 30 seconds"),
				"Collectible/CarrotInvincible", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::OneUp, "OneUp", N_("1up"), EventCategory::Collectible, N_("Extra life, a full carrot in cooperation"),
				"Collectible/OneUp", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::FastFire, "FastFire", N_("Fast Fire"), EventCategory::Collectible, N_("Increases the fire rate, looks like the blaster of the player"),
				"Collectible/FastFireJazz", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::Food, "Food", N_("Food"), EventCategory::Collectible, N_("Counts towards the sugar rush"),
				FoodFields, "Collectible/FoodApple", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::Ammo, "Ammo", N_("Ammo"), EventCategory::Collectible, N_("Gives 3 ammo of the weapon"),
				AmmoFields, "Collectible/AmmoBouncer", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::PowerUpWeapon, "PowerUpWeapon", N_("Weapon Power-up"), EventCategory::Container, N_("Monitor that upgrades a weapon when it's broken"),
				PowerUpWeaponFields, "Object/PowerUp/Blaster", std::uint32_t(1) /* PlayerType::Jazz */, EventTraits::Object),

			DescribeEvent(EventType::Crate, "Crate", N_("Crate"), EventCategory::Container, N_("Wooden crate that releases another event when it's broken"),
				ContainerFields, "Object/Crate/Generic", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::Barrel, "Barrel", N_("Barrel"), EventCategory::Container, N_("Barrel that releases another event when it's broken"),
				ContainerFields, "Object/BarrelContainer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::CrateAmmo, "CrateAmmo", N_("Ammo Crate"), EventCategory::Container, N_("Wooden crate that releases ammo when it's broken"),
				AmmoContainerFields, "Object/Crate/Generic", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BarrelAmmo, "BarrelAmmo", N_("Ammo Barrel"), EventCategory::Container, N_("Barrel that releases ammo when it's broken"),
				AmmoContainerFields, "Object/BarrelContainer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::CrateGem, "CrateGem", N_("Gem Crate"), EventCategory::Container, N_("Wooden crate that releases gems when it's broken"),
				GemContainerFields, "Object/Crate/Generic", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BarrelGem, "BarrelGem", N_("Gem Barrel"), EventCategory::Container, N_("Barrel that releases gems when it's broken"),
				GemContainerFields, "Object/BarrelContainer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::PowerUpMorph, "PowerUpMorph", N_("Morph Monitor"), EventCategory::Container, N_("Monitor that changes the character when it's broken"),
				PowerUpMorphFields, "Object/PowerUp/Swap2", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::PowerUpShield, "PowerUpShield", N_("Shield Monitor"), EventCategory::Container, N_("Monitor that gives a shield for 30 seconds when it's broken"),
				PowerUpShieldFields, "Object/PowerUp/ShieldFire", std::uint32_t(AnimState::Default), EventTraits::Object),

			DescribeEvent(EventType::RaceCheckpoint, "RaceCheckpoint", N_("Race Waypoint"), EventCategory::Other, N_("Ordered waypoint of the track for the minimap and the race ranking"),
				RaceCheckpointFields, nullptr, std::uint32_t(AnimState::Default), EventTraits::MultiplayerOnly),
		};
	}

	ArrayView<const EventDescriptor> GetItemEvents()
	{
		return Events;
	}
}

#endif
