#include "PlayerCorpse.h"
#include "../PlayerType.h"
#include "../ContentResolver.h"

namespace Jazz2::Actors
{
	PlayerCorpse::PlayerCorpse()
		: _paletteOffset(-1)
	{
	}

	PlayerCorpse::~PlayerCorpse()
	{
		if (_paletteOffset >= 0) {
			ContentResolver::Get().ReleasePaletteOffset(_paletteOffset);
			_paletteOffset = -1;
		}
	}

	Task<bool> PlayerCorpse::OnActivatedAsync(const ActorActivationDetails& details)
	{
		PlayerType playerType = (PlayerType)details.Params[0];
		SetFacingLeft(details.Params[1] != 0);
		// Effective fur color of the player this corpse came from (bytes 2..5); 0 means no recolor
		std::uint32_t furColor = (std::uint32_t)details.Params[2] | ((std::uint32_t)details.Params[3] << 8) |
			((std::uint32_t)details.Params[4] << 16) | ((std::uint32_t)details.Params[5] << 24);
		bool recolor = (furColor != 0);

		SetState(ActorState::PreserveOnRollback, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithOtherActors, false);

		// Load indexed only when recoloring, so the corpse can be tinted to match the player
		switch (playerType) {
			default:
			case PlayerType::Jazz:
				async_await RequestMetadataAsync("Interactive/PlayerJazz"_s, recolor);
				break;
			case PlayerType::Spaz:
				async_await RequestMetadataAsync("Interactive/PlayerSpaz"_s, recolor);
				break;
			case PlayerType::Lori:
				async_await RequestMetadataAsync("Interactive/PlayerLori"_s, recolor);
				break;
		}

		SetAnimation((AnimState)536870912);

		if (recolor) {
			// Share a reference-counted palette with the player and any other corpses of the same color
			_paletteOffset = ContentResolver::Get().AcquirePaletteOffset(furColor);
			if (_paletteOffset >= 0) {
				_renderer.SetPalette(_paletteOffset);
			}
		}

		async_return true;
	}
}
