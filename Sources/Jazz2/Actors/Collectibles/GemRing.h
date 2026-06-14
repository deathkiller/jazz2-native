#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	/**
		@brief Gem ring
		
		A rotating ring of gems from JJ2. Touching the ring collects every gem in it at once, adding them to
		the player's gem count and awarding a points bonus.
	*/
	class GemRing : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		/** @brief Creates a new instance */
		GemRing();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;

		void OnCollect(Player* player) override;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ChainPiece {
			Vector2f Pos;
			float Angle;
			float Scale;
			std::unique_ptr<RenderCommand> Command;
		};
#endif

		float _speed;
		float _phase;
		bool _collected;
		float _collectedPhase;

		SmallVector<ChainPiece, 0> _pieces;
	};
}