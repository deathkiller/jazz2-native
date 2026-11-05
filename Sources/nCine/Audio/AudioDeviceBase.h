#pragma once

#include "IAudioDevice.h"
#include "../Base/TimeStamp.h"

#if defined(WITH_THREADS) && !defined(NCINE_HAS_NATIVE_AUDIO)
#	include "../Threading/Thread.h"
#	include "../Threading/ThreadSync.h"
#endif

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Backend-independent part of an audio device
		
		Owns the source pool, the active players and the stream decoding thread. A backend hands over its
		source ids with @ref SetSourcePool() and implements only the buffer and source operations.
	*/
	class AudioDeviceBase : public IAudioDevice
	{
	public:
		~AudioDeviceBase() override;

		AudioDeviceBase(const AudioDeviceBase&) = delete;
		AudioDeviceBase& operator=(const AudioDeviceBase&) = delete;

		inline float GetGain() const override {
			return _gain;
		}

		inline std::uint32_t GetMaxPlayerCount() const override {
			return std::uint32_t(_sourcePool.size() + _players.size());
		}
		inline std::uint32_t GetPlayerCount() const override {
			return std::uint32_t(_players.size());
		}
		// Spelled with the fixed-width type the interface uses: on targets where `std::uint32_t` is `long
		// unsigned int` (MIPS, among others) `unsigned int` is a different type and would not override it
		const IAudioPlayer* GetPlayer(std::uint32_t index) const override;
		IAudioPlayer* GetPlayer(std::uint32_t index) override;

		void StopPlayers() override;
		void PausePlayers() override;
		void StopPlayers(PlayerType playerType) override;
		void PausePlayers(PlayerType playerType) override;

		void FreezePlayers() override;
		void UnfreezePlayers() override;

		std::uint32_t RegisterPlayer(IAudioPlayer* player) override;
		void UnregisterPlayer(IAudioPlayer* player) override;
		void UpdatePlayers() override;

#if !defined(NCINE_HAS_NATIVE_AUDIO)
		bool SubmitStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) override;
		void DrainStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) override;
#endif

		const Vector3f& GetListenerPosition() const override;

		void BeginBlockingOperation() override;
		void EndBlockingOperation() override;

	protected:
		/**
			@brief Called when the outermost blocking operation begins
			
			Blocking operations can nest, so backends override this instead of @ref BeginBlockingOperation().
		*/
		virtual void OnBlockingOperationBegan() { }
		/** @brief Called when the last outstanding blocking operation ends (see @ref OnBlockingOperationBegan()) */
		virtual void OnBlockingOperationEnded() { }

		/** @brief Typical number of sources a backend creates, only the inline capacity of the pools */
		static constexpr std::size_t TypicalNumSources = 16;
		/** @brief Number of frames between two looks at the playback positions while no source is free */
		static constexpr std::int32_t StalledCheckInterval = 15;
		/** @brief How long every source may read as playing without advancing before the device is given up on */
		static constexpr float StalledTimeoutSecs = 5.0f;
#if defined(DEATH_TRACE)
		/** @brief Shortest interval in seconds between two reports of an exhausted source pool */
		static constexpr float SourceWarningIntervalSecs = 5.0f;
#endif

		/** @brief Listener gain (master volume) */
		float _gain;
		/** @brief Listener position */
		Vector3f _listenerPos;
		/** @brief Pool of currently inactive source ids */
		SmallVector<std::uint32_t, TypicalNumSources> _sourcePool;
		/** @brief Currently active audio players */
		SmallVector<IAudioPlayer*, TypicalNumSources> _players;

		AudioDeviceBase();

		/** @brief Hands the source ids created by the backend over to the pool */
		void SetSourcePool(ArrayView<const std::uint32_t> sourceIds);

		/**
		 * @brief Reports that @ref RegisterPlayer() had to refuse for lack of a free source
		 *
		 * Rate-limited, because the callers retry every frame.
		 */
		void ReportNoAvailableSources();

		/**
		 * @brief Releases every source when the backend has stopped advancing them
		 *
		 * Recovers from a backend that stopped mixing silently, checked only while no source is free.
		 */
		void CheckForStalledSources();

#if !defined(NCINE_HAS_NATIVE_AUDIO)
		/**
		 * @brief Stops the decoding thread and releases every request still queued
		 *
		 * Must be called from the backend destructor, the base destructor is too late.
		 */
		void ShutdownDecodeThread();
#endif

	private:
		// Since when every source has been in use without any of them advancing, the sum of the playback
		// positions it was compared against, and the frames left until the next comparison
		TimeStamp _stalledSince;
		std::int64_t _lastSourceProgress;
		std::int32_t _stalledCheckLeft;
		// Outstanding BeginBlockingOperation() calls, so that nesting them is safe
		std::int32_t _blockingOperationDepth;
#if defined(DEATH_TRACE)
		// When the exhausted source pool was last reported, and how many reports were left out since
		TimeStamp _lastSourceWarningTime;
		std::uint32_t _suppressedSourceWarnings;
#endif
#if defined(WITH_THREADS) && !defined(NCINE_HAS_NATIVE_AUDIO)
		// Decoding thread that executes stream decode requests ahead of time
		Thread _decodeThread;
		// Protects the request queue, the active request and the quit flag
		Mutex _decodeMutex;
		// Signaled when a request is added to the queue or the thread should quit
		CondVariable _decodeQueueCond;
		// Signaled when the decoding thread finishes executing a request
		CondVariable _decodeDoneCond;
		// Queue of decode requests waiting to be executed
		SmallVector<std::shared_ptr<StreamDecodeRequest>, 4> _decodeQueue;
		// Request currently being executed by the decoding thread, if any
		std::shared_ptr<StreamDecodeRequest> _activeDecodeRequest;
		// Whether the decoding thread has been created
		bool _decodeThreadCreated;
		// Whether the decoding thread should quit
		bool _decodeThreadShouldQuit;

		static void DecodeThreadFunc(void* arg);
#endif
	};
}
