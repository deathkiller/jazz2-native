﻿#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	/** @brief Pushable box */
	class PushableBox : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		PushableBox();

		static void Preload(const ActorActivationDetails& details);

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		float GetIceShrapnelScale() const override;
	};
}