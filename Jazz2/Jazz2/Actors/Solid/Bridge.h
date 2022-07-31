#pragma once

#include "../../ActorBase.h"

namespace Jazz2::Actors::Solid
{
	enum class BridgeType {
		Rope = 0,
		Stone = 1,
		Vine = 2,
		StoneRed = 3,
		Log = 4,
		Gem = 5,
		Lab = 6,

		Last = Lab
	};

	class Bridge : public ActorBase
	{
	public:
		Bridge();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		static constexpr int PieceWidthsRope[] = { 13, 13, 10, 13, 13, 12, 11 };
		static constexpr int PieceWidthsStone[] = { 15, 9, 10, 9, 15, 9, 15 };
		static constexpr int PieceWidthsVine[] = { 7, 7, 7, 7, 10, 7, 7, 7, 7 };
		static constexpr int PieceWidthsStoneRed[] = { 10, 11, 11, 12 };
		static constexpr int PieceWidthsLog[] = { 13, 13, 13 };
		static constexpr int PieceWidthsGem[] = { 14 };
		static constexpr int PieceWidthsLab[] = { 14 };

		struct BridgePiece {
			Vector2f Pos;
			std::unique_ptr<RenderCommand> Command;
		};

		float _originalY;
		BridgeType _bridgeType;
		int _bridgeWidth;
		float _heightFactor;

		SmallVector<BridgePiece, 0> _pieces;
	};
}