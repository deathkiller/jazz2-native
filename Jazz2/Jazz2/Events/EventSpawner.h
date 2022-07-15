#pragma once

#include "../ActorBase.h"
#include "../ILevelHandler.h"

#include <map>

namespace Jazz2::Events
{
	class EventSpawner
	{
	public:
		using CreateFunction = std::shared_ptr<ActorBase> (*)(const ActorActivationDetails& details);
		using PreloadFunction = void (*)(const ActorActivationDetails& details);

		static constexpr int SpawnParamsSize = 16;

		EventSpawner(ILevelHandler* levelHandler);

		void PreloadEvent(EventType type, uint8_t* spawnParams);
		std::shared_ptr<ActorBase> SpawnEvent(EventType type, uint8_t* spawnParams, ActorFlags flags, int x, int y, int z);
		std::shared_ptr<ActorBase> SpawnEvent(EventType type, uint8_t* spawnParams, ActorFlags flags, const Vector3i& pos);

	private:
		struct SpawnableEvent {
			CreateFunction CreateFunction;
			PreloadFunction PreloadFunction;
		};

		ILevelHandler* _levelHandler;
		std::map<EventType, SpawnableEvent> _spawnableEvents;

		void RegisterKnownSpawnables();
		void RegisterSpawnable(EventType type, CreateFunction create, PreloadFunction preload = nullptr);

		template<typename T>
		void RegisterSpawnable(EventType type);
	};
}