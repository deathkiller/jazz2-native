#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Bridge
		
		A span the player can walk across that sags downward under their weight and springs back once they
		step off, made up of individually drawn pieces. Several visual styles exist (rope, vine, stone, log,
		etc.) depending on the level theme.
	*/
	class Bridge : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		Bridge();
		~Bridge();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		/**
		 * @brief Appends where the pieces of a bridge are, from the position of the bridge
		 *
		 * @p bridgeType and @p bridgeWidth (in pixels) are as the event gives them, @p resource is the animation of the
		 * pieces, they're as wide as its frames. The level editor shows a bridge this way, piece by piece, as long as it is.
		 */
		static void GetPieceOffsets(std::uint8_t bridgeType, std::int32_t bridgeWidth, const GraphicResource* resource, SmallVectorImpl<std::int32_t>& offsets);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;
		/** @brief Applies authoritative sag state received from the server (clients only, in multiplayer) */
		void OnPacketReceived(MemoryStream& packet) override;

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

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct BridgePiece {
			Vector2f Pos;
			std::unique_ptr<RenderCommand> Command;
		};
#endif

		static constexpr float BaseY = -6.0f;

		// Width of a piece without graphics (a server)
		static constexpr std::int32_t FallbackPieceWidth = 13;

		// How far the first piece is moved in from the left end
		static std::int32_t GetPieceInset(std::uint8_t bridgeType);
		// Width of a piece, that of its frame
		static std::int32_t GetPieceWidth(const GraphicResource* resource, std::int32_t index);

		BridgeType _bridgeType;
		std::int32_t _bridgeWidth;
		float _heightFactor;

		float _leftX, _leftHeight, _rightX, _rightHeight;
		// Server-authoritative sag (accounts for all players including remote ones); used on clients for
		// rendering in addition to the locally-computed sag so the bridge bends under remote players too
		float _syncLeftX, _syncLeftHeight, _syncRightX, _syncRightHeight;
		float _sagSyncTimer;
		bool _sagWasActive;

		SmallVector<BridgePiece, 0> _pieces;

		void AddPiece();
		float GetSectionHeight(float x) const;
		float GetDropAt(float widthCovered, float leftX, float leftHeight, float rightX, float rightHeight) const;
	};
}