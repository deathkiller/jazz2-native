#pragma once

#include "../Actors/ActorBase.h"
#include "../ILevelHandler.h"

#include "../../nCine/Base/HashMap.h"

namespace Jazz2::Events
{
	class EventSpawner
	{
	public:
		using CreateDelegate = std::shared_ptr<ActorBase> (*)(const ActorActivationDetails& details);
		using PreloadDelegate = void (*)(const ActorActivationDetails& details);

		static constexpr int SpawnParamsSize = 16;

		EventSpawner(ILevelHandler* levelHandler);

		void PreloadEvent(EventType type, uint8_t* spawnParams);
		std::shared_ptr<ActorBase> SpawnEvent(EventType type, uint8_t* spawnParams, ActorState flags, int x, int y, int z);
		std::shared_ptr<ActorBase> SpawnEvent(EventType type, uint8_t* spawnParams, ActorState flags, const Vector3i& pos);

	private:
		struct SpawnableEvent {
			CreateDelegate CreateFunction;
			PreloadDelegate PreloadFunction;
		};

		ILevelHandler* _levelHandler;
		HashMap<EventType, SpawnableEvent> _spawnableEvents;

		void RegisterKnownSpawnables();
		void RegisterSpawnable(EventType type, CreateDelegate create, PreloadDelegate preload = nullptr);

		template<typename T>
		void RegisterSpawnable(EventType type);
	};
}