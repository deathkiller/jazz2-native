#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../HUD.h"
#include "../../Multiplayer/MpLevelHandler.h"

namespace Jazz2::UI::Multiplayer
{
	/**
		@brief Player HUD for multiplayer
		
		Multiplayer-specific extension of @ref HUD. It overrides the overview and score drawing to show
		multiplayer information such as the player's position in the current round, and adds a centered countdown
		display.
	*/
	class MpHUD : public HUD
	{
	public:
		/** @brief Creates a new instance */
		MpHUD(Jazz2::Multiplayer::MpLevelHandler* levelHandler);

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		/** @brief Shows countdown text in the middle of the screen */
		void ShowCountdown(std::int32_t secsLeft);

	protected:
		void OnDrawOverview(const Rectf& view, const Rectf& adjustedView, Actors::Player* player) override;
		void OnDrawScore(const Rectf& view, Actors::Player* player) override;

		/** @brief Draws the position of the player in the current round */
		void DrawPositionInRound(const Rectf& view, Actors::Player* player);

		/** @brief Draws the race-track minimap in the top-right corner */
		void DrawMinimap(const Rectf& view, Actors::Player* player);

	private:
		static constexpr std::int32_t MinimapMaxVertexFloats = 65536;

		Font* _mediumFont;
		String _countdownText;
		float _countdownTimeLeft;

		// Cached smoothed minimap track centerline (in tile coordinates), rebuilt only when the checkpoints change.
		// The actual thick-line mesh is rebuilt each frame from this (cheap) because it depends on the view.
		SmallVector<Vector2f, 0> _minimapPoints;
		SmallVector<std::int32_t, 0> _minimapRunOffsets;	// Start index of each run; the last entry is the total count
		std::uint32_t _minimapCacheKey;
		std::unique_ptr<float[]> _minimapVertices;			// Persistent host vertex buffer for the line mesh
		std::int32_t _minimapVertexCount;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _minimapCommands;
		std::int32_t _minimapCommandsCount;
		std::unique_ptr<Texture> _minimapLineTexture;		// 1D alpha-feather texture used to antialias the line edges

		/** @brief Rebuilds the cached smoothed track centerline if the checkpoints changed */
		void RefreshMinimapTrack(Jazz2::Multiplayer::MpLevelHandler* mpLevelHandler);
		/** @brief Draws a smoothed thick polyline as a triangle-strip mesh */
		void DrawMinimapTrackLine(const Vector2f* points, std::int32_t count, Vector2f offset, float halfThickness, std::uint16_t z, const Colorf& color);
	};
}

#endif