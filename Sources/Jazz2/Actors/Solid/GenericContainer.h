#pragma once

#include "../SolidObjectBase.h"
#include "../../EventType.h"

namespace Jazz2::Actors::Solid
{
	/**
	 * @brief Base class of an item container
	 *
	 * Shared base for the game's destructible containers (crates and barrels). A container holds a list of
	 * contents that are spawned and scattered outward when it is destroyed.
	 */
	class GenericContainer : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		/** @brief Creates a new instance */
		GenericContainer();

		bool CanCauseDamage(ActorBase* collider) override;

	protected:
		/** @brief Container content item */
		struct ContainerContent {
			/** @brief Event type */
			EventType Type;
			/** @brief Object count */
			std::int32_t Count;
			/** @brief Event parameters */
			std::uint8_t EventParams[16];

			ContainerContent(EventType eventType, std::int32_t count, const std::uint8_t* eventParams, std::int32_t eventParamsSize);
		};

		/** @brief Content items to spawn when the container is destroyed */
		SmallVector<ContainerContent, 1> _content;

		bool OnPerish(ActorBase* collider) override;

		/** @brief Adds a content to the container */
		void AddContent(EventType eventType, std::int32_t count, const std::uint8_t* eventParams, std::int32_t eventParamsSize);
		/** @brief Spawns the added content */
		void SpawnContent();
	};
}