#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../nCine/Base/Clock.h"
#include "../../../nCine/Primitives/Vector2.h"

using namespace nCine;

namespace Jazz2::Actors::Multiplayer
{
	/**
		@brief Interpolation buffer for actor positions received from a remote authority

		Ring buffer of timestamped positions received over the network, sampled @ref ServerDelay milliseconds in
		the past so movement stays smooth between (and across late) updates. Shared by the server-side shadow of
		a remote player (@ref RemotePlayerOnServer) and by client-side remote actors (@ref RemoteActor).
	*/
	class StateInterpolationBuffer
	{
	public:
		/** @brief How far in the past the received state is displayed, in milliseconds */
		static constexpr std::int64_t ServerDelay = 64;

		/** @brief Returns the current timestamp of the interpolation clock, in milliseconds */
		static std::int64_t Now() {
			Clock& c = nCine::clock();
			return std::int64_t(c.now() * 1000 / c.frequency());
		}

		/** @brief Resets the whole buffer to a single position (spawn, warp, forced resync), disabling interpolation */
		void Reset(Vector2f pos, std::int64_t now) {
			for (std::int32_t i = 0; i < BufferSize; i++) {
				_frames[i].Time = now;
				_frames[i].Pos = pos;
			}
		}

		/** @brief Returns the most recently pushed position */
		Vector2f GetLatest() const {
			std::int32_t idx = _cursor - 1;
			if (idx < 0) {
				idx += BufferSize;
			}
			return _frames[idx].Pos;
		}

		/**
		 * @brief Pushes a newly received position
		 *
		 * When @p wasVisible is `false`, the actor was hidden before this update, so the previous and current
		 * frames are collapsed to the new position to disable interpolation across the gap.
		 */
		void Push(Vector2f pos, std::int64_t now, bool wasVisible) {
			if (wasVisible) {
				// Actor is still visible, enable interpolation
				_frames[_cursor].Time = now;
				_frames[_cursor].Pos = pos;
			} else {
				// Actor was hidden before, reset state buffer to disable interpolation
				std::int32_t prevIdx = _cursor - 1;
				if (prevIdx < 0) {
					prevIdx += BufferSize;
				}

				std::int64_t renderTime = now - ServerDelay;

				_frames[prevIdx].Time = renderTime;
				_frames[prevIdx].Pos = pos;
				_frames[_cursor].Time = renderTime;
				_frames[_cursor].Pos = pos;
			}

			_cursor++;
			if (_cursor >= BufferSize) {
				_cursor = 0;
			}
		}

		/**
		 * @brief Samples the interpolated position at the given time
		 *
		 * @return `true` and writes @p result when @p renderTime is covered by the buffer; `false` when it is
		 * ahead of the newest received state, in which case @p result is left untouched.
		 */
		bool Sample(std::int64_t renderTime, Vector2f& result) const {
			std::int32_t nextIdx = _cursor - 1;
			if (nextIdx < 0) {
				nextIdx += BufferSize;
			}

			if (renderTime > _frames[nextIdx].Time) {
				return false;
			}

			std::int32_t prevIdx;
			while (true) {
				prevIdx = nextIdx - 1;
				if (prevIdx < 0) {
					prevIdx += BufferSize;
				}

				if (prevIdx == _cursor || _frames[prevIdx].Time <= renderTime) {
					break;
				}

				nextIdx = prevIdx;
			}

			std::int64_t timeRange = (_frames[nextIdx].Time - _frames[prevIdx].Time);
			if (timeRange > 0) {
				float lerp = (float)(renderTime - _frames[prevIdx].Time) / timeRange;
				result = _frames[prevIdx].Pos + (_frames[nextIdx].Pos - _frames[prevIdx].Pos) * lerp;
			} else {
				result = _frames[nextIdx].Pos;
			}
			return true;
		}

	private:
		struct StateFrame {
			std::int64_t Time;
			Vector2f Pos;
		};

		static constexpr std::int32_t BufferSize = 8;

		StateFrame _frames[BufferSize];
		std::int32_t _cursor = 0;
	};
}

#endif
