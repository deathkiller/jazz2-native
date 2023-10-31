#include "AmbientBubbles.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Environment
{
	AmbientBubbles::AmbientBubbles()
		:
		_cooldown(0.0f),
		_bubblesLeft(0),
		_delay(0.0f)
	{
	}

	void AmbientBubbles::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Common/AmbientBubbles"_s);
	}

	Task<bool> AmbientBubbles::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_speed = details.Params[0];

		SetState(ActorState::ForceDisableCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Common/AmbientBubbles"_s);

		async_return true;
	}

	void AmbientBubbles::OnUpdate(float timeMult)
	{
		_cooldown -= timeMult;
		if (_cooldown <= 0.0f) {
			SpawnBubbles(_bubblesLeft);
			_bubblesLeft = _speed;
			_cooldown = BaseTime;
		} else if (_bubblesLeft > 0) {
			_delay -= timeMult;
			if (_delay <= 0.0f) {
				SpawnBubbles(1);
				_bubblesLeft--;
				_delay = BaseTime / _speed;
			}
		}
	}

	void AmbientBubbles::SpawnBubbles(int count)
	{
		if (count <= 0) {
			return;
		}

		auto tilemap = _levelHandler->TileMap();
		if (tilemap != nullptr) {
			auto* res = _metadata->FindAnimation(AnimState::Default); // AmbientBubbles
			if (res != nullptr) {
				Vector2i texSize = res->Base->TextureDiffuse->size();
				Vector2i size = res->Base->FrameDimensions;
				Vector2i frameConf = res->Base->FrameConfiguration;

				for (int i = 0; i < count; i++) {
					float scale = Random().NextFloat(0.3f, 1.0f);
					float speedX = Random().NextFloat(-0.5f, 0.5f) * scale;
					float speedY = Random().NextFloat(-3.0f, -2.0f) * scale;
					float accel = Random().NextFloat(-0.008f, -0.001f) * scale;
					int frame = res->FrameOffset + Random().Next(0, res->FrameCount);

					Tiles::TileMap::DestructibleDebris debris = { };
					debris.Pos = _pos;
					debris.Depth = _renderer.layer();
					debris.Size = Vector2f((float)size.X, (float)size.Y);
					debris.Speed = Vector2f(speedX, speedY);
					debris.Acceleration = Vector2f(0.0f, accel);

					debris.Scale = scale;
					debris.Alpha = 0.9f;
					debris.AlphaSpeed = -0.009f;

					debris.Time = 110.0f;

					debris.TexScaleX = (size.X / float(texSize.X));
					debris.TexBiasX = ((float)(frame % frameConf.X) / frameConf.X);
					debris.TexScaleY = (size.Y / float(texSize.Y));
					debris.TexBiasY = ((float)(frame / frameConf.X) / frameConf.Y);

					debris.DiffuseTexture = res->Base->TextureDiffuse.get();

					tilemap->CreateDebris(debris);
				}
			}
		}
	}
}