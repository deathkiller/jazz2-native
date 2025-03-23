#pragma once

#include "../Actors/ActorBase.h"
#include "../ILevelHandler.h"

#include "../../nCine/Base/HashMap.h"

namespace Jazz2::Events
{
	/** @brief Spawns objects in a level */
	class EventSpawner
	{
	public:
		/** @brief Delegate to create an object */
		using CreateDelegate = std::shared_ptr<Actors::ActorBase> (*)(const Actors::ActorActivationDetails& details);
		/** @brief Delegate to preload assets for an object */
		using PreloadDelegate = void (*)(const Actors::ActorActivationDetails& details);

		/** @defgroup constants Constants
			@{ */

		/** @brief Size of event parameters */
		static constexpr std::int32_t SpawnParamsSize = 16;

		/** @} */

		EventSpawner(ILevelHandler* levelHandler);

		/** @brief Preloads assets for a given event */
		void PreloadEvent(EventType type, std::uint8_t* spawnParams);
		/** @brief Spawns an object for a given event */
		std::shared_ptr<Actors::ActorBase> SpawnEvent(EventType type, std::uint8_t* spawnParams, Actors::ActorState flags, std::int32_t x, std::int32_t y, std::int32_t z);
		/** @overload */
		std::shared_ptr<Actors::ActorBase> SpawnEvent(EventType type, std::uint8_t* spawnParams, Actors::ActorState flags, const Vector3i& pos);

		/** @brief Registers a delegate to create an object from an event */
		void RegisterSpawnable(EventType type, CreateDelegate create, PreloadDelegate preload = nullptr);

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct SpawnableEvent {
			CreateDelegate CreateFunction;
			PreloadDelegate PreloadFunction;
		};
#endif

		ILevelHandler* _levelHandler;
		HashMap<EventType, SpawnableEvent> _spawnableEvents;

		void RegisterKnownSpawnables();

		template<typename T>
		void RegisterSpawnable(EventType type);
	};
}