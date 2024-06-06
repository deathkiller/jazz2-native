#pragma once

#include "../Primitives/Vector3.h"
#include "../Base/FrameTimer.h"

namespace nCine
{
	class IAudioPlayer;

	/// Audio device interface class
	class IAudioDevice
	{
	public:
		static constexpr unsigned int UnavailableSource = ~0u;

		static constexpr float LengthToPhysical = 1.0f / 100.0f;
		static constexpr float VelocityToPhysical = FrameTimer::FramesPerSecond / 100.0f;
		static constexpr float ReferenceDistance = 200.0f * LengthToPhysical;
		static constexpr float MaxDistance = 900.0f * LengthToPhysical;

		enum class PlayerType {
			Buffer,
			Stream
		};

		virtual bool isValid() const = 0;

		virtual const char* name() const = 0;

		virtual ~IAudioDevice() = 0;
		/// Returns the listener gain value
		virtual float gain() const = 0;
		/// Sets the listener gain value
		virtual void setGain(float gain) = 0;

		/// Returns the maximum number of active players
		virtual unsigned int maxNumPlayers() const = 0;
		/// Returns the number of active players
		virtual unsigned int numPlayers() const = 0;
		/// Returns the specified running player object
		virtual const IAudioPlayer* player(unsigned int index) const = 0;

		/// Stops every player currently playing
		virtual void stopPlayers() = 0;
		/// Pauses every player currently playing
		virtual void pausePlayers() = 0;
		/// Stops every player of the specified type
		virtual void stopPlayers(PlayerType playerType) = 0;
		/// Pauses every player of the specified type
		virtual void pausePlayers(PlayerType playerType) = 0;

		/// Pauses every player currently playing without unregistering it
		virtual void freezePlayers() = 0;
		/// Resumes every player previoulsy "frozen" to a playing state
		virtual void unfreezePlayers() = 0;

		/// Registers a new stream player for buffer update
		virtual unsigned int registerPlayer(IAudioPlayer* player) = 0;
		/// Unregisters a stream player
		virtual void unregisterPlayer(IAudioPlayer* player) = 0;
		/// Updates players state (and buffer queue in the case of stream players)
		virtual void updatePlayers() = 0;

		virtual const Vector3f& getListenerPosition() const = 0;
		virtual void updateListener(const Vector3f& position, const Vector3f& velocity) = 0;

		virtual int nativeFrequency() = 0;

		virtual void suspendDevice() = 0;
		virtual void resumeDevice() = 0;
	};

	inline IAudioDevice::~IAudioDevice() { }

	/// A fake audio device which doesn't play anything
	class NullAudioDevice : public IAudioDevice
	{
	public:
		bool isValid() const override {
			return false;
		}

		const char* name() const override {
			return "NullAudioDevice";
		}

		float gain() const override {
			return 1.0f;
		}
		void setGain(float gain) override { }

		unsigned int maxNumPlayers() const override {
			return 0;
		}
		unsigned int numPlayers() const override {
			return 0;
		}
		const IAudioPlayer* player(unsigned int index) const override {
			return nullptr;
		}

		void stopPlayers() override { }
		void pausePlayers() override { }
		void stopPlayers(PlayerType playerType) override { }
		void pausePlayers(PlayerType playerType) override { }

		void freezePlayers() override { }
		void unfreezePlayers() override { }

		unsigned int registerPlayer(IAudioPlayer* player) override { return UnavailableSource; }
		void unregisterPlayer(IAudioPlayer* player) override { }
		void updatePlayers() override {}
		const Vector3f& getListenerPosition() const override { return Vector3f::Zero; }
		void updateListener(const Vector3f& position, const Vector3f& velocity) override { }
		int nativeFrequency() override { return 0; }

		void suspendDevice() override { }
		void resumeDevice() override { }
	};
}
