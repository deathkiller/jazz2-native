#pragma once

#include "../../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class Checkpoint : public ActorBase
	{
	public:
		Checkpoint();

		static void Preload(const ActorActivationDetails& details)
		{
			uint8_t theme = details.Params[0];
			switch (theme) {
				case 0:
				default:
					PreloadMetadataAsync("Object/Checkpoint");
					break;
				case 1: // Xmas
					PreloadMetadataAsync("Object/CheckpointXmas");
					break;
			}
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;
		bool OnHandleCollision(ActorBase* other) override;

	private:
		uint8_t _theme;
		bool _activated;
	};
}