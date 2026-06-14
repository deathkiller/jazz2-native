#pragma once

#include "../Base/TimeStamp.h"

#include <Asserts.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Interface for an on-screen debug overlay
		
		Base class for overlays that draw diagnostic information on top of the rendered scene, such as profiler
		graphs, frame timings and informational text. Implementations are updated once per frame by the
		application and refresh their textual data no more often than the configured interval.
	*/
	class IDebugOverlay
	{
	public:
		/**
		 * @brief Settings controlling which parts of the debug overlay are shown
		 */
		struct DisplaySettings
		{
			DisplaySettings() : showProfilerGraphs(true), showInfoText(true), showInterface(false) {}

			/** @brief Whether to show the profiler graphs */
			bool showProfilerGraphs;
			/** @brief Whether to show the information text */
			bool showInfoText;
			/** @brief Whether to show the debug interface */
			bool showInterface;
		};

		explicit IDebugOverlay(float profileTextUpdateTime);
		virtual ~IDebugOverlay();

		IDebugOverlay(const IDebugOverlay&) = delete;
		IDebugOverlay& operator=(const IDebugOverlay&) = delete;

		/** @brief Returns the mutable display settings of the overlay */
		inline DisplaySettings& GetSettings() {
			return settings_;
		}
		/** @brief Updates the overlay and draws it for the current frame */
		virtual void Update() = 0;
		/** @brief Refreshes the cached frame timing values shown by the overlay */
		virtual void UpdateFrameTimings() = 0;

#if defined(DEATH_TRACE)
		/** @brief Appends a trace log message to be displayed by the overlay */
		virtual void Log(TraceLevel level, StringView time, StringView threadId, StringView functionName, StringView message) = 0;
#endif

	protected:
		/** @brief Display settings controlling which parts of the overlay are shown */
		DisplaySettings settings_;
		/** @brief Time stamp of the last textual data update */
		TimeStamp lastUpdateTime_;
		/** @brief Minimum interval in seconds between textual data updates */
		float updateTime_;
	};

	inline IDebugOverlay::IDebugOverlay(float profileTextUpdateTime)
		: updateTime_(profileTextUpdateTime) {}

	inline IDebugOverlay::~IDebugOverlay() {}
}
