#pragma once

#include "../../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class EndOfLevel : public ActorBase
	{
	public:
		EndOfLevel();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Object/SignEol"_s);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;

	private:
		ExitType _exitType;
	};
}