#include "AudioDeviceBase.h"
#include "AudioBufferPlayer.h"
#include "AudioStreamPlayer.h"

namespace nCine
{
	AudioDeviceBase::AudioDeviceBase()
		: _gain(1.0f), _lastSourceProgress(0), _stalledCheckLeft(0), _blockingOperationDepth(0)
#if defined(DEATH_TRACE)
			, _suppressedSourceWarnings(0)
#endif
#if defined(WITH_THREADS) && !defined(NCINE_HAS_NATIVE_AUDIO)
			, _decodeThreadCreated(false), _decodeThreadShouldQuit(false)
#endif
	{
	}

	AudioDeviceBase::~AudioDeviceBase()
	{
#if !defined(NCINE_HAS_NATIVE_AUDIO)
		// The backend destructor is expected to have done this already, it runs first and the
		// decoding thread must not outlive the readers it touches
		ShutdownDecodeThread();
#endif
	}

	void AudioDeviceBase::SetSourcePool(ArrayView<const std::uint32_t> sourceIds)
	{
		_sourcePool.clear();
		_sourcePool.reserve(sourceIds.size());
		// Backwards, so the pool hands out the first source id first
		for (std::size_t i = sourceIds.size(); i > 0; i--) {
			_sourcePool.push_back(sourceIds[i - 1]);
		}
	}

	const IAudioPlayer* AudioDeviceBase::GetPlayer(std::uint32_t index) const
	{
		if (index < _players.size()) {
			return _players[index];
		}
		return nullptr;
	}

	IAudioPlayer* AudioDeviceBase::GetPlayer(std::uint32_t index)
	{
		if (index < _players.size()) {
			return _players[index];
		}
		return nullptr;
	}

	void AudioDeviceBase::StopPlayers()
	{
		// Iterating backwards because Stop() unregisters the player, erasing it from the array
		for (std::size_t i = _players.size(); i > 0; i--) {
			_players[i - 1]->Stop();
		}
		_players.clear();
	}

	void AudioDeviceBase::PausePlayers()
	{
		for (auto& player : _players) {
			player->Pause();
		}
		// Keep paused players registered, they still own their source
	}

	void AudioDeviceBase::StopPlayers(PlayerType playerType)
	{
		const Object::ObjectType objectType = (playerType == PlayerType::Buffer)
			? AudioBufferPlayer::sType()
			: AudioStreamPlayer::sType();

		// Iterating backwards because Stop() unregisters the player, erasing it from the array
		for (std::size_t i = _players.size(); i > 0; i--) {
			IAudioPlayer* player = _players[i - 1];
			if (player->type() == objectType) {
				player->Stop();
			}
		}
	}

	void AudioDeviceBase::PausePlayers(PlayerType playerType)
	{
		const Object::ObjectType objectType = (playerType == PlayerType::Buffer)
			? AudioBufferPlayer::sType()
			: AudioStreamPlayer::sType();

		// The players stay registered, see PausePlayers() above
		for (auto& player : _players) {
			if (player->type() == objectType) {
				player->Pause();
			}
		}
	}

	void AudioDeviceBase::FreezePlayers()
	{
		for (auto& player : _players) {
			player->Pause();
		}
		// The players array is not cleared at this point, it is needed as-is by the unfreeze method
	}

	void AudioDeviceBase::UnfreezePlayers()
	{
		// Backwards, a stream that has nothing left to play unregisters itself
		for (std::size_t i = _players.size(); i > 0; i--) {
			_players[i - 1]->Play();
		}
	}

	std::uint32_t AudioDeviceBase::RegisterPlayer(IAudioPlayer* player)
	{
		if (_sourcePool.empty()) {
			ReportNoAvailableSources();
			return UnavailableSource;
		}

		std::uint32_t sourceId = _sourcePool.pop_back_val();
		_players.push_back(player);
		return sourceId;
	}

