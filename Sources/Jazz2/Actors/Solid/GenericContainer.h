#pragma once

#include "../SolidObjectBase.h"
#include "../../EventType.h"

namespace Jazz2::Actors::Solid
{
	class GenericContainer : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		GenericContainer();

	protected:
		struct ContainerContent {
			EventType Type;
			int Count;
			uint8_t EventParams[16];

			ContainerContent(EventType eventType, int count, const uint8_t* eventParams, int eventParamsSize)
			{
				Type = eventType;
				Count = count;

				std::memcpy(EventParams, eventParams, eventParamsSize);
				if (eventParamsSize < 16) {
					std::memset(EventParams + eventParamsSize, 0, 16 - eventParamsSize);
				}
			}
		};

		SmallVector<ContainerContent, 0> _content;

		bool OnPerish(ActorBase* collider) override;

		void AddContent(EventType eventType, int count, const uint8_t* eventParams, int eventParamsSize);
		void SpawnContent();
	};
}