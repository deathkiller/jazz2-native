#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	enum class GraphicsOptionsItemType {
		RescaleMode,
		Resolution,
#if defined(NCINE_HAS_WINDOWS)
		Fullscreen,
#endif
		Antialiasing,
		BackgroundDithering,
		LowWaterQuality,
		ShowPlayerTrails,
		PreferVerticalSplitscreen,
		PreferZoomOut,
		KeepAspectRatioInCinematics,
		UnalignedViewport,
		ShowPerformanceMetrics
	};

	struct GraphicsOptionsItem {
		GraphicsOptionsItemType Type;
		StringView DisplayName;
		bool HasBooleanValue;
	};
#endif

	class GraphicsOptionsSection : public ScrollableMenuSection<GraphicsOptionsItem>
	{
	public:
		GraphicsOptionsSection();
		~GraphicsOptionsSection();

		void OnDraw(Canvas* canvas) override;

	protected:
		void OnHandleInput() override;
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;

	private:
		bool _isDirty;
	};
}