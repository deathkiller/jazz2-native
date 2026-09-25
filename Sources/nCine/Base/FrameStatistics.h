#pragma once

#include "TimeStamp.h"

#include <cstddef>
#include <cstdint>

namespace nCine
{
	/**
		@brief Frame timing and backend counters, averaged for an on-screen overlay

		Collects nothing until @ref SetEnabled() switches it on, so a build that never displays the numbers pays a
		branch per phase of the frame and nothing else. While it is on, @ref Application::Step() times every phase of
		the frame, @ref RenderQueue counts the draw calls, and the graphics backend adds what only it can know through
		@ref AddCounter() - the GPU time, how long it blocked on the GPU or the display, hardware counters. Every
		@ref AveragingInterval the accumulated values are averaged into the snapshot @ref GetSnapshot() returns, so
		the numbers change a few times a second instead of flickering every frame, while a single long frame still
		shows in @ref Snapshot::MaxFrameTime.
	*/
	class FrameStatistics
	{
	public:
		/** @brief Phase of a frame, in the order @ref Application::Step() runs them */
		enum class Phase : std::uint8_t
		{
			BeginFrame,		/**< @ref IAppEventHandler::OnBeginFrame() */
			Update,			/**< Scene graph update */
			PostUpdate,		/**< @ref IAppEventHandler::OnPostUpdate() */
			Visit,			/**< Scene graph visit, which culls the nodes and collects their render commands */
			Draw,			/**< Sorting, batching and submitting the render commands */
			Audio,			/**< Updating the audio players, which is also where the streams are decoded */
			EndFrame,		/**< @ref IAppEventHandler::OnEndFrame() */
			Present,		/**< Presenting the frame, including any wait for the GPU or the vertical blank */
			Wait,			/**< Frame rate limiter */

			Count			/**< Count of phases */
		};

		/** @brief Unit of a counter value */
		enum class Unit : std::uint8_t
		{
			Milliseconds,	/**< Time in milliseconds */
			Percent,		/**< Percentage */
			Bytes,			/**< Size in bytes */
			Number			/**< Plain number */
		};

		/** @brief Counter reported by a backend, averaged over an interval */
		struct Counter
		{
			/** @brief Short name, which must be a string literal - the snapshot keeps only the pointer */
			const char* Name;
			/** @brief Average value per frame that reported it */
			float Value;
			/** @brief What the value is out of, or zero if it has no such bound */
			float Limit;
			/** @brief Unit of the value */
			Unit Type;
		};

		/** @{ @name Constants */

		/** @brief Maximum number of distinct counters, further ones are dropped */
		static constexpr std::uint32_t MaxCounters = 8;
		/** @brief Interval in seconds the values are averaged over */
		static constexpr float AveragingInterval = 0.5f;

		/** @} */

		/** @brief Values averaged over the last complete interval */
		struct Snapshot
		{
			/**
			 * @brief Changes every time the snapshot does
			 *
			 * So whatever is derived from the snapshot - the formatted text of an overlay, say - can be redone only
			 * when there is something new, twice a second rather than every frame.
			 */
			std::uint32_t Sequence;
			/** @brief Number of frames the snapshot averages, zero until the first interval completes */
			std::uint32_t FrameCount;
			/** @brief Average frame time in milliseconds */
			float FrameTime;
			/** @brief Longest frame of the interval in milliseconds */
			float MaxFrameTime;
			/** @brief Average time of each phase in milliseconds */
			float PhaseTimes[(std::size_t)Phase::Count];
			/** @brief Average number of draw calls per frame */
			float DrawCalls;
			/** @brief Average number of render commands per frame, before they were batched into the draw calls */
			float RenderCommands;
			/** @brief Memory in use in bytes, or zero where it cannot be queried */
			std::uint64_t MemoryUsed;
			/** @brief Memory the use is out of in bytes, or zero where there is no fixed amount */
			std::uint64_t MemoryTotal;
			/** @brief Counters reported by the backends, in the order they first reported */
			Counter Counters[MaxCounters];
			/** @brief Number of valid entries in @ref Counters */
			std::uint32_t CounterCount;
		};

		FrameStatistics() = delete;
		~FrameStatistics() = delete;

		/** @brief Returns `true` while the statistics are being collected */
		static inline bool IsEnabled() {
			return _enabled;
		}
		/**
		 * @brief Starts or stops collecting the statistics
		 *
		 * Starting throws away whatever was collected before, so the first snapshot after it describes only
		 * frames that were measured whole.
		 */
		static void SetEnabled(bool enabled);

		/** @brief Adds the draw calls of one render queue to the current frame */
		static void AddDrawCalls(std::uint32_t drawCalls, std::uint32_t renderCommands);
		/**
		 * @brief Adds a value of a counter to the current frame
		 *
		 * Values reported under the same name within one frame add up, and the snapshot averages them over the
		 * frames that reported the counter at all - a GPU timer whose result arrives only every other frame is
		 * not halved by the frames in between. Does nothing while the statistics are not being collected, but a
		 * caller whose value is expensive to obtain should check @ref IsEnabled() before it gets it.
		 */
		static void AddCounter(const char* name, float value, Unit unit, float limit = 0.0f);
		/**
		 * @brief Closes the current frame
		 *
		 * @param phaseTimes	Duration of each @ref Phase in seconds
		 * @param frameTime		Duration of the whole frame in seconds, measured from its start to the start of the next one
		 */
		static void EndFrame(const float* phaseTimes, float frameTime);

		/** @brief Returns the values averaged over the last complete interval */
		static inline const Snapshot& GetSnapshot() {
			return _snapshot;
		}

	private:
		struct CounterAccumulator
		{
			const char* Name;
			double Sum;
			float Limit;
			std::uint32_t Frames;
			Unit Type;
			bool ReportedInFrame;
		};

		static bool _enabled;
		static Snapshot _snapshot;
		static TimeStamp _intervalStart;
		static std::uint32_t _frames;
		static double _frameTimeSum;
		static float _maxFrameTime;
		static double _phaseTimeSums[(std::size_t)Phase::Count];
		static double _drawCallSum;
		static double _renderCommandSum;
		static CounterAccumulator _counters[MaxCounters];
		static std::uint32_t _counterCount;

		static void ResetAccumulators();
		static void PublishSnapshot();
		static void QueryMemoryUsage(std::uint64_t& used, std::uint64_t& total);
	};
}