	void AudioDeviceBase::ReportNoAvailableSources()
	{
#if defined(DEATH_TRACE)
		// A device that never created a source in the first place already said so when it failed, and there
		// is nothing here to describe - this is the null device and the backends that could not initialize
		if (_players.empty()) {
			return;
		}

		// Rate-limited, refused players retry every frame. The looping count tells a leaked loop from a busy scene.
		_suppressedSourceWarnings++;
		if (_lastSourceWarningTime.ticks() != 0 && _lastSourceWarningTime.secondsSince() < SourceWarningIntervalSecs) {
			return;
		}
		_lastSourceWarningTime = TimeStamp::now();

		std::uint32_t numLooping = 0, numStreamed = 0;
		for (auto& activePlayer : _players) {
			if (activePlayer->type() == AudioStreamPlayer::sType()) {
				numStreamed++;
			} else if (activePlayer->IsLooping()) {
				numLooping++;
			}
		}

		std::uint32_t suppressed = _suppressedSourceWarnings - 1;
		_suppressedSourceWarnings = 0;

		if (suppressed > 0) {
			LOGW("No more available audio sources for playing - all {} are in use ({} looping, {} streamed), {} further attempts were not reported",
				_players.size(), numLooping, numStreamed, suppressed);
		} else {
			LOGW("No more available audio sources for playing - all {} are in use ({} looping, {} streamed)",
				_players.size(), numLooping, numStreamed);
		}
#endif
	}

	void AudioDeviceBase::UnregisterPlayer(IAudioPlayer* player)
	{
		if (player->_sourceId == UnavailableSource) {
			return;
		}

		_sourcePool.push_back(player->_sourceId);
		player->_sourceId = UnavailableSource;

		auto it = _players.begin();
		while (it != _players.end()) {
			if (*it == player) {
				_players.erase(it);
				break;
			}
			++it;
		}
	}

	void AudioDeviceBase::UpdatePlayers()
	{
		// Iterating backwards because a finished player unregisters itself, erasing it from the array
		for (std::size_t i = _players.size(); i > 0; i--) {
			_players[i - 1]->UpdateState();
		}

		CheckForStalledSources();
	}

	void AudioDeviceBase::BeginBlockingOperation()
	{
		// Counted, because blocking operations can nest (level load and episode scan)
		_blockingOperationDepth++;
		if (_blockingOperationDepth == 1) {
			OnBlockingOperationBegan();
		}
	}

	void AudioDeviceBase::EndBlockingOperation()
	{
		if (_blockingOperationDepth <= 0) {
			// An end without a matching begin is a caller bug, not something to act on - reopening a stream
			// nobody stopped would be the damaging direction
			return;
		}
		_blockingOperationDepth--;
		if (_blockingOperationDepth == 0) {
			OnBlockingOperationEnded();
		}
	}

