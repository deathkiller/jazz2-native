#pragma once

#define NCINE_INCLUDE_OPENALC
#include "../CommonHeaders.h"

#include "IAudioDevice.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/// It represents the interface to the OpenAL audio device
	class ALAudioDevice : public IAudioDevice
	{
	public:
		ALAudioDevice();
		~ALAudioDevice() override;

		inline const char* name() const override {
			return deviceName_;
		}

		float gain() const override {
			return gain_;
		}
		void setGain(float gain) override;

		inline unsigned int maxNumPlayers() const override {
			return MaxSources;
		}
		inline unsigned int numPlayers() const override {
			return (unsigned int)players_.size();
		}
		const IAudioPlayer* player(unsigned int index) const override;

		void stopPlayers() override;
		void pausePlayers() override;
		void stopPlayers(PlayerType playerType) override;
		void pausePlayers(PlayerType playerType) override;

		void freezePlayers() override;
		void unfreezePlayers() override;

		unsigned int nextAvailableSource() override;
		void registerPlayer(IAudioPlayer* player) override;
		void unregisterPlayer(IAudioPlayer* player) override;
		void updatePlayers() override;
		
		void updateListener(const Vector3f& position, const Vector3f& velocity) override;

		int nativeFrequency() override;

	private:
		/// Maximum number of OpenAL sources
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_EMSCRIPTEN)
		static const unsigned int MaxSources = 32;
#else
		static const unsigned int MaxSources = 64;
#endif

		/// The OpenAL device
		ALCdevice* device_;
		/// The OpenAL context for the device
		ALCcontext* context_;
		/// The listener gain value (master volume)
		ALfloat gain_;
		/// The sources pool
		ALuint sources_[MaxSources];
		/// The array of currently active audio players
		SmallVector<IAudioPlayer*, MaxSources> players_;
		/// native device frequency
		int nativeFreq_;

		/// The OpenAL device name string
		const char* deviceName_;

		/// Deleted copy constructor
		ALAudioDevice(const ALAudioDevice&) = delete;
		/// Deleted assignment operator
		ALAudioDevice& operator=(const ALAudioDevice&) = delete;
	};

}
