#include "FastFireCollectible.h"
#include "../../ILevelHandler.h"
#include "../../ContentResolver.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	FastFireCollectible::FastFireCollectible()
		: _paletteOffset(-1)
	{
	}

	FastFireCollectible::~FastFireCollectible()
	{
		if (_paletteOffset >= 0) {
			ContentResolver::Get().ReleasePaletteOffset(_paletteOffset);
			_paletteOffset = -1;
		}
	}

	void FastFireCollectible::Preload(const ActorActivationDetails& details)
	{
		// Preloading is not supported here
	}

	Task<bool> FastFireCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 200;

		// The fast fire collectible is the player's blaster, so it matches the (first local) player's character and
		// custom colors. Online sessions have no single owner, so they keep the default (Jazz) uncolored appearance.
		auto players = _levelHandler->GetPlayers();
		Player* owner = (_levelHandler->IsLocalSession() && !players.empty() ? players[0] : nullptr);
		PlayerType playerType = (owner != nullptr ? owner->GetPlayerType() : PlayerType::Jazz);
		std::uint32_t furColor = (owner != nullptr ? owner->GetEffectiveFurColor() : 0);
		bool recolor = (furColor != 0);

		// Load indexed only when recoloring, so the sprite can be tinted at draw time (see Player::RefreshColorPalette)
		switch (playerType) {
			default:
			case PlayerType::Jazz: async_await RequestMetadataAsync("Collectible/FastFireJazz"_s, recolor); break;
			case PlayerType::Spaz: async_await RequestMetadataAsync("Collectible/FastFireSpaz"_s, recolor); break;
			case PlayerType::Lori: async_await RequestMetadataAsync("Collectible/FastFireLori"_s, recolor); break;
		}

		SetAnimation(AnimState::Default);

		SetFacingDirection();

		if (recolor) {
			// Only recolor if the sprite actually loaded indexed; otherwise applying a palette would corrupt the
			// baked colors (the palette shader would read them as raw indices)
			auto& resolver = ContentResolver::Get();
			bool isIndexed = (!resolver.IsHeadless() && _metadata != nullptr && !_metadata->Animations.empty() &&
				_metadata->Animations[0].Base != nullptr &&
				(_metadata->Animations[0].Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed);
			if (isIndexed) {
				// Share a reference-counted palette with the player and any other objects of the same color
				_paletteOffset = resolver.AcquirePaletteOffset(furColor);
				if (_paletteOffset >= 0) {
					_renderer.SetPalette(_paletteOffset);
				}
			}
		}

		async_return true;
	}

	void FastFireCollectible::OnCollect(Player* player)
	{
		if (player->AddFastFire(1)) {
			CollectibleBase::OnCollect(player);
		}
	}
}