#pragma once

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
	/** @brief How the episode selected in @ref EpisodeSelectSection should be started */
	enum class EpisodeSelectMode {
		Singleplayer,		/**< Plain single-player story game */
		LocalMultiplayer,	/**< Local splitscreen multiplayer match */
		OnlineMultiplayer	/**< Hosted online multiplayer server */
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	enum class EpisodeDataFlags {
		None = 0x00,

		IsAvailable = 0x01,
		IsMissing = 0x02,
		IsCompleted = 0x04,
		CanContinue = 0x08,
		CheatsUsed = 0x10,
		LevelsInUnknownDirectory = 0x20,
		RedirectToCustomLevels = 0x40
	};

	DEATH_ENUM_FLAGS(EpisodeDataFlags);

	struct EpisodeData {
		Episode Description;
		EpisodeDataFlags Flags;
	};
#endif

	/**
		@brief Episode selection menu section

		Lists the installed episodes and lets the player pick one to start or continue. Built on top of @ref WidgetSection
		with each episode drawn through a @ref CanvasWidget; the background cross-fade, expand state and start transition
		are driven by the section.
	*/
	class EpisodeSelectSection : public WidgetSection
	{
	public:
		/**
		 * @brief Creates a new instance
		 *
		 * @param mode           How the selected episode should be started (single-player, local splitscreen or
		 *                       online server); anything other than single-player enables multiplayer-style level
		 *                       browsing (custom levels)
		 * @param privateServer  Whether the hosted online server should be private (not publicly listed); only used
		 *                       when @p mode is @ref EpisodeSelectMode::OnlineMultiplayer
		 */
		EpisodeSelectSection(EpisodeSelectMode mode = EpisodeSelectMode::Singleplayer, bool privateServer = false);

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
		float _expandedAnimation;
		float _transitionTime;
		std::int32_t _transitionFromEpisode;
		float _transitionFromEpisodeTime;
		float _animation;
		EpisodeSelectMode _mode;
		bool _privateServer;
		bool _expanded;
		bool _shouldStart;
		ScrollView* _list;
		SmallVector<EpisodeData> _episodes;

		std::int32_t GetSelectedRow() const;
		void DrawEpisodeRow(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset, bool isSelected, std::int32_t row);
		void ExecuteSelected();
		void HandleTap(std::int32_t row, Vector2i touchPos);
		void OnAfterTransition();
		void AddEpisode(StringView episodeFile);
	};
}
