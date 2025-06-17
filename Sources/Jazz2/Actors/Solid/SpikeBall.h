#pragma once

#include "../Enemies/EnemyBase.h"

namespace Jazz2::Actors::Solid
{
	/** @brief Spike ball */
	class SpikeBall : public Enemies::EnemyBase
	{
		DEATH_RUNTIME_OBJECT(Enemies::EnemyBase);

	public:
		SpikeBall();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ChainPiece {
			Vector2f Pos;
			float Scale;
			std::unique_ptr<RenderCommand> Command;
		};
#endif

		bool _shade;
		float _speed;
		bool _isSwing;
		float _phase;
		Vector2f _originPos;
		std::uint16_t _originLayer;

		SmallVector<ChainPiece, 0> _pieces;

		Vector2f GetPhasePosition(std::int32_t distance, float* scale);
	};
}