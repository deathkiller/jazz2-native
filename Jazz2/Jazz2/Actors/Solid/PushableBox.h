#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	class PushableBox : public SolidObjectBase
	{
	public:
		PushableBox();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
	};
}