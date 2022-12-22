#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
	enum class EpisodeDataFlags {
		None = 0x00,

		IsAvailable = 0x01,
		IsMissing = 0x02,
		IsCompleted = 0x04,
		CanContinue = 0x08,
		CheatsUsed = 0x10,
	};

	DEFINE_ENUM_OPERATORS(EpisodeDataFlags);

	struct EpisodeData {
		Episode Description;
		EpisodeDataFlags Flags;
	};

	class EpisodeSelectSection : public ScrollableMenuSection<EpisodeData>
	{
	public:
		EpisodeSelectSection();

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnTouchEvent(const TouchEvent& event, const Vector2i& viewSize) override;

	private:
		float _expandedAnimation;
		bool _expanded;
		float _transitionTime;
		bool _shouldStart;

		void OnTouchUp(int newIndex, const Vector2i& viewSize, const Vector2i& touchPos) override;
		void OnExecuteSelected() override;
		void OnDrawEmptyText(Canvas* canvas, int& charOffset) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, int& charOffset, bool isSelected) override;

		void OnAfterTransition();
		void AddEpisode(const StringView& episodeFile);
	};
}