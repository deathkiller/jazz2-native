#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	class GemRing : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		GemRing();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;

		void OnCollect(Player* player) override;

	private:
		struct ChainPiece {
			Vector2f Pos;
			float Angle;
			float Scale;
			std::unique_ptr<RenderCommand> Command;
		};

		float _speed;
		float _phase;
		bool _collected;
		float _collectedPhase;

		SmallVector<ChainPiece, 0> _pieces;
	};
}