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

		static constexpr std::int32_t PieceWidthsRope[] = { 13, 13, 10, 13, 13, 12, 11 };
		static constexpr std::int32_t PieceWidthsStone[] = { 15, 9, 10, 9, 15, 9, 15 };
		static constexpr std::int32_t PieceWidthsVine[] = { 7, 7, 7, 7, 10, 7, 7, 7, 7 };
		static constexpr std::int32_t PieceWidthsStoneRed[] = { 10, 11, 11, 12 };
		static constexpr std::int32_t PieceWidthsLog[] = { 13, 13, 13 };
		static constexpr std::int32_t PieceWidthsGem[] = { 14 };
		static constexpr std::int32_t PieceWidthsLab[] = { 14 };

		BridgeType _bridgeType;
		std::int32_t _bridgeWidth;
		float _heightFactor;
		const std::int32_t* _widths;
		std::int32_t _widthsCount;
		std::int32_t _widthOffset;

		float _leftX, _leftHeight, _rightX, _rightHeight;

		SmallVector<BridgePiece, 0> _pieces;

		float GetSectionHeight(float x) const;
	};
}