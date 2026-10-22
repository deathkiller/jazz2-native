#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"

namespace Jazz2::Editor::EventCatalogParts
{
	namespace
	{
		// Values of WeaponType
		constexpr ParamOption WeaponTypes[] = {
			// TRANSLATORS: Choices of a parameter of an event (weapons)
			{ NC_("editor", "Blaster"), 0 }, { NC_("editor", "Bouncer"), 1 }, { NC_("editor", "Freezer"), 2 }, { NC_("editor", "Seeker"), 3 }, { NC_("editor", "RF"), 4 },
			// TRANSLATORS: Choices of a parameter of an event (weapons)
			{ NC_("editor", "Toaster"), 5 }, { NC_("editor", "TNT"), 6 }, { NC_("editor", "Pepper"), 7 }, { NC_("editor", "Electro"), 8 }, { NC_("editor", "Thunderbolt"), 9 }
		};
		// Blaster has no ammo, so it's left out
		constexpr ParamOption AmmoTypes[] = {
			// TRANSLATORS: Choices of a parameter of an event (weapons)
			{ NC_("editor", "Bouncer"), 1 }, { NC_("editor", "Freezer"), 2 }, { NC_("editor", "Seeker"), 3 }, { NC_("editor", "RF"), 4 }, { NC_("editor", "Toaster"), 5 },
			// TRANSLATORS: Choices of a parameter of an event (weapons)
			{ NC_("editor", "TNT"), 6 }, { NC_("editor", "Pepper"), 7 }, { NC_("editor", "Electro"), 8 }, { NC_("editor", "Thunderbolt"), 9 }
		};
		// Blaster (0) means ammo of a random weapon the players already have
		constexpr ParamOption AmmoContainerTypes[] = {
			// TRANSLATORS: Choices of a parameter of an event (weapons, or a random one the players have)
			{ NC_("editor", "Random"), 0 }, { NC_("editor", "Bouncer"), 1 }, { NC_("editor", "Freezer"), 2 }, { NC_("editor", "Seeker"), 3 }, { NC_("editor", "RF"), 4 },
			// TRANSLATORS: Choices of a parameter of an event (weapons)
			{ NC_("editor", "Toaster"), 5 }, { NC_("editor", "TNT"), 6 }, { NC_("editor", "Pepper"), 7 }, { NC_("editor", "Electro"), 8 }, { NC_("editor", "Thunderbolt"), 9 }
		};

		// TRANSLATORS: Choices of a parameter of an event (coin metals)
		constexpr ParamOption CoinTypes[] = { { NC_("editor", "Silver"), 0 }, { NC_("editor", "Gold"), 1 } };
		constexpr EventParamField CoinFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Type"), 0, 8, CoinTypes).WithDescription(NC_("editor", "Silver is worth 1 coin, gold 5 coins")),
		};
		// CoinCollectible uses the type as the animation, Coins.res has only silver (0) and gold (1)
		constexpr EventPreviewCase CoinPreview[] = {
			{ 0, "Collectible/Coins", 0 }, { 1, "Collectible/Coins", 1 }
		};

