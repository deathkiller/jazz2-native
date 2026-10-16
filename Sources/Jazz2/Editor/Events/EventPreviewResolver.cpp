#include "EventPreviewResolver.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"
#include "../../Direction.h"
#include "../../EventType.h"
#include "../../PlayerType.h"
#include "../../WeaponType.h"

namespace Jazz2::Editor
{
	namespace
	{
		// FoodCollectible::OnActivatedAsync(), indexed by FoodType
		constexpr const char* FoodMetadata[] = {
			nullptr,
			"Collectible/FoodApple", "Collectible/FoodBanana", "Collectible/FoodCherry", "Collectible/FoodOrange",
			"Collectible/FoodPear", "Collectible/FoodPretzel", "Collectible/FoodStrawberry", "Collectible/FoodLemon",
			"Collectible/FoodLime", "Collectible/FoodThing", "Collectible/FoodWaterMelon", "Collectible/FoodPeach",
			"Collectible/FoodGrapes", "Collectible/FoodLettuce", "Collectible/FoodEggplant", "Collectible/FoodCucumber",
			"Collectible/FoodPepsi", "Collectible/FoodCoke", "Collectible/FoodMilk", "Collectible/FoodPie",
			"Collectible/FoodCake", "Collectible/FoodDonut", "Collectible/FoodCupcake", "Collectible/FoodChips",
			"Collectible/FoodCandy", "Collectible/FoodChocolate", "Collectible/FoodIceCream", "Collectible/FoodBurger",
			"Collectible/FoodPizza", "Collectible/FoodFries", "Collectible/FoodChickenLeg", "Collectible/FoodSandwich",
			"Collectible/FoodTaco", "Collectible/FoodHotDog", "Collectible/FoodHam", "Collectible/FoodCheese"
		};

		// AmmoCollectible::OnActivatedAsync(), indexed by WeaponType, Blaster has no ammo
		constexpr const char* AmmoMetadata[] = {
			nullptr,
			"Collectible/AmmoBouncer", "Collectible/AmmoFreezer", "Collectible/AmmoSeeker", "Collectible/AmmoRF",
			"Collectible/AmmoToaster", "Collectible/AmmoTNT", "Collectible/AmmoPepper", "Collectible/AmmoElectro",
			"Collectible/AmmoThunderbolt"
		};

		// AmmoCrate::OnActivatedAsync(), indexed by WeaponType, Blaster and Thunderbolt use the generic crate
		constexpr const char* AmmoCrateMetadata[] = {
			"Object/Crate/Generic",
			"Object/Crate/AmmoBouncer", "Object/Crate/AmmoFreezer", "Object/Crate/AmmoSeeker", "Object/Crate/AmmoRF",
			"Object/Crate/AmmoToaster", "Object/Crate/AmmoTNT", "Object/Crate/AmmoPepper", "Object/Crate/AmmoElectro"
		};

		// PowerUpWeaponMonitor::OnActivatedAsync(), indexed by WeaponType, TNT and Thunderbolt show an empty monitor
		constexpr const char* WeaponMonitorMetadata[] = {
			"Object/PowerUp/Blaster", "Object/PowerUp/Bouncer", "Object/PowerUp/Freezer", "Object/PowerUp/Seeker",
			"Object/PowerUp/RF", "Object/PowerUp/Toaster", "Object/PowerUp/Empty", "Object/PowerUp/Pepper",
			"Object/PowerUp/Electro", "Object/PowerUp/Empty"
		};

		// PowerUpShieldMonitor::OnActivatedAsync(), indexed by ShieldType
		constexpr const char* ShieldMonitorMetadata[] = {
			"Object/PowerUp/Empty",
			"Object/PowerUp/ShieldFire", "Object/PowerUp/ShieldWater", "Object/PowerUp/ShieldLightning", "Object/PowerUp/ShieldLaser"
		};

		// PowerUpMorphMonitor::OnActivatedAsync(), indexed by PowerUpMorphMonitor::MorphType
		constexpr const char* MorphMonitorMetadata[] = {
			"Object/PowerUp/Swap2", "Object/PowerUp/Swap3", "Object/PowerUp/Bird"
		};

		// Bridge::OnActivatedAsync(), indexed by Bridge::BridgeType
		constexpr const char* BridgeMetadata[] = {
			"Bridge/Rope", "Bridge/Stone", "Bridge/Vine", "Bridge/StoneRed", "Bridge/Log", "Bridge/Gem", "Bridge/Lab"
		};

