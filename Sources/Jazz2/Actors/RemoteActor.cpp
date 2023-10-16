#include "RemoteActor.h"

#if defined(WITH_MULTIPLAYER)

#include "../../nCine/Base/Clock.h"

namespace Jazz2::Actors
{
	RemoteActor::RemoteActor()
		: _stateBufferPos(0), _lastAnim(AnimState::Idle)
	{
	}

	Task<bool> RemoteActor::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::ForceDisableCollisions | ActorState::PreserveOnRollback | ActorState::ExcludeSimilar, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		Clock& c = nCine::clock();
		std::uint64_t now = c.now() * 1000 / c.frequency();
		for (std::int32_t i = 0; i < countof(_stateBuffer); i++) {
			_stateBuffer[i].Time = now - countof(_stateBuffer) + i;
			_stateBuffer[i].Pos = Vector2f(details.Pos.X, details.Pos.Y);
		}

		async_await RequestMetadataAsync("Interactive/PlayerJazz"_s);

		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void RemoteActor::OnUpdate(float timeMult)
	{
		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();
		std::int64_t renderTime = now - ServerDelay;

		std::int32_t nextIdx = _stateBufferPos - 1;
		if (nextIdx < 0) {
			nextIdx += countof(_stateBuffer);
		}

		if (renderTime <= _stateBuffer[nextIdx].Time) {
			std::int32_t prevIdx;
			while (true) {
				prevIdx = nextIdx - 1;
				if (prevIdx < 0) {
					prevIdx += countof(_stateBuffer);
				}

				if (prevIdx == _stateBufferPos || _stateBuffer[prevIdx].Time <= renderTime) {
					break;
				}

				nextIdx = prevIdx;
			}

			Vector2f pos;
			std::int64_t timeRange = (_stateBuffer[nextIdx].Time - _stateBuffer[prevIdx].Time);
			if (timeRange > 0) {
				float lerp = (float)(renderTime - _stateBuffer[prevIdx].Time) / timeRange;
				pos = _stateBuffer[prevIdx].Pos + (_stateBuffer[nextIdx].Pos - _stateBuffer[prevIdx].Pos) * lerp;
			} else {
				pos = _stateBuffer[nextIdx].Pos;
			}

			MoveInstantly(pos, MoveType::Absolute);
		}

		ActorBase::OnUpdate(timeMult);
	}

	void RemoteActor::SyncWithServer(const Vector2f& pos, AnimState anim, bool isVisible, bool isFacingLeft)
	{
		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();

		bool wasVisible = _renderer.isDrawEnabled();
		_renderer.setDrawEnabled(isVisible);
		SetFacingLeft(isFacingLeft);

		if (_lastAnim != anim) {
			_lastAnim = anim;
			SetAnimation(anim);
		}

		if (wasVisible) {
			// Actor is still visible, enable interpolation
			_stateBuffer[_stateBufferPos].Time = now;
			_stateBuffer[_stateBufferPos].Pos = pos;
		} else {
			// Actor was hidden before, reset state buffer to disable interpolation
			std::int32_t stateBufferPrevPos = _stateBufferPos - 1;
			if (stateBufferPrevPos < 0) {
				stateBufferPrevPos += countof(_stateBuffer);
			}

			std::int64_t renderTime = now - ServerDelay;

			_stateBuffer[stateBufferPrevPos].Time = renderTime;
			_stateBuffer[stateBufferPrevPos].Pos = pos;
			_stateBuffer[_stateBufferPos].Time = renderTime;
			_stateBuffer[_stateBufferPos].Pos = pos;
		}

		_stateBufferPos++;
		if (_stateBufferPos >= countof(_stateBuffer)) {
			_stateBufferPos = 0;
		}
	}
}

#endif