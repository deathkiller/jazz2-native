#include "GenericContainer.h"
#include "../Weapons/TNT.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventSpawner.h"
#include "../../Tiles/TileMap.h"
#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Solid
{
	GenericContainer::GenericContainer()
	{
	}

	bool GenericContainer::CanCauseDamage(ActorBase* collider)
	{
		return _levelHandler->IsReforged() || runtime_cast<Weapons::TNT>(collider);
	}

	bool GenericContainer::OnPerish(ActorBase* collider)
	{
		SpawnContent();

		return SolidObjectBase::OnPerish(collider);
	}

	void GenericContainer::AddContent(EventType eventType, std::int32_t count, const std::uint8_t* eventParams, std::int32_t eventParamsSize)
	{
		if (count > 0) {
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
		SetState(prevState);

		std::int32_t maxCount = 0;
		for (auto& item : _content) {
			if (maxCount < item.Count) {
				maxCount = item.Count;
			}
		}

		bool canThrowFar = (maxCount > 1 && _levelHandler->IsPositionEmpty(this, aabbExtended, params));

		for (auto& item : _content) {
			for (std::int32_t j = 0; j < item.Count; j++) {
				float x, y, fx, fy;
				if (item.Count > 1) {
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
					ActorState::None, Vector3i((std::int32_t)x, (std::int32_t)y, _renderer.layer() - 10));
				if (actor != nullptr) {
					actor->AddExternalForce(fx, fy);
					_levelHandler->AddActor(actor);
				}
			}
		}

		_content.clear();
	}

	GenericContainer::ContainerContent::ContainerContent(EventType eventType, std::int32_t count, const std::uint8_t* eventParams, std::int32_t eventParamsSize)
	{
		Type = eventType;
		Count = count;

		std::memcpy(EventParams, eventParams, eventParamsSize);
		if (eventParamsSize < 16) {
			std::memset(EventParams + eventParamsSize, 0, 16 - eventParamsSize);
		}
	}
}