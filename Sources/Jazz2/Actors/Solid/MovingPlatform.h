#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	class MovingPlatform : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		MovingPlatform();
		~MovingPlatform();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		enum class PlatformType {
			CarrotusFruit = 1,
			Ball = 2,
			CarrotusGrass = 3,
			Lab = 4,
			Sonic = 5,
			Spike = 6,

			SpikeBall = 7,
		};

		struct ChainPiece {
			Vector2f Pos;
			std::unique_ptr<RenderCommand> Command;
		};

		PlatformType _type;
		float _speed;
		bool _isSwing;
		float _phase;
		Vector2f _originPos, _lastPos;

		SmallVector<ChainPiece, 0> _pieces;

		Vector2f GetPhasePosition(int distance);
	};
}