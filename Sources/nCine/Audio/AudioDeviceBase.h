#pragma once

#include "IAudioDevice.h"

#if defined(WITH_THREADS)
#	include "../Threading/Thread.h"
#	include "../Threading/ThreadSync.h"
#endif

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Backend-independent part of an audio device

		Owns the pool of free sources, the list of active players and the stream decoding thread -
		everything an @ref IAudioDevice has to do that does not depend on the sound hardware. A
		backend derives from this, hands over the source ids it created with @ref setSourcePool()
		and only implements the buffer and source operations of @ref IAudioDevice.
	*/
	class AudioDeviceBase : public IAudioDevice
	{
	public:
		~AudioDeviceBase() override;

		AudioDeviceBase(const AudioDeviceBase&) = delete;
		AudioDeviceBase& operator=(const AudioDeviceBase&) = delete;

		inline float gain() const override {
			return _gain;
		}

		inline std::uint32_t maxNumPlayers() const override {
			return std::uint32_t(_sourcePool.size() + _players.size());
		}
		inline std::uint32_t numPlayers() const override {
			return std::uint32_t(_players.size());
		}
		// Spelled with the fixed-width type the interface uses: on targets where `std::uint32_t` is `long
		// unsigned int` (MIPS, among others) `unsigned int` is a different type and would not override it
		const IAudioPlayer* player(std::uint32_t index) const override;
		IAudioPlayer* player(std::uint32_t index) override;

		void stopPlayers() override;
		void pausePlayers() override;
		void stopPlayers(PlayerType playerType) override;
		void pausePlayers(PlayerType playerType) override;

		void freezePlayers() override;
		void unfreezePlayers() override;

		std::uint32_t registerPlayer(IAudioPlayer* player) override;
		void unregisterPlayer(IAudioPlayer* player) override;
		void updatePlayers() override;

		bool submitStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) override;
		void drainStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) override;

		const Vector3f& getListenerPosition() const override;

	protected:
		/** @brief Typical number of sources a backend creates, only the inline capacity of the pools */
		static constexpr std::size_t TypicalNumSources = 16;

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
		void setSourcePool(ArrayView<const std::uint32_t> sourceIds);

		/**
		 * @brief Stops the decoding thread and releases every request still queued
		 *
		 * Has to be called by the backend destructor before it tears down anything the readers
		 * could still touch, the base destructor is too late for that.
		 */
		void shutdownDecodeThread();

	private:
#if defined(WITH_THREADS)
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

		static void decodeThreadFunc(void* arg);
#endif
	};
}
