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
		// CoinCollectible uses the type as the animation, Coins.res has only silver (0) and gold (1)
		constexpr EventPreviewCase CoinPreview[] = {
			{ 0, "Collectible/Coins", 0 }, { 1, "Collectible/Coins", 1 }
		};

		constexpr ParamOption GemColors[] = { { N_("Red"), 0 }, { N_("Green"), 1 }, { N_("Blue"), 2 }, { N_("Purple"), 3 } };
		constexpr EventParamField GemFields[] = {
			EnumField(N_("Color"), 0, 2, GemColors).WithDescription(N_("Red is worth 1 gem, green 5, blue 10 and purple 1")),
			BoolField(N_("Disappears"), 1, 0).WithDescription(N_("Disappears 8 seconds after it's knocked loose")),
			BoolField(N_("Flipped"), 1, 1).WithDescription(N_("Faces the other way"))
				.MirroredX(MirrorMode::Toggle),
			BoolField(N_("Delayed"), 1, 2).WithDescription(N_("Can't be collected for the first 40 frames")),
		};

		constexpr EventParamField GemRingFields[] = {
			UIntField(N_("Length"), 0, 8, 0, 255, 8).WithDescription(N_("Number of red gems in the ring, 0 means 8")),
			UIntField(N_("Speed"), 1, 8, 0, 255, 8).WithDescription(N_("Speed of rotation, 0 means 8")),
		};

		constexpr EventParamField CarrotFields[] = {
			BoolField(N_("Full"), 0).WithDescription(N_("Restores full health instead of one heart")),
		};
		constexpr EventPreviewCase CarrotPreview[] = {
			{ 0, "Collectible/Carrot", std::uint32_t(AnimState::Default) },
			{ EventPreviewCase::AnyValue, "Collectible/CarrotFull", std::uint32_t(AnimState::Default) }
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
		// FoodCollectible::OnActivatedAsync(), by FoodType
		constexpr std::uint32_t FoodAnimation = std::uint32_t(AnimState::Default);
		constexpr EventPreviewCase FoodPreview[] = {
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

		constexpr EventParamField AmmoFields[] = {
			EnumField(N_("Weapon"), 0, 8, AmmoTypes, 1),
		};
		// AmmoCollectible::OnActivatedAsync(), by WeaponType; the upgraded ammo (animation 1) is shown only when the first
		// local player has the upgrade
		constexpr EventPreviewCase AmmoPreview[] = {
			{ 1, "Collectible/AmmoBouncer", 0 }, { 2, "Collectible/AmmoFreezer", 0 }, { 3, "Collectible/AmmoSeeker", 0 },
			{ 4, "Collectible/AmmoRF", 0 }, { 5, "Collectible/AmmoToaster", 0 }, { 6, "Collectible/AmmoTNT", 0 },
			{ 7, "Collectible/AmmoPepper", 0 }, { 8, "Collectible/AmmoElectro", 0 }, { 9, "Collectible/AmmoThunderbolt", 0 }
		};

		constexpr EventParamField PowerUpWeaponFields[] = {
			EnumField(N_("Weapon"), 0, 8, WeaponTypes).WithDescription(N_("Gives 25 ammo and the upgrade")),
		};
		// PowerUpWeaponMonitor::OnActivatedAsync(), by WeaponType; the blaster monitor shows the character of the first local
		// player, Jazz (PlayerType::Jazz) without one
		constexpr std::uint32_t MonitorAnimation = std::uint32_t(AnimState::Default);
		constexpr EventPreviewCase PowerUpWeaponPreview[] = {
			{ 0, "Object/PowerUp/Blaster", 1 }, { 1, "Object/PowerUp/Bouncer", MonitorAnimation }, { 2, "Object/PowerUp/Freezer", MonitorAnimation },
			{ 3, "Object/PowerUp/Seeker", MonitorAnimation }, { 4, "Object/PowerUp/RF", MonitorAnimation }, { 5, "Object/PowerUp/Toaster", MonitorAnimation },
			{ 7, "Object/PowerUp/Pepper", MonitorAnimation }, { 8, "Object/PowerUp/Electro", MonitorAnimation },
			{ EventPreviewCase::AnyValue, "Object/PowerUp/Empty", MonitorAnimation }
		};

		// What's released when it breaks, the event of the type with the bytes from 3 on as its parameters, see
		// GenericContainer::AddContent()
		constexpr EventParamField ContainerFields[] = {
			EventTypeField(N_("Content"), 0).WithDescription(N_("Event spawned when it breaks, bytes 3 to 15 are passed to it as its parameters")),
			UIntField(N_("Count"), 2, 8, 0, 255, 1),
			ContentsField(N_("Parameters of the content"), 3, 13, 0)
				.WithDescription(N_("Parameters of the event spawned when it breaks")),
		};

		constexpr EventParamField AmmoContainerFields[] = {
			EnumField(N_("Weapon"), 0, 8, AmmoContainerTypes).WithDescription(N_("Releases 5 ammo of the weapon, Random releases 4 to 6 ammo of the weapons the players have")),
		};
		// AmmoCrate::OnActivatedAsync(), by WeaponType, Blaster and Thunderbolt use the generic crate
		constexpr std::uint32_t CrateAnimation = std::uint32_t(AnimState::Idle);
		constexpr EventPreviewCase CrateAmmoPreview[] = {
			{ 1, "Object/Crate/AmmoBouncer", CrateAnimation }, { 2, "Object/Crate/AmmoFreezer", CrateAnimation },
			{ 3, "Object/Crate/AmmoSeeker", CrateAnimation }, { 4, "Object/Crate/AmmoRF", CrateAnimation },
			{ 5, "Object/Crate/AmmoToaster", CrateAnimation }, { 6, "Object/Crate/AmmoTNT", CrateAnimation },
			{ 7, "Object/Crate/AmmoPepper", CrateAnimation }, { 8, "Object/Crate/AmmoElectro", CrateAnimation },
			{ EventPreviewCase::AnyValue, "Object/Crate/Generic", CrateAnimation }
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
			EnumField(N_("Morph"), 0, 8, MorphTypes).WithDescription(N_("Switches to the next character, Bird turns the player into a flying bird that shoots until a Fly Off or Revert Morph area")),
		};
		// PowerUpMorphMonitor::OnActivatedAsync(), by PowerUpMorphMonitor::MorphType
		constexpr EventPreviewCase PowerUpMorphPreview[] = {
			{ 0, "Object/PowerUp/Swap2", MonitorAnimation }, { 1, "Object/PowerUp/Swap3", MonitorAnimation }, { 2, "Object/PowerUp/Bird", MonitorAnimation },
			{ EventPreviewCase::AnyValue, "Object/PowerUp/Empty", MonitorAnimation }
		};

		// Values of ShieldType
		constexpr ParamOption ShieldTypes[] = { { N_("Fire"), 1 }, { N_("Water"), 2 }, { N_("Lightning"), 3 }, { N_("Laser"), 4 } };
		constexpr EventParamField PowerUpShieldFields[] = {
			EnumField(N_("Shield"), 0, 8, ShieldTypes, 1),
		};
		// PowerUpShieldMonitor::OnActivatedAsync(), by ShieldType
		constexpr EventPreviewCase PowerUpShieldPreview[] = {
			{ 1, "Object/PowerUp/ShieldFire", MonitorAnimation }, { 2, "Object/PowerUp/ShieldWater", MonitorAnimation },
			{ 3, "Object/PowerUp/ShieldLightning", MonitorAnimation }, { 4, "Object/PowerUp/ShieldLaser", MonitorAnimation },
			{ EventPreviewCase::AnyValue, "Object/PowerUp/Empty", MonitorAnimation }
		};

		constexpr EventParamField RaceCheckpointFields[] = {
			UIntField(N_("Order"), 0, 8, 0, 255).WithDescription(N_("Waypoints are connected in ascending order, the ones with the same order and group are merged into their center")),
			UIntField(N_("Group"), 1, 8, 0, 255).WithDescription(N_("Split of the track, each group is a separate route")),
		};

		constexpr EventDescriptor Events[] = {
			DescribeEvent(EventType::Coin, "Coin", N_("Coin"), EventCategory::Collectible, nullptr,
				CoinFields, "Collectible/Coins", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, CoinPreview),
			DescribeEvent(EventType::Gem, "Gem", N_("Gem"), EventCategory::Collectible, nullptr,
				GemFields, "Collectible/Gems", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::GemGiant, "GemGiant", N_("Giant Gem"), EventCategory::Collectible, N_("Breaks into 5 to 12 red gems when it's shot or attacked"),
				"Object/GemGiant", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::GemRing, "GemRing", N_("Gem Ring"), EventCategory::Collectible, N_("Rotating ring of red gems collected all at once"),
				GemRingFields, "Collectible/Gems", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::GemStomp, "GemStomp", N_("Gem Stomp"), EventCategory::Collectible, N_("Releases 8 red gems when a player buttstomps onto this tile, only once"),
				nullptr, std::uint32_t(AnimState::Default), EventTraits::Area),
			DescribeEvent(EventType::Carrot, "Carrot", N_("Carrot"), EventCategory::Collectible, N_("Restores health"),
				CarrotFields, "Collectible/Carrot", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, CarrotPreview),
			DescribeEvent(EventType::CarrotFly, "CarrotFly", N_("Fly Carrot"), EventCategory::Collectible, N_("Gives the copter ears"),
				"Collectible/CarrotFly", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::CarrotInvincible, "CarrotInvincible", N_("Invincibility Carrot"), EventCategory::Collectible, N_("Makes the player invincible for 30 seconds"),
				"Collectible/CarrotInvincible", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::OneUp, "OneUp", N_("1up"), EventCategory::Collectible, N_("Extra life, a full carrot in cooperation"),
				"Collectible/OneUp", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::FastFire, "FastFire", N_("Fast Fire"), EventCategory::Collectible, N_("Increases the fire rate"),
				"Collectible/FastFireJazz", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::Food, "Food", N_("Food"), EventCategory::Collectible, N_("Counts towards the sugar rush"),
				FoodFields, "Collectible/FoodApple", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, FoodPreview),
			DescribeEvent(EventType::Ammo, "Ammo", N_("Ammo"), EventCategory::Collectible, N_("Gives 3 ammo of the weapon"),
				AmmoFields, "Collectible/AmmoBouncer", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, AmmoPreview),
			DescribeEvent(EventType::PowerUpWeapon, "PowerUpWeapon", N_("Weapon Power-up"), EventCategory::Container, N_("Monitor that upgrades a weapon when it's broken"),
				PowerUpWeaponFields, "Object/PowerUp/Blaster", std::uint32_t(1) /* PlayerType::Jazz */, EventTraits::Object)
				.WithPreviewCases(0, PowerUpWeaponPreview),

			DescribeEvent(EventType::Crate, "Crate", N_("Crate"), EventCategory::Container, N_("Wooden crate that releases another event when it's broken"),
				ContainerFields, "Object/Crate/Generic", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::Barrel, "Barrel", N_("Barrel"), EventCategory::Container, N_("Barrel that releases another event when it's broken"),
				ContainerFields, "Object/BarrelContainer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::CrateAmmo, "CrateAmmo", N_("Ammo Crate"), EventCategory::Container, N_("Wooden crate that releases ammo when it's broken"),
				AmmoContainerFields, "Object/Crate/Generic", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(0, CrateAmmoPreview),
			DescribeEvent(EventType::BarrelAmmo, "BarrelAmmo", N_("Ammo Barrel"), EventCategory::Container, N_("Barrel that releases ammo when it's broken"),
				AmmoContainerFields, "Object/BarrelContainer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::CrateGem, "CrateGem", N_("Gem Crate"), EventCategory::Container, N_("Wooden crate that releases gems when it's broken"),
				GemContainerFields, "Object/Crate/Generic", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BarrelGem, "BarrelGem", N_("Gem Barrel"), EventCategory::Container, N_("Barrel that releases gems when it's broken"),
				GemContainerFields, "Object/BarrelContainer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::PowerUpMorph, "PowerUpMorph", N_("Morph Monitor"), EventCategory::Container, N_("Monitor that changes the character, or turns the player into a bird, when it's broken"),
				PowerUpMorphFields, "Object/PowerUp/Swap2", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, PowerUpMorphPreview),
			DescribeEvent(EventType::PowerUpShield, "PowerUpShield", N_("Shield Monitor"), EventCategory::Container, N_("Monitor that gives a shield for 30 seconds when it's broken"),
				PowerUpShieldFields, "Object/PowerUp/ShieldFire", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, PowerUpShieldPreview),

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