		// MovingPlatform::OnActivatedAsync(), indexed by MovingPlatform::PlatformType
		constexpr const char* PlatformMetadata[] = {
			nullptr,
			"MovingPlatform/CarrotusFruit", "MovingPlatform/Ball", "MovingPlatform/CarrotusGrass", "MovingPlatform/Lab",
			"MovingPlatform/Sonic", "MovingPlatform/Spike", "MovingPlatform/SpikeBall"
		};

		// Pole::OnActivatedAsync(), indexed by the theme
		constexpr const char* PoleMetadata[] = {
			"Pole/Carrotus", "Pole/Diamondus", "Pole/DiamondusTree", "Pole/Jungle", "Pole/Psych"
		};

		// Multiplayer/Teams.h, MaxTeamCount, that header exists only with WITH_MULTIPLAYER
		constexpr std::uint8_t CtfTeamCount = 4;

		template<std::size_t N>
		const char* FromTable(const char* const (&table)[N], std::uint32_t index)
		{
			return (index < N ? table[index] : nullptr);
		}

		// EventParamsReader::GetInt16()
		std::int16_t ReadInt16(const std::uint8_t* params, std::int32_t offset)
		{
			return std::int16_t(std::uint16_t(params[offset]) | (std::uint16_t(params[offset + 1]) << 8));
		}
	}

