#include "GenericContainer.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventSpawner.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Solid
{
	GenericContainer::GenericContainer()
	{
	}

	bool GenericContainer::OnPerish(ActorBase* collider)
	{
		SpawnContent();

		return SolidObjectBase::OnPerish(collider);
	}

	void GenericContainer::AddContent(EventType eventType, int count, uint8_t* eventParams, int eventParamsSize)
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
		for (auto& item : _content) {
			float fx, fy;
			if (_content.size() > 1) {
				fx = Random().NextFloat(-6.0f, 6.0f);
				fy = Random().NextFloat(-2.0f, 0.2f);
			} else {
				fx = 0.0f;
				fy = 0.0f;
			}

			std::shared_ptr<ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(item.Type, item.EventParams,
				ActorFlags::None, Vector3i((int)(_pos.X + fx * (2.0f + _content.size() * 0.1f)), (int)(_pos.Y + fy * (12.0f + _content.size() * 0.2f)), _renderer.layer() - 10));
			if (actor != nullptr) {
				actor->AddExternalForce(fx, fy);
				_levelHandler->AddActor(actor);
			}
		}

		_content.clear();
	}
}