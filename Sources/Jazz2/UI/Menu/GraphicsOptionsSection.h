#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
	enum class GraphicsOptionsItemType {
		RescaleMode,
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
		Fullscreen,
#endif
		Antialiasing,
		LowWaterQuality,
		ShowPlayerTrails,
		UnalignedViewport,
		KeepAspectRatioInCinematics,
		ShowPerformanceMetrics
	};

	struct GraphicsOptionsItem {
		GraphicsOptionsItemType Type;
		StringView DisplayName;
		bool HasBooleanValue;
	};

	class GraphicsOptionsSection : public ScrollableMenuSection<GraphicsOptionsItem>
	{
	public:
		GraphicsOptionsSection();
		~GraphicsOptionsSection();

		void OnDraw(Canvas* canvas) override;

	private:
		bool _isDirty;

		void OnHandleInput() override;
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;
	};
}