		// TRANSLATORS: Choices of a parameter of an event (gem colors)
		constexpr ParamOption GemColors[] = { { NC_("editor", "Red"), 0 }, { NC_("editor", "Green"), 1 }, { NC_("editor", "Blue"), 2 }, { NC_("editor", "Purple"), 3 } };
		constexpr EventParamField GemFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Color"), 0, 2, GemColors).WithDescription(NC_("editor", "Red is worth 1 gem, green 5, blue 10 and purple 1")),
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(NC_("editor", "Disappears"), 1, 0).WithDescription(NC_("editor", "Disappears 8 seconds after it's knocked loose")),
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(NC_("editor", "Flipped"), 1, 1).WithDescription(NC_("editor", "Faces the other way"))
				.MirroredX(MirrorMode::Toggle),
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(NC_("editor", "Delayed"), 1, 2).WithDescription(NC_("editor", "Can't be collected for the first 40 frames")),
		};

		constexpr EventParamField GemRingFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Length"), 0, 8, 0, 255, 8).WithDescription(NC_("editor", "Number of red gems in the ring, 0 means 8")),
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Speed"), 1, 8, 0, 255, 8).WithDescription(NC_("editor", "Speed of rotation, 0 means 8")),
		};

		constexpr EventParamField CarrotFields[] = {
			// TRANSLATORS: Parameter of a carrot event, a checkbox for a full carrot that restores all health
			BoolField(NC_("editor", "Full"), 0).WithDescription(NC_("editor", "Restores full health instead of one heart")),
		};
		constexpr EventPreviewCase CarrotPreview[] = {
			{ 0, "Collectible/Carrot", std::uint32_t(AnimState::Default) },
			{ EventPreviewCase::AnyValue, "Collectible/CarrotFull", std::uint32_t(AnimState::Default) }
		};

		// Values of FoodType
		constexpr ParamOption FoodTypes[] = {
			// TRANSLATORS: Choices of a parameter of an event (kinds of food)
			{ NC_("editor", "Apple"), 1 }, { NC_("editor", "Banana"), 2 }, { NC_("editor", "Cherry"), 3 }, { NC_("editor", "Orange"), 4 }, { NC_("editor", "Pear"), 5 },
			// TRANSLATORS: Choices of a parameter of an event (kinds of food), "Thing" is the name of one of them
			{ NC_("editor", "Pretzel"), 6 }, { NC_("editor", "Strawberry"), 7 }, { NC_("editor", "Lemon"), 8 }, { NC_("editor", "Lime"), 9 }, { NC_("editor", "Thing"), 10 },
			{ NC_("editor", "Watermelon"), 11 }, { NC_("editor", "Peach"), 12 }, { NC_("editor", "Grapes"), 13 }, { NC_("editor", "Lettuce"), 14 }, { NC_("editor", "Eggplant"), 15 },
			// TRANSLATORS: Choices of a parameter of an event (kinds of food), Pepsi and Coke are drinks
			{ NC_("editor", "Cucumber"), 16 }, { NC_("editor", "Pepsi"), 17 }, { NC_("editor", "Coke"), 18 }, { NC_("editor", "Milk"), 19 }, { NC_("editor", "Pie"), 20 },
			// TRANSLATORS: Choices of a parameter of an event (kinds of food), "Chips" are potato chips (crisps)
			{ NC_("editor", "Cake"), 21 }, { NC_("editor", "Donut"), 22 }, { NC_("editor", "Cupcake"), 23 }, { NC_("editor", "Chips"), 24 }, { NC_("editor", "Candy"), 25 },
			{ NC_("editor", "Chocolate"), 26 }, { NC_("editor", "Ice Cream"), 27 }, { NC_("editor", "Burger"), 28 }, { NC_("editor", "Pizza"), 29 }, { NC_("editor", "Fries"), 30 },
			{ NC_("editor", "Chicken Leg"), 31 }, { NC_("editor", "Sandwich"), 32 }, { NC_("editor", "Taco"), 33 }, { NC_("editor", "Hot Dog"), 34 }, { NC_("editor", "Ham"), 35 },
			{ NC_("editor", "Cheese"), 36 }
		};
		constexpr EventParamField FoodFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Type"), 0, 8, FoodTypes, 1).WithDescription(NC_("editor", "Pepsi, Coke and Milk are drinks")),
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
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Weapon"), 0, 8, AmmoTypes, 1),
		};
		// AmmoCollectible::OnActivatedAsync(), by WeaponType; the upgraded ammo (animation 1) is shown only when the first
		// local player has the upgrade
		constexpr EventPreviewCase AmmoPreview[] = {
			{ 1, "Collectible/AmmoBouncer", 0 }, { 2, "Collectible/AmmoFreezer", 0 }, { 3, "Collectible/AmmoSeeker", 0 },
			{ 4, "Collectible/AmmoRF", 0 }, { 5, "Collectible/AmmoToaster", 0 }, { 6, "Collectible/AmmoTNT", 0 },
			{ 7, "Collectible/AmmoPepper", 0 }, { 8, "Collectible/AmmoElectro", 0 }, { 9, "Collectible/AmmoThunderbolt", 0 }
		};

		constexpr EventParamField PowerUpWeaponFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Weapon"), 0, 8, WeaponTypes).WithDescription(NC_("editor", "Gives 25 ammo and the upgrade")),
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
			// TRANSLATORS: Parameter of a crate or a barrel event, the event inside of it
			EventTypeField(NC_("editor", "Content"), 0).WithDescription(NC_("editor", "Event spawned when it breaks, bytes 3 to 15 are passed to it as its parameters")),
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Count"), 2, 8, 0, 255, 1),
			// TRANSLATORS: Parameter of a crate or a barrel event, the parameters of the event inside of it
			ContentsField(NC_("editor", "Parameters of the content"), 3, 13, 0)
				.WithDescription(NC_("editor", "Parameters of the event spawned when it breaks")),
		};

		constexpr EventParamField AmmoContainerFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Weapon"), 0, 8, AmmoContainerTypes).WithDescription(NC_("editor", "Releases 5 ammo of the weapon, Random releases 4 to 6 ammo of the weapons the players have")),
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
			// TRANSLATORS: Parameters of a gem crate or barrel event, the number of gems of each color
			UIntField(NC_("editor", "Red"), 0, 8, 0, 255, 5),
			UIntField(NC_("editor", "Green"), 1, 8, 0, 255),
			UIntField(NC_("editor", "Blue"), 2, 8, 0, 255),
			UIntField(NC_("editor", "Purple"), 3, 8, 0, 255),
		};

		// Values of PowerUpMorphMonitor::MorphType
		// TRANSLATORS: Choices of a parameter of an event (characters a morph monitor switches between, or a bird)
		constexpr ParamOption MorphTypes[] = { { NC_("editor", "Jazz/Spaz"), 0 }, { NC_("editor", "Jazz/Spaz/Lori"), 1 }, { NC_("editor", "Bird"), 2 } };
		constexpr EventParamField PowerUpMorphFields[] = {
			// TRANSLATORS: Parameter of a morph monitor event, what it turns the player into
			EnumField(NC_("editor", "Morph"), 0, 8, MorphTypes).WithDescription(NC_("editor", "Switches to the next character, Bird turns the player into a flying bird that shoots until a Fly Off or Revert Morph area")),
		};
		// PowerUpMorphMonitor::OnActivatedAsync(), by PowerUpMorphMonitor::MorphType
		constexpr EventPreviewCase PowerUpMorphPreview[] = {
			{ 0, "Object/PowerUp/Swap2", MonitorAnimation }, { 1, "Object/PowerUp/Swap3", MonitorAnimation }, { 2, "Object/PowerUp/Bird", MonitorAnimation },
			{ EventPreviewCase::AnyValue, "Object/PowerUp/Empty", MonitorAnimation }
		};

		// Values of ShieldType
		// TRANSLATORS: Choices of a parameter of an event (kinds of shields)
		constexpr ParamOption ShieldTypes[] = { { NC_("editor", "Fire"), 1 }, { NC_("editor", "Water"), 2 }, { NC_("editor", "Lightning"), 3 }, { NC_("editor", "Laser"), 4 } };
		constexpr EventParamField PowerUpShieldFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Shield"), 0, 8, ShieldTypes, 1),
		};
		// PowerUpShieldMonitor::OnActivatedAsync(), by ShieldType
		constexpr EventPreviewCase PowerUpShieldPreview[] = {
			{ 1, "Object/PowerUp/ShieldFire", MonitorAnimation }, { 2, "Object/PowerUp/ShieldWater", MonitorAnimation },
			{ 3, "Object/PowerUp/ShieldLightning", MonitorAnimation }, { 4, "Object/PowerUp/ShieldLaser", MonitorAnimation },
			{ EventPreviewCase::AnyValue, "Object/PowerUp/Empty", MonitorAnimation }
		};

		constexpr EventParamField RaceCheckpointFields[] = {
			// TRANSLATORS: Parameter of a race waypoint event, its position in the sequence of waypoints
			UIntField(NC_("editor", "Order"), 0, 8, 0, 255).WithDescription(NC_("editor", "Waypoints are connected in ascending order, the ones with the same order and group are merged into their center")),
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Group"), 1, 8, 0, 255).WithDescription(NC_("editor", "Split of the track, each group is a separate route")),
		};

		constexpr EventParamField PassthroughEventFields[] = {
			// TRANSLATORS: Parameter of an event of the original game, its number there
			UIntField(NC_("editor", "Event"), 0, 8, 1, 255, 1).WithDescription(NC_("editor", "Number of the event in the original game")),
			// TRANSLATORS: Parameter of an event of the original game, all of its parameters as one number
			UIntField(NC_("editor", "Parameters"), 1, 20, 0, 0xFFFFF).WithDescription(NC_("editor", "Parameters of the event in the original game, as one number")),
		};

		constexpr EventDescriptor Events[] = {
			// TRANSLATORS: Name of an event, a collectible coin
			DescribeEvent(EventType::Coin, "Coin", NC_("editor", "Coin"), EventCategory::Collectible, nullptr,
				CoinFields, "Collectible/Coins", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, CoinPreview),
			// TRANSLATORS: Name of an event, a collectible gem
			DescribeEvent(EventType::Gem, "Gem", NC_("editor", "Gem"), EventCategory::Collectible, nullptr,
				GemFields, "Collectible/Gems", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a big gem that breaks into small ones
			DescribeEvent(EventType::GemGiant, "GemGiant", NC_("editor", "Giant Gem"), EventCategory::Collectible, NC_("editor", "Breaks into 5 to 12 red gems when it's shot or attacked"),
				"Object/GemGiant", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a rotating ring of gems
			DescribeEvent(EventType::GemRing, "GemRing", NC_("editor", "Gem Ring"), EventCategory::Collectible, NC_("editor", "Rotating ring of red gems collected all at once"),
				GemRingFields, "Collectible/Gems", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a tile that releases gems when a player buttstomps onto it
			DescribeEvent(EventType::GemStomp, "GemStomp", NC_("editor", "Gem Stomp"), EventCategory::Collectible, NC_("editor", "Releases 8 red gems when a player buttstomps onto this tile, only once"),
				nullptr, std::uint32_t(AnimState::Default), EventTraits::Area),
			// TRANSLATORS: Name of an event, a collectible carrot that restores health
			DescribeEvent(EventType::Carrot, "Carrot", NC_("editor", "Carrot"), EventCategory::Collectible, NC_("editor", "Restores health"),
				CarrotFields, "Collectible/Carrot", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, CarrotPreview),
			// TRANSLATORS: Name of an event, a carrot that lets the player fly
			DescribeEvent(EventType::CarrotFly, "CarrotFly", NC_("editor", "Fly Carrot"), EventCategory::Collectible, NC_("editor", "Gives the copter ears"),
				"Collectible/CarrotFly", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a carrot that makes the player invincible
			DescribeEvent(EventType::CarrotInvincible, "CarrotInvincible", NC_("editor", "Invincibility Carrot"), EventCategory::Collectible, NC_("editor", "Makes the player invincible for 30 seconds"),
				"Collectible/CarrotInvincible", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, an extra life
			DescribeEvent(EventType::OneUp, "OneUp", NC_("editor", "1up"), EventCategory::Collectible, NC_("editor", "Extra life, a full carrot in cooperation"),
				"Collectible/OneUp", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a collectible that increases the fire rate
			DescribeEvent(EventType::FastFire, "FastFire", NC_("editor", "Fast Fire"), EventCategory::Collectible, NC_("editor", "Increases the fire rate"),
				"Collectible/FastFireJazz", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a collectible piece of food
			DescribeEvent(EventType::Food, "Food", NC_("editor", "Food"), EventCategory::Collectible, NC_("editor", "Counts towards the sugar rush"),
				FoodFields, "Collectible/FoodApple", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, FoodPreview),
			// TRANSLATORS: Name of an event, collectible ammunition of a weapon
			DescribeEvent(EventType::Ammo, "Ammo", NC_("editor", "Ammo"), EventCategory::Collectible, NC_("editor", "Gives 3 ammo of the weapon"),
				AmmoFields, "Collectible/AmmoBouncer", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, AmmoPreview),
			// TRANSLATORS: Name of an event, a monitor that upgrades a weapon
			DescribeEvent(EventType::PowerUpWeapon, "PowerUpWeapon", NC_("editor", "Weapon Power-up"), EventCategory::Container, NC_("editor", "Monitor that upgrades a weapon when it's broken"),
				PowerUpWeaponFields, "Object/PowerUp/Blaster", std::uint32_t(1) /* PlayerType::Jazz */, EventTraits::Object)
				.WithPreviewCases(0, PowerUpWeaponPreview),

			// TRANSLATORS: Name of an event, a breakable wooden crate
			DescribeEvent(EventType::Crate, "Crate", NC_("editor", "Crate"), EventCategory::Container, NC_("editor", "Wooden crate that releases another event when it's broken"),
				ContainerFields, "Object/Crate/Generic", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a breakable barrel
			DescribeEvent(EventType::Barrel, "Barrel", NC_("editor", "Barrel"), EventCategory::Container, NC_("editor", "Barrel that releases another event when it's broken"),
				ContainerFields, "Object/BarrelContainer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a breakable wooden crate with ammunition
			DescribeEvent(EventType::CrateAmmo, "CrateAmmo", NC_("editor", "Ammo Crate"), EventCategory::Container, NC_("editor", "Wooden crate that releases ammo when it's broken"),
				AmmoContainerFields, "Object/Crate/Generic", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(0, CrateAmmoPreview),
			// TRANSLATORS: Name of an event, a breakable barrel with ammunition
			DescribeEvent(EventType::BarrelAmmo, "BarrelAmmo", NC_("editor", "Ammo Barrel"), EventCategory::Container, NC_("editor", "Barrel that releases ammo when it's broken"),
				AmmoContainerFields, "Object/BarrelContainer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a breakable wooden crate with gems
			DescribeEvent(EventType::CrateGem, "CrateGem", NC_("editor", "Gem Crate"), EventCategory::Container, NC_("editor", "Wooden crate that releases gems when it's broken"),
				GemContainerFields, "Object/Crate/Generic", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a breakable barrel with gems
			DescribeEvent(EventType::BarrelGem, "BarrelGem", NC_("editor", "Gem Barrel"), EventCategory::Container, NC_("editor", "Barrel that releases gems when it's broken"),
				GemContainerFields, "Object/BarrelContainer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a monitor that changes the character
			DescribeEvent(EventType::PowerUpMorph, "PowerUpMorph", NC_("editor", "Morph Monitor"), EventCategory::Container, NC_("editor", "Monitor that changes the character, or turns the player into a bird, when it's broken"),
				PowerUpMorphFields, "Object/PowerUp/Swap2", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, PowerUpMorphPreview),
			// TRANSLATORS: Name of an event, a monitor that gives a shield
			DescribeEvent(EventType::PowerUpShield, "PowerUpShield", NC_("editor", "Shield Monitor"), EventCategory::Container, NC_("editor", "Monitor that gives a shield for 30 seconds when it's broken"),
				PowerUpShieldFields, "Object/PowerUp/ShieldFire", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, PowerUpShieldPreview),

			// TRANSLATORS: Name of an event, a point of the track in the race mode
			DescribeEvent(EventType::RaceCheckpoint, "RaceCheckpoint", NC_("editor", "Race Waypoint"), EventCategory::Other, NC_("editor", "Ordered waypoint of the track for the minimap and the race ranking"),
				RaceCheckpointFields, nullptr, std::uint32_t(AnimState::Default), EventTraits::MultiplayerOnly),

			// TRANSLATORS: Name of an event, one of the original game that this game doesn't use
			DescribeEvent(EventType::PassthroughEvent, "PassthroughEvent", NC_("editor", "Passthrough Event"), EventCategory::Other,
				NC_("editor", "Event of the original game that the game doesn't use, kept for the scripts of the level, which read the events of the level"),
				PassthroughEventFields, nullptr, std::uint32_t(AnimState::Default), EventTraits::Unused),
		};
	}

	ArrayView<const EventDescriptor> GetItemEvents()
	{
		return Events;
	}
}

#endif
