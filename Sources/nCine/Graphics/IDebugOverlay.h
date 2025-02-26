#pragma once

#include "../Base/TimeStamp.h"

#include <Asserts.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/// Interface for debug overlays
	class IDebugOverlay
	{
	public:
		/// Settings for the debug overlay
		struct DisplaySettings
		{
			DisplaySettings() : showProfilerGraphs(true), showInfoText(true), showInterface(false) {}

			/// Whether to show the profiler graphs
			bool showProfilerGraphs;
			/// Whether to show the information text
			bool showInfoText;
			/// Whether to show the debug interface
			bool showInterface;
		};

		explicit IDebugOverlay(float profileTextUpdateTime);
		virtual ~IDebugOverlay();

		IDebugOverlay(const IDebugOverlay&) = delete;
		IDebugOverlay& operator=(const IDebugOverlay&) = delete;

		inline DisplaySettings& settings() {
			return settings_;
		}
		virtual void update() = 0;
		virtual void updateFrameTimings() = 0;

#if defined(DEATH_TRACE)
		virtual void log(TraceLevel level, StringView time, StringView threadId, StringView message) = 0;
#endif

	protected:
		DisplaySettings settings_;
		TimeStamp lastUpdateTime_;
		float updateTime_;
	};

	inline IDebugOverlay::IDebugOverlay(float profileTextUpdateTime)
		: updateTime_(profileTextUpdateTime) {}

	inline IDebugOverlay::~IDebugOverlay() {}
}