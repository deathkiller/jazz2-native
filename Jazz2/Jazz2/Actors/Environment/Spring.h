#pragma once

#include "../../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class Spring : public ActorBase
	{
	public:
		Spring();

		bool KeepSpeedX, KeepSpeedY;

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Object/Spring"_s);
		}

		Vector2f Activate();

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;

	private:
		uint16_t _type;
		uint16_t _orientation;
		float _strength;
		//uint16_t delay;
		//bool frozen;
		float _cooldown;
	};
}