#include "EventPreviewResolver.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"
#include "../../Direction.h"
#include "../../EventType.h"

namespace Jazz2::Editor
{
	namespace
	{
		// Pole::OnActivatedAsync(), indexed by the theme
		constexpr const char* PoleMetadata[] = {
			"Pole/Carrotus", "Pole/Diamondus", "Pole/DiamondusTree", "Pole/Jungle", "Pole/Psych"
		};

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
		// Only what depends on more than one parameter, or not just on its value, the rest is said by the catalog, see
		// EventDescriptor::PreviewCases
		switch (EventType(event.Type)) {
			// Collectibles, they also face left on every other tile by the parity of their position, which the
			// record doesn't know (CollectibleBase::SetFacingDirection())
			case EventType::Gem: {
				// GemCollectible, each color is its own animation with its own palette offset in Gems.res
				spec.Metadata = "Collectible/Gems";
				spec.Animation = std::uint32_t(p[0] & 0x03);
				spec.FlipX = ((p[1] & 0x02) != 0);
				return true;
			}

			// Basic and environment
			case EventType::Checkpoint: {
				spec.Metadata = (p[0] == 1 ? "Object/CheckpointXmas" : "Object/Checkpoint");
				spec.Animation = (p[1] != 0 ? 1 : 0);
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
			case EventType::Pole: {
				const char* metadata = FromTable(PoleMetadata, p[0]);
				spec.Metadata = (metadata != nullptr ? metadata : "Pole/Carrotus");
				spec.Animation = std::uint32_t(AnimState::Default);
				spec.Offset = Vector2f(float(ReadInt16(p, 1)), float(ReadInt16(p, 3)));
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

			// Enemies
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

			default:
				return false;
		}
	}
}

#endif
