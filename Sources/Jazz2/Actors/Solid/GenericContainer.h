#pragma once

#include "../SolidObjectBase.h"
#include "../../EventType.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Base class of an item container
		
		Shared base for the game's destructible containers (crates and barrels). A container holds a list of
		contents that are spawned and scattered outward when it is destroyed.
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

		/** @brief Height of a fall above which a container breaks on landing, the 2 px per tick of the original reached at its gravity */
		static constexpr float BreakFallHeight = 16.0f;

		bool OnPerish(ActorBase* collider) override;
		void OnUpdate(float timeMult) override;
		void OnHitFloor(float timeMult) override;

		/** @brief Adds a content to the container */
		void AddContent(EventType eventType, std::int32_t count, const std::uint8_t* eventParams, std::int32_t eventParamsSize);
		/** @brief Adds the gems of a container that has no gems set: 4 to 7 of random colors */
		void AddRandomGems();
		/** @brief Spawns the added content */
		void SpawnContent();

	private:
		float _lastSpeedY = 0.0f;
		bool _hasLanded = false;
		bool _breakOnLanding = false;
	};
}