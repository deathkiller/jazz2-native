#include "AudioDeviceBase.h"
#include "AudioBufferPlayer.h"
#include "AudioStreamPlayer.h"

namespace nCine
{
	AudioDeviceBase::AudioDeviceBase()
		: _gain(1.0f)
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
		_players.clear();
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

		// Iterating backwards, so removing the current element doesn't affect the remaining ones
		for (std::size_t i = _players.size(); i > 0; i--) {
			IAudioPlayer* player = _players[i - 1];
			if (player->type() == objectType) {
				player->pause();
				_players.eraseUnordered(&_players[i - 1]);
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
			return UnavailableSource;
		}

		std::uint32_t sourceId = _sourcePool.pop_back_val();
		_players.push_back(player);
		return sourceId;
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
