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
#if defined(WITH_THREADS)
			, _decodeThreadCreated(false), _decodeThreadShouldQuit(false)
#endif
	{
	}

	AudioDeviceBase::~AudioDeviceBase()
	{
		// The backend destructor is expected to have done this already, it runs first and the
		// decoding thread must not outlive the readers it touches
		shutdownDecodeThread();
	}

	void AudioDeviceBase::setSourcePool(ArrayView<const std::uint32_t> sourceIds)
	{
		_sourcePool.clear();
		_sourcePool.reserve(sourceIds.size());
		// Backwards, so the pool hands out the first source id first
		for (std::size_t i = sourceIds.size(); i > 0; i--) {
			_sourcePool.push_back(sourceIds[i - 1]);
		}
	}

	const IAudioPlayer* AudioDeviceBase::player(std::uint32_t index) const
	{
		if (index < _players.size()) {
			return _players[index];
		}
		return nullptr;
	}

	IAudioPlayer* AudioDeviceBase::player(std::uint32_t index)
	{
		if (index < _players.size()) {
			return _players[index];
		}
		return nullptr;
	}

	void AudioDeviceBase::stopPlayers()
	{
		// Iterating backwards because stop() unregisters the player, erasing it from the array
		for (std::size_t i = _players.size(); i > 0; i--) {
			_players[i - 1]->stop();
		}
		_players.clear();
	}

	void AudioDeviceBase::pausePlayers()
	{
		for (auto& player : _players) {
			player->pause();
		}
		// The players stay registered: a paused player still owns its source (that is what lets play()
		// resume it from where it is), so dropping it from the array only loses track of the source.
		// It could then never come back to the pool - not when the player is resumed and runs out, because
		// updatePlayers() no longer reaches it, and not when it is paused for good either.
	}

	void AudioDeviceBase::stopPlayers(PlayerType playerType)
	{
		const Object::ObjectType objectType = (playerType == PlayerType::Buffer)
			? AudioBufferPlayer::sType()
			: AudioStreamPlayer::sType();

		// Iterating backwards because stop() unregisters the player, erasing it from the array
		for (std::size_t i = _players.size(); i > 0; i--) {
			IAudioPlayer* player = _players[i - 1];
			if (player->type() == objectType) {
				player->stop();
			}
		}
	}

	void AudioDeviceBase::pausePlayers(PlayerType playerType)
	{
		const Object::ObjectType objectType = (playerType == PlayerType::Buffer)
			? AudioBufferPlayer::sType()
			: AudioStreamPlayer::sType();

		// The players stay registered, see pausePlayers() above
		for (auto& player : _players) {
			if (player->type() == objectType) {
				player->pause();
			}
		}
	}

	void AudioDeviceBase::freezePlayers()
	{
		for (auto& player : _players) {
			player->pause();
		}
		// The players array is not cleared at this point, it is needed as-is by the unfreeze method
	}

	void AudioDeviceBase::unfreezePlayers()
	{
		for (auto& player : _players) {
			player->play();
		}
	}

	std::uint32_t AudioDeviceBase::registerPlayer(IAudioPlayer* player)
	{
		if (_sourcePool.empty()) {
			reportNoAvailableSources();
			return UnavailableSource;
		}

		std::uint32_t sourceId = _sourcePool.pop_back_val();
		_players.push_back(player);
		return sourceId;
	}

	void AudioDeviceBase::reportNoAvailableSources()
	{
#if defined(DEATH_TRACE)
		// A device that never created a source in the first place already said so when it failed, and there
		// is nothing here to describe - this is the null device and the backends that could not initialize
		if (_players.empty()) {
			return;
		}

		// Every caller that keeps a sound around retries it on the next frame, so a line per refused play()
		// is thousands of lines a second for as long as the condition lasts. That buries the part of the log
		// this would be diagnosed from and costs more time than the audio it replaced, so it is said once
		// per interval instead, with what is holding the sources: a looping count close to the total means a
		// looping sound that nobody stopped, a low one means genuinely more sounds at once than there is
		// room for.
		_suppressedSourceWarnings++;
		if (_lastSourceWarningTime.ticks() != 0 && _lastSourceWarningTime.secondsSince() < SourceWarningIntervalSecs) {
			return;
		}
		_lastSourceWarningTime = TimeStamp::now();

		std::uint32_t numLooping = 0, numStreamed = 0;
		for (auto& activePlayer : _players) {
			if (activePlayer->type() == AudioStreamPlayer::sType()) {
				numStreamed++;
			} else if (activePlayer->isLooping()) {
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

	void AudioDeviceBase::unregisterPlayer(IAudioPlayer* player)
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

	void AudioDeviceBase::updatePlayers()
	{
		// Iterating backwards because a finished player unregisters itself, erasing it from the array
		for (std::size_t i = _players.size(); i > 0; i--) {
			_players[i - 1]->updateState();
		}

		checkForStalledSources();
	}

	void AudioDeviceBase::beginBlockingOperation()
	{
		// Counted rather than latched: two owners open these windows independently (ContentResolver's load
		// and the episode scan), and a backend that stops its stream on a flag would have the inner end
		// reopen it in the middle of the outer block - on the PlayStation 2 that is the module repeating
		// its last buffer, the artefact the mechanism exists to prevent
		_blockingOperationDepth++;
		if (_blockingOperationDepth == 1) {
			onBlockingOperationBegan();
		}
	}

	void AudioDeviceBase::endBlockingOperation()
	{
		if (_blockingOperationDepth <= 0) {
			// An end without a matching begin is a caller bug, not something to act on - reopening a stream
			// nobody stopped would be the damaging direction
			return;
		}
		_blockingOperationDepth--;
		if (_blockingOperationDepth == 0) {
			onBlockingOperationEnded();
		}
	}

	void AudioDeviceBase::checkForStalledSources()
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

		// Every source is taken and the loop above released none of them. For a scene with more sounds at
		// once than the device has room for that is normal and passes by itself - the samples end and the
		// pool refills. What it must not be is permanent, and it becomes permanent as soon as the backend
		// stops mixing without saying so: an output the system took back while the application was in the
		// background, a device that went away and never came back. Every source then reads as playing for
		// good, no player ever reaches the "finished" branch of its updateState(), and from that point on
		// the application is silent for the rest of the session while every play() fails.
		//
		// The playback positions are what tells the two apart, and they cost nothing to ask for on the
		// frames this looks at - only those where nothing is free anyway. A mixer that is running moves
		// them; one that has stopped does not move any of them, ever.
		std::int64_t progress = 0;
		std::uint32_t numPlaying = 0;
		std::uint32_t numWithOffset = 0;
		for (auto& player : _players) {
			// Only the ones that claim to be playing: a paused player is supposed to stand still, and a
			// pause menu holding every source would otherwise look exactly like a stalled device
			if (player->isPlaying()) {
				const std::int32_t offset = player->sampleOffset();
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
			// Not every backend can answer this question. AICA returns 0 for every STREAMED source by
			// construction (Dreamcast), and ASND returns 0 for any source whose buffer is not queued yet
			// (Wii, GameCube) - so a pool held entirely by streams reads as a constant sum on perfectly
			// good hardware, and would be given up on after the timeout below. A sum that is flat at zero
			// is no evidence of a stall, it is the absence of evidence either way, so nothing is judged
			// on it - a device those backends really did lose is left to their own recovery.
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

		// Nothing of this is audible in any case, and letting the players go is what keeps the application
		// able to play again the moment the device does mix: the pool goes back to full, and a source that
		// a stopped device refuses to start is simply reaped on the next frame instead of held forever
		stopPlayers();
		_stalledSince = TimeStamp{};
	}

	bool AudioDeviceBase::submitStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request)
	{
#if defined(WITH_THREADS)
		_decodeMutex.Lock();
		if (!_decodeThreadCreated) {
			// The decoding thread is created lazily on the first streamed sound
			_decodeThreadCreated = true;
			_decodeThread = Thread(AudioDeviceBase::decodeThreadFunc, this);
		}
		_decodeQueue.push_back(request);
		_decodeMutex.Unlock();
		_decodeQueueCond.Signal();
		return true;
#else
		return false;
#endif
	}

	void AudioDeviceBase::drainStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request)
	{
#if defined(WITH_THREADS)
		// A null request would compare equal to the idle active request and wait forever
		if (request == nullptr) {
			return;
		}

		_decodeMutex.Lock();
		// Remove the request from the queue if it hasn't been picked up yet
		for (std::size_t i = 0; i < _decodeQueue.size(); i++) {
			if (_decodeQueue[i] == request) {
				_decodeQueue.erase(&_decodeQueue[i]);
				request->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
				_decodeMutex.Unlock();
				return;
			}
		}
		// Wait for the decoding thread if the request is currently being executed
		while (_activeDecodeRequest == request) {
			_decodeDoneCond.Wait(_decodeMutex);
		}
		_decodeMutex.Unlock();
#endif
	}

	void AudioDeviceBase::shutdownDecodeThread()
	{
#if defined(WITH_THREADS)
		_decodeMutex.Lock();
		if (_decodeThreadShouldQuit) {
			_decodeMutex.Unlock();
			return;
		}
		_decodeThreadShouldQuit = true;
		// Requests still in the queue will never be executed, reset them so their owners don't wait forever
		for (auto& request : _decodeQueue) {
			request->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
		}
		_decodeQueue.clear();
		_decodeMutex.Unlock();
		_decodeQueueCond.Broadcast();
		if (_decodeThreadCreated) {
			_decodeThread.Join();
			_decodeThreadCreated = false;
		}
#endif
	}

#if defined(WITH_THREADS)
	void AudioDeviceBase::decodeThreadFunc(void* arg)
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

			device->_activeDecodeRequest = std::move(device->_decodeQueue.front());
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
#endif

	const Vector3f& AudioDeviceBase::getListenerPosition() const
	{
		return _listenerPos;
	}
}
