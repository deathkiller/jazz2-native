#pragma once

#include "../SolidObjectBase.h"
#include "../../EventType.h"

namespace Jazz2::Actors::Solid
{
	class GenericContainer : public SolidObjectBase
	{
	public:
		GenericContainer();

	protected:
		bool OnPerish(ActorBase* collider) override;

		void AddContent(EventType eventType, int count, uint8_t* eventParams, int eventParamsSize);
		void SpawnContent();

	private:
		struct ContainerContent {
			EventType Type;
			int Count;
			uint8_t EventParams[16];

			ContainerContent(EventType eventType, int count, uint8_t* eventParams, int eventParamsSize)
			{
				Type = eventType;
				Count = count;

				memcpy(EventParams, eventParams, eventParamsSize);
				if (eventParamsSize < 16) {
					memset(EventParams + eventParamsSize, 0, 16 - eventParamsSize);
				}
			}
		};

		SmallVector<ContainerContent, 0> _content;
	};
}