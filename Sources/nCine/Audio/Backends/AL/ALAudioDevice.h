#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENALC
#include "../../../CommonHeaders.h"
#endif

#include "../../AudioDeviceBase.h"

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
#	include <CommonWindows.h>
#	include <mmdeviceapi.h>
#	include <audiopolicy.h>
#endif

#include <Containers/String.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief OpenAL implementation of @ref IAudioDevice

		Owns the OpenAL device and context, manages a fixed pool of sources and tracks the active
		players. On desktop Windows it also listens for default device changes and recreates the
		device when needed.
	*/
	class ALAudioDevice : public AudioDeviceBase
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		, public IMMNotificationClient
#endif
	{
	public:
		ALAudioDevice();
		~ALAudioDevice() override;

		bool isValid() const override;

		const char* name() const override;

		void setGain(float gain) override;

		void updateListener(const Vector3f& position, const Vector3f& velocity) override;

		std::int32_t nativeFrequency() override;

		std::uint32_t createBuffer(BufferUsage usage) override;
		void deleteBuffer(std::uint32_t bufferId) override;
		bool uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency) override;

		void setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override;
		void setSourceGain(std::uint32_t sourceId, float gain) override;
		void setSourcePitch(std::uint32_t sourceId, float pitch) override;
		void setSourceLooping(std::uint32_t sourceId, bool looping) override;
		void setSourceRelative(std::uint32_t sourceId, bool relative) override;
		void setSourcePosition(std::uint32_t sourceId, const Vector3f& position) override;
		void setSourceLowPass(std::uint32_t sourceId, float value) override;
		std::int32_t sourceSampleOffset(std::uint32_t sourceId) override;
		void setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset) override;
		void playSource(std::uint32_t sourceId) override;
		void pauseSource(std::uint32_t sourceId) override;
		void stopSource(std::uint32_t sourceId) override;
		bool isSourcePlaying(std::uint32_t sourceId) override;

		void queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override;
		std::int32_t numProcessedBuffers(std::uint32_t sourceId) override;
		void unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds) override;

#if defined(WITH_LIBRETRO)
		bool renderSamples(std::int16_t* buffer, std::int32_t numFrames) override;
#endif

		void suspendDevice() override;
		void resumeDevice() override;

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		void updatePlayers() override;
#endif

	private:
		/** @brief Maximum number of OpenAL sources */
#if defined(DEATH_TARGET_PSP)
		// Every source is mixed in software on the main CPU here, so this is a frame-time budget rather than
		// a memory one: each one that is actually playing costs a resample-and-accumulate pass over the
		// output block. Sixteen is well above what the game ever has sounding at once (the level handler
		// reaps finished players every frame), and it is the point past which a burst of simultaneous
		// explosions would start showing up in the frame time instead of just in the mix.
		static const std::int32_t MaxSources = 16;
#elif defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_IOS) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_VITA)
		static const std::int32_t MaxSources = 48;
#else
		static const std::int32_t MaxSources = 64;
#endif

		/** @brief OpenAL device */
		ALCdevice* device_;
		/** @brief OpenAL context for the device */
		ALCcontext* context_;
		/** @brief Array of all audio sources */
		ALuint sources_[MaxSources];
		/** @brief Native device sample frequency */
		std::int32_t nativeFreq_;

		/** @brief OpenAL device name */
		const char* deviceName_;

#if defined(OPENAL_FILTERS_SUPPORTED)
		/** @brief Low-pass filter of each source, created on first use, `0` when the source has none */
		ALuint filters_[MaxSources];

		/** @brief Returns a reference to the filter slot of the specified source, or `nullptr` if unknown */
		ALuint* filterForSource(std::uint32_t sourceId);
#endif

		void Init();

#if defined(WITH_LIBRETRO)
		/** @brief `alcRenderSamplesSOFT()` of the `ALC_SOFT_loopback` extension, `nullptr` when unavailable */
		LPALCRENDERSAMPLESSOFT alcRenderSamplesSOFT_;
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		static constexpr std::uint64_t DeviceChangeLimitMs = 250;

		LPALCREOPENDEVICESOFT alcReopenDeviceSOFT_;
		IMMDeviceEnumerator* pEnumerator_;
		std::uint64_t lastDeviceChangeTime_;
		String lastDeviceId_;
		bool shouldRecreate_;

		void recreateAudioDevice();
		void registerAudioEvents();
		void unregisterAudioEvents();

		// IMMNotificationClient implementation
		IFACEMETHODIMP_(ULONG) AddRef() override;
		IFACEMETHODIMP_(ULONG) Release() override;
		IFACEMETHODIMP QueryInterface(REFIID iid, void** object) override;
		IFACEMETHODIMP OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override;
		IFACEMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
		IFACEMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
		IFACEMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
		IFACEMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
#endif
	};

}