	bool ResolveEventPreview(const Formats::EventRecord& event, EventPreviewSpec& spec)
	{
		const std::uint8_t* p = event.Params;
		switch (EventType(event.Type)) {
			// Collectibles, they also face left on every other tile by the parity of their position, which the
			// record doesn't know (CollectibleBase::SetFacingDirection())
			case EventType::Food: {
				const char* metadata = FromTable(FoodMetadata, p[0]);
				if (metadata == nullptr) {
					return false;
				}
				spec.Metadata = metadata;
				spec.Animation = std::uint32_t(AnimState::Default);
				return true;
			}
			case EventType::Ammo: {
				// Upgraded ammo (animation 1) is shown only when the first local player has the upgrade
				const char* metadata = FromTable(AmmoMetadata, p[0]);
				if (metadata == nullptr) {
					return false;
				}
				spec.Metadata = metadata;
				spec.Animation = 0;
				return true;
			}
			case EventType::Carrot: {
				spec.Metadata = (p[0] != 0 ? "Collectible/CarrotFull" : "Collectible/Carrot");
				spec.Animation = std::uint32_t(AnimState::Default);
				return true;
			}
			case EventType::Coin: {
				// CoinCollectible uses the type as the animation, Coins.res has only silver (0) and gold (1)
				if (p[0] > 1) {
					return false;
				}
				spec.Metadata = "Collectible/Coins";
				spec.Animation = p[0];
				return true;
			}
			case EventType::Gem: {
				// GemCollectible, each color is its own animation with its own palette offset in Gems.res
				spec.Metadata = "Collectible/Gems";
				spec.Animation = std::uint32_t(p[0] & 0x03);
				spec.FlipX = ((p[1] & 0x02) != 0);
				return true;
			}

			// Containers
			case EventType::PowerUpWeapon: {
				const char* metadata = FromTable(WeaponMonitorMetadata, p[0]);
				spec.Metadata = (metadata != nullptr ? metadata : "Object/PowerUp/Empty");
				// The blaster monitor shows the first local player's character, Jazz without one
				spec.Animation = (WeaponType(p[0]) == WeaponType::Blaster ? std::uint32_t(PlayerType::Jazz) : std::uint32_t(AnimState::Default));
				return true;
			}
			case EventType::PowerUpShield: {
				const char* metadata = FromTable(ShieldMonitorMetadata, p[0]);
				spec.Metadata = (metadata != nullptr ? metadata : "Object/PowerUp/Empty");
				spec.Animation = std::uint32_t(AnimState::Default);
				return true;
			}
			case EventType::PowerUpMorph: {
				const char* metadata = FromTable(MorphMonitorMetadata, p[0]);
				spec.Metadata = (metadata != nullptr ? metadata : "Object/PowerUp/Empty");
				spec.Animation = std::uint32_t(AnimState::Default);
				return true;
			}
			case EventType::CrateAmmo: {
				const char* metadata = FromTable(AmmoCrateMetadata, p[0]);
				spec.Metadata = (metadata != nullptr ? metadata : "Object/Crate/Generic");
				spec.Animation = std::uint32_t(AnimState::Idle);
				return true;
			}

			// Basic, warps and environment
			case EventType::Checkpoint: {
				spec.Metadata = (p[0] == 1 ? "Object/CheckpointXmas" : "Object/Checkpoint");
				spec.Animation = (p[1] != 0 ? 1 : 0);
				return true;
			}
			case EventType::WarpCoinBonus: {
				// BonusWarp has a sign only for these costs, anything else shows the plain one
				std::uint8_t cost = p[3];
				spec.Metadata = "Object/BonusWarp";
				spec.Animation = (cost == 10 || cost == 20 || cost == 50 || cost == 100 ? cost : std::uint32_t(AnimState::Default));
				return true;
			}
			case EventType::Spring: {
				// Spring::OnActivatedAsync(), only red, green and blue have their animations
				std::uint8_t type = p[0];
				if (type > 2) {
					return false;
				}
				// Spring::Orientation is Bottom (launches up), Right, Top (launches down) and Left, anything above is
				// turned away from a wall at runtime, here the tile to the right is taken as empty
				std::uint8_t orientation = p[1];
				if (orientation > 3) {
					orientation = (orientation == 5 ? 3 : 1);
				}
				std::uint32_t orientationBit = 0;
				switch (orientation) {
					// Placed 8 pixels into the tile, but it falls down from there (it has gravity in most levels), so it's
					// shown standing on the bottom of the tile, as it's seen in the game
					case 0: spec.Offset = Vector2f(0.0f, 8.0f); break;
					case 1: orientationBit = 1; break;
					case 2: spec.Offset = Vector2f(0.0f, -8.0f); orientationBit = 2; break;
					case 3: orientationBit = 1; spec.FlipX = true; break;
				}
				spec.Metadata = "Object/Spring";
				spec.Animation = ((std::uint32_t(type) + 1) << 10) | (orientationBit << 12);
				return true;
			}
			case EventType::Bridge: {
				const char* metadata = FromTable(BridgeMetadata, p[2]);
				spec.Metadata = (metadata != nullptr ? metadata : "Bridge/Rope");
				spec.Animation = std::uint32_t(AnimState::Default);
				spec.Offset = Vector2f(0.0f, -6.0f);
				return true;
			}
			case EventType::MovingPlatform: {
				// The platform is moved to the end of its chain on its first update, not when it's activated
				const char* metadata = FromTable(PlatformMetadata, p[0]);
				spec.Metadata = (metadata != nullptr ? metadata : "MovingPlatform/CarrotusFruit");
				spec.Animation = 0;
				return true;
			}
			case EventType::PushableBox: {
				spec.Metadata = (p[0] == 1 ? "Object/PushBoxCrate" : "Object/PushBoxRock");
				spec.Animation = std::uint32_t(AnimState::Default);
				return true;
			}
			case EventType::Pole: {
				const char* metadata = FromTable(PoleMetadata, p[0]);
				spec.Metadata = (metadata != nullptr ? metadata : "Pole/Carrotus");
				spec.Animation = std::uint32_t(AnimState::Default);
				spec.Offset = Vector2f(float(ReadInt16(p, 1)), float(ReadInt16(p, 3)));
				return true;
			}
			case EventType::Moth: {
				// Moth uses the color as the animation, Moth.res has 4 of them
				if (p[0] > 3) {
					return false;
				}
				spec.Metadata = "Object/Moth";
				spec.Animation = p[0];
				return true;
			}
			case EventType::Bomb: {
				const char* metadata;
				switch (p[0]) {
					case 0: metadata = "Object/Bomb"; break;
					case 1: metadata = "Enemy/LizardFloat"; break;
					case 2: metadata = "Enemy/LizardFloatXmas"; break;
					default: return false;
				}
				spec.Metadata = metadata;
				spec.Animation = 2;
				spec.FlipX = (p[1] != 0);
				return true;
			}
			case EventType::PinballBumper: {
				// PinballBumper uses the look as the animation, PinballBumper.res has 2 of them
				if (p[0] > 1) {
					return false;
				}
				spec.Metadata = "Object/PinballBumper";
				spec.Animation = p[0];
				return true;
			}
			case EventType::PinballPaddle: {
				spec.Metadata = "Object/PinballPaddle";
				spec.Animation = std::uint32_t(AnimState::Idle);
				spec.FlipX = (p[0] != 0);
				return true;
			}
			case EventType::CtfBase: {
				// MpLevelHandler::BuildCtfBases() ignores the base of an unknown team, and spawns the others at the
				// left edge of the tile, 8 px below its center
				if (p[0] >= CtfTeamCount) {
					return false;
				}
				spec.Metadata = "Object/CtfBase";
				spec.Animation = p[0];
				spec.Offset = Vector2f(-16.0f, 8.0f);
				return true;
			}
			case EventType::BirdCage: {
				const char* metadata;
				switch (p[0]) {
					case 0: metadata = "Object/BirdCageChuck"; break;
					case 1: metadata = "Object/BirdCageBirdy"; break;
					default: return false;
				}
				spec.Metadata = metadata;
				spec.Animation = std::uint32_t(p[1] != 0 ? AnimState::Activated : AnimState::Idle);
				return true;
			}
			case EventType::SteamNote: {
				spec.Metadata = "Object/SteamNote";
				spec.Animation = std::uint32_t(AnimState::Default);
				spec.Offset = Vector2f(-10.0f, 0.0f);
				return true;
			}

			// Enemies
			case EventType::EnemyTurtle: {
				spec.Metadata = (p[0] == 1 ? "Enemy/TurtleXmas" : "Enemy/Turtle");
				spec.Animation = std::uint32_t(AnimState::Walk);
				return true;
			}
			case EventType::EnemyLizard: {
				// Lizard faces the given way only when it's falling, otherwise randomly
				spec.Metadata = (p[0] == 1 ? "Enemy/LizardXmas" : "Enemy/Lizard");
				spec.Animation = std::uint32_t(AnimState::Walk);
				spec.FlipX = (p[1] != 0 && p[2] != 0);
				return true;
			}
			case EventType::EnemySucker: {
				// Sucker popped off a floating sucker starts with its deflating transition (Sucker.res)
				spec.Metadata = "Enemy/Sucker";
				if (Direction(p[0]) != Direction::None) {
					spec.Animation = 0x40000000;
					spec.FlipX = (Direction(p[0]) == Direction::Left);
				} else {
					spec.Animation = std::uint32_t(AnimState::Walk);
				}
				return true;
			}
			case EventType::EnemyMonkey: {
				spec.Metadata = "Enemy/Monkey";
				spec.Animation = std::uint32_t(p[0] != 0 ? AnimState::Walk : AnimState::Jump);
				return true;
			}
			case EventType::EnemyDoggy: {
				spec.Metadata = (p[0] == 1 ? "Enemy/Cat" : "Enemy/Doggy");
				spec.Animation = std::uint32_t(AnimState::Walk);
				return true;
			}
			case EventType::EnemyLizardFloat: {
				spec.Metadata = (p[0] == 1 ? "Enemy/LizardFloatXmas" : "Enemy/LizardFloat");
				spec.Animation = std::uint32_t(AnimState::Idle);
				return true;
			}
			case EventType::EnemyDragon: {
				// Dragon moves itself down so it's not stuck in the ceiling
				spec.Metadata = "Enemy/Dragon";
				spec.Animation = std::uint32_t(AnimState::Idle);
				spec.Offset = Vector2f(0.0f, 8.0f);
				return true;
			}
			case EventType::EnemyFencer: {
				spec.Metadata = "Enemy/Fencer";
				spec.Animation = std::uint32_t(AnimState::Idle);
				spec.FlipX = true;
				return true;
			}
			case EventType::EnemyCaterpillar: {
				spec.Metadata = "Enemy/Caterpillar";
				spec.Animation = std::uint32_t(AnimState::Idle);
				spec.FlipX = true;
				return true;
			}
			case EventType::EnemySparks: {
				spec.Metadata = "Enemy/Sparks";
				spec.Animation = std::uint32_t(AnimState::Idle);
				spec.FlipX = true;
				return true;
			}
			case EventType::TurtleShell: {
				const char* metadata;
				switch (p[8]) {
					default:
					case 0: metadata = "Enemy/TurtleShell"; break;
					case 1: metadata = "Enemy/TurtleShellXmas"; break;
					case 2: metadata = "Boss/TurtleBossShell"; break;
				}
				spec.Metadata = metadata;
				spec.Animation = std::uint32_t(AnimState::Default);
				return true;
			}

			// Bosses
			case EventType::BossBilsy: {
				spec.Metadata = (p[0] == 1 ? "Boss/BilsyXmas" : "Boss/Bilsy");
				spec.Animation = std::uint32_t(AnimState::Idle);
				return true;
			}
			case EventType::BossDevanRemote: {
				spec.Metadata = "Boss/DevanRemote";
				spec.Animation = std::uint32_t(AnimState::Idle);
				spec.FlipX = true;
				return true;
			}
			case EventType::BossRobot: {
				spec.Metadata = "Boss/Robot";
				spec.Animation = std::uint32_t(AnimState::Idle);
				spec.FlipX = true;
				return true;
			}
			case EventType::BossTurtleTough: {
				spec.Metadata = "Boss/TurtleBoss";
				spec.Animation = std::uint32_t(AnimState::Idle);
				spec.FlipX = true;
				return true;
			}
			case EventType::BossQueen: {
				spec.Metadata = "Boss/Queen";
				spec.Animation = std::uint32_t(AnimState::Idle);
				spec.Offset = Vector2f(0.0f, -8.0f);
				return true;
			}

			default:
				return false;
		}
	}
}

#endif