	void AudioDeviceBase::CheckForStalledSources()
	{
		if (!_sourcePool.empty()) {
			// Something is free, so nothing is waiting and there is nothing to judge
			_stalledSince = TimeStamp{};
			_stalledCheckLeft = 0;
			return;
		}
		if (_stalledCheckLeft > 0) {
			_stalledCheckLeft--;
			return;
		}
		_stalledCheckLeft = StalledCheckInterval;

		// No source is free - normal for a busy scene, but permanent if the backend stopped mixing silently.
		// Playback positions tell the two apart, a stopped mixer never advances them.
		std::int64_t progress = 0;
		std::uint32_t numPlaying = 0;
		std::uint32_t numWithOffset = 0;
		for (auto& player : _players) {
			// Only the ones that claim to be playing: a paused player is supposed to stand still, and a
			// pause menu holding every source would otherwise look exactly like a stalled device
			if (player->IsPlaying()) {
				const std::int32_t offset = player->GetSampleOffset();
				progress += offset;
				numPlaying++;
				if (offset != 0) {
					numWithOffset++;
				}
			}
		}
		if (numPlaying == 0) {
			_stalledSince = TimeStamp{};
			return;
		}
		if (numWithOffset == 0) {
			// AICA and ASND report 0 for streamed or unqueued sources, so a sum stuck at zero proves nothing
			_stalledSince = TimeStamp{};
			return;
		}

		if (_stalledSince.ticks() == 0 || progress != _lastSourceProgress) {
			_stalledSince = TimeStamp::now();
			_lastSourceProgress = progress;
			return;
		}
		if (_stalledSince.secondsSince() < StalledTimeoutSecs) {
			return;
		}

		LOGW("No audio source has advanced in {} seconds while all {} are in use, the device seems to have stopped mixing - releasing them",
			StalledTimeoutSecs, _players.size());

		// Release everything, so playback recovers as soon as the device mixes again
		StopPlayers();
		_stalledSince = TimeStamp{};
	}

#if !defined(NCINE_HAS_NATIVE_AUDIO)
	bool AudioDeviceBase::SubmitStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request)
	{
#	if defined(WITH_THREADS)
		_decodeMutex.Lock();
		if (!_decodeThreadCreated) {
			// The decoding thread is created lazily on the first streamed sound
			_decodeThreadCreated = true;
			_decodeThread = Thread(AudioDeviceBase::DecodeThreadFunc, this);
		}
		_decodeQueue.push_back(request);
		_decodeMutex.Unlock();
		_decodeQueueCond.Signal();
		return true;
#	else
		return false;
#	endif
	}

	void AudioDeviceBase::DrainStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request)
	{
#	if defined(WITH_THREADS)
		// A null request would compare equal to the idle active request and wait forever
		if (request == nullptr) {
			return;
		}

		_decodeMutex.Lock();
		// Remove the request from the queue if it hasn't been picked up yet
		for (std::size_t i = 0; i < _decodeQueue.size(); i++) {
			if (_decodeQueue[i] == request) {
				_decodeQueue.erase(&_decodeQueue[i]);
				request->CurrentState.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
				_decodeMutex.Unlock();
				return;
			}
		}
		// Wait for the decoding thread if the request is currently being executed
		while (_activeDecodeRequest == request) {
			_decodeDoneCond.Wait(_decodeMutex);
		}
		_decodeMutex.Unlock();
#	endif
	}

	void AudioDeviceBase::ShutdownDecodeThread()
	{
#	if defined(WITH_THREADS)
		_decodeMutex.Lock();
		if (_decodeThreadShouldQuit) {
			_decodeMutex.Unlock();
			return;
		}
		_decodeThreadShouldQuit = true;
		// Requests still in the queue will never be executed, reset them so their owners don't wait forever
		for (auto& request : _decodeQueue) {
			request->CurrentState.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
		}
		_decodeQueue.clear();
		_decodeMutex.Unlock();
		_decodeQueueCond.Broadcast();
		if (_decodeThreadCreated) {
			_decodeThread.Join();
			_decodeThreadCreated = false;
		}
#	endif
	}

#	if defined(WITH_THREADS)
	void AudioDeviceBase::DecodeThreadFunc(void* arg)
	{
		Thread::SetCurrentName("Audio decoding");

		AudioDeviceBase* device = static_cast<AudioDeviceBase*>(arg);
		device->_decodeMutex.Lock();
		while (true) {
			while (device->_decodeQueue.empty() && !device->_decodeThreadShouldQuit) {
				device->_decodeQueueCond.Wait(device->_decodeMutex);
			}
			if (device->_decodeThreadShouldQuit) {
				break;
			}

			device->_activeDecodeRequest = Death::move(device->_decodeQueue.front());
			device->_decodeQueue.erase(device->_decodeQueue.begin());
			device->_decodeMutex.Unlock();

			// Decoding is executed without holding the lock, so new requests can still be submitted
			device->_activeDecodeRequest->Execute();

			device->_decodeMutex.Lock();
			device->_activeDecodeRequest = nullptr;
			device->_decodeDoneCond.Broadcast();
		}
		device->_decodeMutex.Unlock();
	}
#	endif
#endif

	const Vector3f& AudioDeviceBase::GetListenerPosition() const
	{
		return _listenerPos;
	}
}
