#pragma once

#include "../Actors/ActorBase.h"
#include "../ILevelHandler.h"

#include "../../nCine/Base/HashMap.h"

namespace Jazz2::Events
{
	class EventSpawner
	{
	public:
		using CreateDelegate = std::shared_ptr<Actors::ActorBase> (*)(const Actors::ActorActivationDetails& details);
		using PreloadDelegate = void (*)(const Actors::ActorActivationDetails& details);

		static constexpr std::int32_t SpawnParamsSize = 16;

		EventSpawner(ILevelHandler* levelHandler);

		void PreloadEvent(EventType type, std::uint8_t* spawnParams);
		std::shared_ptr<Actors::ActorBase> SpawnEvent(EventType type, std::uint8_t* spawnParams, Actors::ActorState flags, std::int32_t x, std::int32_t y, std::int32_t z);
		std::shared_ptr<Actors::ActorBase> SpawnEvent(EventType type, std::uint8_t* spawnParams, Actors::ActorState flags, const Vector3i& pos);

		void RegisterSpawnable(EventType type, CreateDelegate create, PreloadDelegate preload = nullptr);

	private:
		struct SpawnableEvent {
			CreateDelegate CreateFunction;
			PreloadDelegate PreloadFunction;
		};

		ILevelHandler* _levelHandler;
		HashMap<EventType, SpawnableEvent> _spawnableEvents;

		void RegisterKnownSpawnables();

		template<typename T>
		void RegisterSpawnable(EventType type);
	};
}