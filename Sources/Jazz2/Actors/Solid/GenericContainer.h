#pragma once

#include "../SolidObjectBase.h"
#include "../../EventType.h"

namespace Jazz2::Actors::Solid
{
	/** @brief Base class of an item container */
	class GenericContainer : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		GenericContainer();

	protected:
		struct ContainerContent {
			EventType Type;
			std::int32_t Count;
			std::uint8_t EventParams[16];

			ContainerContent(EventType eventType, std::int32_t count, const std::uint8_t* eventParams, std::int32_t eventParamsSize);
		};

		SmallVector<ContainerContent, 1> _content;

		bool OnPerish(ActorBase* collider) override;

		void AddContent(EventType eventType, std::int32_t count, const std::uint8_t* eventParams, std::int32_t eventParamsSize);
		void SpawnContent();
	};
}