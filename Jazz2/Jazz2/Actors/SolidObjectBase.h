#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors
{
	class SolidObjectBase : public ActorBase
	{
	public:
		SolidObjectBase();

		bool IsOneWay;
		bool Movable;

		bool Push(bool left, float timeMult);

	};
}