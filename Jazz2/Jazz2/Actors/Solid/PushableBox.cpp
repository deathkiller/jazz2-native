#include "PushableBox.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../Explosion.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"

namespace Jazz2::Actors::Solid
{
	PushableBox::PushableBox()
	{
	}

	void PushableBox::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			default:
			case 0: PreloadMetadataAsync("Object/PushBoxRock"_s); break;
			case 1: PreloadMetadataAsync("Object/PushBoxCrate"_s); break;
		}
	}

	Task<bool> PushableBox::OnActivatedAsync(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];

		Movable = true;

		switch (theme) {
			default:
			case 0: co_await RequestMetadataAsync("Object/PushBoxRock"); break;
			case 1: co_await RequestMetadataAsync("Object/PushBoxCrate"); break;
		}

		SetAnimation("PushBox"_s);

		co_return true;
	}
}