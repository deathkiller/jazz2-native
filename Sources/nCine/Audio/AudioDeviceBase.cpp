#include "AudioDeviceBase.h"
#include "AudioBufferPlayer.h"
#include "AudioStreamPlayer.h"

namespace nCine
{
	AudioDeviceBase::AudioDeviceBase()
		: gain_(1.0f)
#if defined(WITH_THREADS)
			, decodeThreadCreated_(false), decodeThreadShouldQuit_(false)
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
		sourcePool_.clear();
		sourcePool_.reserve(sourceIds.size());
		// Backwards, so the pool hands out the first source id first
		for (std::size_t i = sourceIds.size(); i > 0; i--) {
			sourcePool_.push_back(sourceIds[i - 1]);
		}
	}

	const IAudioPlayer* AudioDeviceBase::player(std::uint32_t index) const
	{
		if (index < players_.size()) {
			return players_[index];
		}
		return nullptr;
	}

	IAudioPlayer* AudioDeviceBase::player(std::uint32_t index)
	{
		if (index < players_.size()) {
			return players_[index];
		}
		return nullptr;
	}

	void AudioDeviceBase::stopPlayers()
	{
		// Iterating backwards because stop() unregisters the player, erasing it from the array
		for (std::size_t i = players_.size(); i > 0; i--) {
			players_[i - 1]->stop();
		}
		players_.clear();
	}

	void AudioDeviceBase::pausePlayers()
	{
		for (auto& player : players_) {
			player->pause();
		}
		players_.clear();
	}

	void AudioDeviceBase::stopPlayers(PlayerType playerType)
	{
		const Object::ObjectType objectType = (playerType == PlayerType::Buffer)
			? AudioBufferPlayer::sType()
			: AudioStreamPlayer::sType();

		// Iterating backwards because stop() unregisters the player, erasing it from the array
		for (std::size_t i = players_.size(); i > 0; i--) {
			IAudioPlayer* player = players_[i - 1];
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
		for (std::size_t i = players_.size(); i > 0; i--) {
			IAudioPlayer* player = players_[i - 1];
			if (player->type() == objectType) {
				player->pause();
				players_.eraseUnordered(&players_[i - 1]);
			}
		}
	}

	void AudioDeviceBase::freezePlayers()
	{
		for (auto& player : players_) {
			player->pause();
		}
		// The players array is not cleared at this point, it is needed as-is by the unfreeze method
	}

	void AudioDeviceBase::unfreezePlayers()
	{
		for (auto& player : players_) {
			player->play();
		}
	}

	std::uint32_t AudioDeviceBase::registerPlayer(IAudioPlayer* player)
	{
		if (sourcePool_.empty()) {
			return UnavailableSource;
		}

		std::uint32_t sourceId = sourcePool_.pop_back_val();
		players_.push_back(player);
		return sourceId;
	}

	void AudioDeviceBase::unregisterPlayer(IAudioPlayer* player)
	{
		if (player->sourceId_ == UnavailableSource) {
			return;
		}

		sourcePool_.push_back(player->sourceId_);
		player->sourceId_ = UnavailableSource;

		auto it = players_.begin();
		while (it != players_.end()) {
			if (*it == player) {
				players_.erase(it);
				break;
			}
			++it;
		}
	}

	void AudioDeviceBase::updatePlayers()
	{
		// Iterating backwards because a finished player unregisters itself, erasing it from the array
		for (std::size_t i = players_.size(); i > 0; i--) {
			players_[i - 1]->updateState();
		}
	}

	bool AudioDeviceBase::submitStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request)
	{
#if defined(WITH_THREADS)
		decodeMutex_.Lock();
		if (!decodeThreadCreated_) {
			// The decoding thread is created lazily on the first streamed sound
			decodeThreadCreated_ = true;
			decodeThread_ = Thread(AudioDeviceBase::decodeThreadFunc, this);
		}
		decodeQueue_.push_back(request);
		decodeMutex_.Unlock();
		decodeQueueCond_.Signal();
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

		decodeMutex_.Lock();
		// Remove the request from the queue if it hasn't been picked up yet
		for (std::size_t i = 0; i < decodeQueue_.size(); i++) {
			if (decodeQueue_[i] == request) {
				decodeQueue_.erase(&decodeQueue_[i]);
				request->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
				decodeMutex_.Unlock();
				return;
			}
		}
		// Wait for the decoding thread if the request is currently being executed
		while (activeDecodeRequest_ == request) {
			decodeDoneCond_.Wait(decodeMutex_);
		}
		decodeMutex_.Unlock();
#endif
	}

	void AudioDeviceBase::shutdownDecodeThread()
	{
#if defined(WITH_THREADS)
		decodeMutex_.Lock();
		if (decodeThreadShouldQuit_) {
			decodeMutex_.Unlock();
			return;
		}
		decodeThreadShouldQuit_ = true;
		// Requests still in the queue will never be executed, reset them so their owners don't wait forever
		for (auto& request : decodeQueue_) {
			request->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
		}
		decodeQueue_.clear();
		decodeMutex_.Unlock();
		decodeQueueCond_.Broadcast();
		if (decodeThreadCreated_) {
			decodeThread_.Join();
			decodeThreadCreated_ = false;
		}
#endif
	}

#if defined(WITH_THREADS)
	void AudioDeviceBase::decodeThreadFunc(void* arg)
	{
		Thread::SetCurrentName("Audio decoding");

		AudioDeviceBase* device = static_cast<AudioDeviceBase*>(arg);
		device->decodeMutex_.Lock();
		while (true) {
			while (device->decodeQueue_.empty() && !device->decodeThreadShouldQuit_) {
				device->decodeQueueCond_.Wait(device->decodeMutex_);
			}
			if (device->decodeThreadShouldQuit_) {
				break;
			}

			device->activeDecodeRequest_ = std::move(device->decodeQueue_.front());
			device->decodeQueue_.erase(device->decodeQueue_.begin());
			device->decodeMutex_.Unlock();

			// Decoding is executed without holding the lock, so new requests can still be submitted
			device->activeDecodeRequest_->Execute();

			device->decodeMutex_.Lock();
			device->activeDecodeRequest_ = nullptr;
			device->decodeDoneCond_.Broadcast();
		}
		device->decodeMutex_.Unlock();
	}
#endif

	const Vector3f& AudioDeviceBase::getListenerPosition() const
	{
		return listenerPos_;
	}
}
