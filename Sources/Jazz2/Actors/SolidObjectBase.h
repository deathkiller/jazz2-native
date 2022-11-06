#pragma once

#include "ActorBase.h"

namespace Jazz2::Actors
{
	class SolidObjectBase : public ActorBase
	{
	public:
		SolidObjectBase();

		bool IsOneWay;
		bool Movable;

		float Push(bool left, float timeMult);

	};
}