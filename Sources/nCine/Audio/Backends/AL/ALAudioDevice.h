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

		bool IsValid() const override;

		const char* GetName() const override;

		void SetGain(float gain) override;

		void UpdateListener(const Vector3f& position, const Vector3f& velocity) override;

		std::int32_t GetNativeFrequency() override;

		std::uint32_t CreateBuffer(BufferUsage usage) override;
		void DeleteBuffer(std::uint32_t bufferId) override;
		bool UploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency) override;

		void SetSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override;
		void SetSourceGain(std::uint32_t sourceId, float gain) override;
		void SetSourcePitch(std::uint32_t sourceId, float pitch) override;
		void SetSourceLooping(std::uint32_t sourceId, bool looping) override;
		void SetSourceRelative(std::uint32_t sourceId, bool relative) override;
		void SetSourcePosition(std::uint32_t sourceId, const Vector3f& position) override;
		void SetSourceLowPass(std::uint32_t sourceId, float value) override;
		std::int32_t GetSourceSampleOffset(std::uint32_t sourceId) override;
		void SetSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset) override;
		void PlaySource(std::uint32_t sourceId) override;
		void PauseSource(std::uint32_t sourceId) override;
		void StopSource(std::uint32_t sourceId) override;
		bool IsSourcePlaying(std::uint32_t sourceId) override;

		void QueueBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override;
		std::int32_t GetProcessedBufferCount(std::uint32_t sourceId) override;
		void UnqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds) override;

#if defined(WITH_LIBRETRO)
		bool RenderSamples(std::int16_t* buffer, std::int32_t numFrames) override;
#endif

		void SuspendDevice() override;
		void ResumeDevice() override;

		void UpdatePlayers() override;

	private:
		/** @brief Maximum number of OpenAL sources */
#if defined(DEATH_TARGET_PSP)
		// Mixed in software on the main CPU, so this is a frame-time budget; still above what the game plays at once
		static const std::int32_t MaxSources = 16;
#elif defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_IOS) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_VITA)
		static const std::int32_t MaxSources = 48;
#else
		static const std::int32_t MaxSources = 64;
#endif

		/** @brief OpenAL device */
		ALCdevice* _device;
		/** @brief OpenAL context for the device */
		ALCcontext* _context;
		/** @brief Array of all audio sources */
		ALuint _sources[MaxSources];
		/** @brief Native device sample frequency */
		std::int32_t _nativeFreq;

		/** @brief OpenAL device name */
		const char* _deviceName;

#if defined(OPENAL_FILTERS_SUPPORTED)
		/** @brief Low-pass filter of each source, created on first use, `0` when the source has none */
		ALuint _filters[MaxSources];

		/** @brief Returns a reference to the filter slot of the specified source, or `nullptr` if unknown */
		ALuint* GetSourceFilter(std::uint32_t sourceId);
#endif

#if defined(ALC_SOFT_reopen_device)
		/** @brief `alcReopenDeviceSOFT()` of the `ALC_SOFT_reopen_device` extension, `nullptr` when unavailable */
		LPALCREOPENDEVICESOFT _alcReopenDeviceSOFT;
#endif
#if defined(ALC_EXT_disconnect) && !defined(WITH_LIBRETRO)
		/** @brief Number of calls to @ref UpdatePlayers() between two checks of the device connection */
		static constexpr std::int32_t ConnectionCheckInterval = 30;

		/** @brief Frames left until the next check of the device connection */
		std::int32_t _connectionCheckLeft;
		/** @brief Whether the device was disconnected the last time it was checked */
		bool _deviceDisconnected;

		/** @brief Notices a device that went away and tries to bring it back, see @ref UpdatePlayers() */
		void CheckDeviceConnection();
#endif

		void Init();

#if defined(WITH_LIBRETRO)
		/** @brief `alcRenderSamplesSOFT()` of the `ALC_SOFT_loopback` extension, `nullptr` when unavailable */
		LPALCRENDERSAMPLESSOFT _alcRenderSamplesSOFT;
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		static constexpr std::uint64_t DeviceChangeLimitMs = 250;

		IMMDeviceEnumerator* _pEnumerator;
		std::uint64_t _lastDeviceChangeTime;
		String _lastDeviceId;
		bool _shouldRecreate;

		void RecreateAudioDevice();
		void RegisterAudioEvents();
		void UnregisterAudioEvents();

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
