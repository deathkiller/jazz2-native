#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Solid
{
	class Bridge : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		Bridge();
		~Bridge();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;

	private:
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

		struct BridgePiece {
			Vector2f Pos;
			std::unique_ptr<RenderCommand> Command;
		};

		static constexpr float BaseY = -6.0f;

		static constexpr int PieceWidthsRope[] = { 13, 13, 10, 13, 13, 12, 11 };
		static constexpr int PieceWidthsStone[] = { 15, 9, 10, 9, 15, 9, 15 };
		static constexpr int PieceWidthsVine[] = { 7, 7, 7, 7, 10, 7, 7, 7, 7 };
		static constexpr int PieceWidthsStoneRed[] = { 10, 11, 11, 12 };
		static constexpr int PieceWidthsLog[] = { 13, 13, 13 };
		static constexpr int PieceWidthsGem[] = { 14 };
		static constexpr int PieceWidthsLab[] = { 14 };

		BridgeType _bridgeType;
		int _bridgeWidth;
		float _heightFactor;
		const int* _widths;
		int _widthsCount;
		int _widthOffset;

		float _foundHeight;
		float _foundX;

		SmallVector<BridgePiece, 0> _pieces;
	};
}