#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors
{
	class SolidObjectBase : public ActorBase
	{
	public:
		SolidObjectBase();
		~SolidObjectBase();

		bool IsOneWay;

		bool Push(bool left, float timeMult);

	};
}