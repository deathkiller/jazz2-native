#include "GenericContainer.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventSpawner.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Solid
{
	GenericContainer::GenericContainer()
	{
		SetState(ActorState::TriggersTNT, true);
	}

	bool GenericContainer::OnPerish(ActorBase* collider)
	{
		SpawnContent();

		return SolidObjectBase::OnPerish(collider);
	}

	void GenericContainer::AddContent(EventType eventType, int count, const uint8_t* eventParams, int eventParamsSize)
	{
		if (count <= 0) {
			return;
		}

		for (int i = 0; i < count; ++i) {
			_content.emplace_back(eventType, count, eventParams, eventParamsSize);
		}
	}

	void GenericContainer::SpawnContent()
	{
		if (_content.empty()) {
			return;
		}

		AABBf aabbExtended = AABB;
		aabbExtended.L -= 20.0f;
		aabbExtended.R += 20.0f;
		aabbExtended.T -= 20.0f;
		TileCollisionParams params = { TileDestructType::None, true };
		auto prevState = GetState();
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::IsSolidObject | ActorState::CollideWithSolidObjectsBelow, false);
		bool canThrowFar = (_content.size() > 1 && _levelHandler->IsPositionEmpty(this, aabbExtended, params));
		SetState(prevState);

		for (auto& item : _content) {
			float x, y, fx, fy;
			if (_content.size() > 1) {
				if (canThrowFar) {
					fx = Random().NextFloat(-6.0f, 6.0f);
					fy = Random().NextFloat(-0.8f, 0.0f);
					x = _pos.X + fx * (2.0f + _content.size() * 0.1f);
					y = _pos.Y + fy * (12.0f + _content.size() * 0.2f);
				} else {
					fx = Random().NextFloat(-4.0f, 4.0f);
					fy = Random().NextFloat(-0.4f, 0.0f);
					x = _pos.X + fx * (1.0f + _content.size() * 0.1f);
					y = _pos.Y + fy * (4.0f + _content.size() * 0.2f);
				}
			} else {
				fx = 0.0f;
				fy = 0.0f;
				x = _pos.X;
				y = _pos.Y;
			}

			std::shared_ptr<ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(item.Type, item.EventParams,
				ActorState::None, Vector3i((int)x, (int)y, _renderer.layer() - 10));
			if (actor != nullptr) {
				actor->AddExternalForce(fx, fy);
				_levelHandler->AddActor(actor);
			}
		}

		_content.clear();
	}
}