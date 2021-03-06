#include "ALAudioDevice.h"
#include "AudioBufferPlayer.h"
#include "AudioStreamPlayer.h"
#include "../ServiceLocator.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	ALAudioDevice::ALAudioDevice()
		: device_(nullptr), context_(nullptr), gain_(1.0f), deviceName_(nullptr), nativeFreq_(44100)
	{
		device_ = alcOpenDevice(nullptr);
		RETURN_ASSERT_MSG_X(device_ != nullptr, "alcOpenDevice failed: 0x%x", alGetError());
		deviceName_ = alcGetString(device_, ALC_DEVICE_SPECIFIER);

		context_ = alcCreateContext(device_, nullptr);
		if (context_ == nullptr) {
			alcCloseDevice(device_);
			RETURN_MSG_X("alcCreateContext failed: 0x%x", alGetError());
		}

#if !defined(__EMSCRIPTEN__)
		// Try to get native sample rate of default audio device
		ALCint nativeFreq = 0;
		alcGetIntegerv(device_, ALC_FREQUENCY, 1, &nativeFreq);
		if (nativeFreq >= 44100 && nativeFreq <= 192000) {
			nativeFreq_ = nativeFreq;
		}
#endif

		if (!alcMakeContextCurrent(context_)) {
			alcDestroyContext(context_);
			alcCloseDevice(device_);
			RETURN_MSG_X("alcMakeContextCurrent failed: 0x%x", alGetError());
		}

		alGetError();
		alGenSources(MaxSources, sources_);
		const ALenum error = alGetError();
		if (error != AL_NO_ERROR) {
			LOGE_X("alGenSources failed: 0x%x", error);
		}

		alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
		alDopplerFactor(1.0f);
		alSpeedOfSound(360.0f);
		alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
		alListenerf(AL_GAIN, gain_);
	}

	ALAudioDevice::~ALAudioDevice()
	{
		for (ALuint sourceId : sources_) {
			alSourcei(sourceId, AL_BUFFER, AL_NONE);
		}
		alDeleteSources(MaxSources, sources_);

		alcDestroyContext(context_);

		ALCboolean result = alcCloseDevice(device_);
		//FATAL_ASSERT_MSG_X(result, "alcCloseDevice failed: %d", alGetError());
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void ALAudioDevice::setGain(ALfloat gain)
	{
		gain_ = gain;
		alListenerf(AL_GAIN, gain_);
	}

	const IAudioPlayer* ALAudioDevice::player(unsigned int index) const
	{
		if (index < players_.size())
			return players_[index];

		return nullptr;
	}

	void ALAudioDevice::stopPlayers()
	{
		for (auto& player : players_) {
			player->stop();
		}
		players_.clear();
	}

	void ALAudioDevice::pausePlayers()
	{
		for (auto& player : players_) {
			player->pause();
		}
		players_.clear();
	}

	void ALAudioDevice::stopPlayers(PlayerType playerType)
	{
		const Object::ObjectType objectType = (playerType == PlayerType::Buffer)
			? AudioBufferPlayer::sType()
			: AudioStreamPlayer::sType();

		for (int i = (int)players_.size() - 1; i >= 0; i--) {
			if (players_[i]->type() == objectType) {
				players_[i]->stop();
				players_.erase(&players_[i]);
			}
		}
	}

	void ALAudioDevice::pausePlayers(PlayerType playerType)
	{
		const Object::ObjectType objectType = (playerType == PlayerType::Buffer)
			? AudioBufferPlayer::sType()
			: AudioStreamPlayer::sType();

		for (int i = (int)players_.size() - 1; i >= 0; i--) {
			if (players_[i]->type() == objectType) {
				players_[i]->pause();
				players_.erase(&players_[i]);
			}
		}
	}

	void ALAudioDevice::freezePlayers()
	{
		for (auto& player : players_) {
			player->pause();
		}
		// The players array is not cleared at this point, it is needed as-is by the unfreeze method
	}

	void ALAudioDevice::unfreezePlayers()
	{
		for (auto& player : players_) {
			player->play();
		}
	}

	unsigned int ALAudioDevice::nextAvailableSource()
	{
		ALint sourceState;

		for (ALuint sourceId : sources_) {
			alGetSourcei(sourceId, AL_SOURCE_STATE, &sourceState);
			if (sourceState != AL_PLAYING && sourceState != AL_PAUSED)
				return sourceId;
		}

		return UnavailableSource;
	}

	void ALAudioDevice::registerPlayer(IAudioPlayer* player)
	{
		//ASSERT(player);
		//ASSERT(players_.size() < MaxSources);

		if (players_.size() < MaxSources) {
			players_.push_back(player);
		}
	}

	void ALAudioDevice::unregisterPlayer(IAudioPlayer* player)
	{
		auto it = players_.begin();
		while (it != players_.end()) {
			if (*it == player) {
				players_.erase(it);
				break;
			} else {
				++it;
			}
		}
	}

	void ALAudioDevice::updatePlayers()
	{
		for (int i = (int)players_.size() - 1; i >= 0; i--) {
			if (players_[i]->isPlaying()) {
				players_[i]->updateState();
			} else {
				players_.erase(&players_[i]);
			}
		}
	}

	void ALAudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		alListener3f(AL_POSITION, position.X * LengthToPhysical, position.Y * -LengthToPhysical, position.Z * -LengthToPhysical);
		alListener3f(AL_VELOCITY, velocity.X * VelocityToPhysical, velocity.Y * -VelocityToPhysical, velocity.Z * -VelocityToPhysical);
	}

	int ALAudioDevice::nativeFrequency()
	{
		return nativeFreq_;
	}
}
