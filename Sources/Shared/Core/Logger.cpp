#include "Logger.h"

#if defined(DEATH_TRACE)

#include "../Containers/GrowableArray.h"

#include <stdarg.h>
#include <cstdio>

#if defined(DEATH_TRACE_ASYNC)
#	include <algorithm>
#endif

using namespace Death::Containers;

namespace Death { namespace Trace {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

#if defined(DEATH_TRACE_ASYNC)
	namespace Implementation
	{
		RdtscClock::RdtscTicks& RdtscClock::RdtscTicks::instance()
		{
			static RdtscTicks inst;
			return inst;
		}

		RdtscClock::RdtscTicks::RdtscTicks()
			: _nanosecondsPerTick(0.0)
		{
			constexpr std::chrono::milliseconds SpinDuration = std::chrono::milliseconds{10};
			constexpr std::int32_t MaxTrials = 15;
			constexpr std::int32_t MinTrials = 3;
			constexpr double ConvergenceThreshold = 0.01; // 1% threshold

			StaticArray<MaxTrials, double> rates(ValueInit);
			std::size_t ratesCount = 0;
			double previousMedian = 0.0;

			for (std::size_t i = 0; i < MaxTrials; i++) {
				auto begTs = std::chrono::nanoseconds{std::chrono::steady_clock::now().time_since_epoch().count()};
				std::uint64_t const begTsc = rdtsc();
				std::uint64_t endTsc;
				std::chrono::nanoseconds elapsedNs;
				do {
					auto endTs = std::chrono::nanoseconds{std::chrono::steady_clock::now().time_since_epoch().count()};
					endTsc = rdtsc();
					elapsedNs = endTs - begTs;
				} while (elapsedNs < SpinDuration);

				rates[ratesCount++] = static_cast<double>(endTsc - begTsc) / static_cast<double>(elapsedNs.count());

				if (((i + 1) >= MinTrials) && (((i + 1) % 2) != 0)) {
					std::nth_element(rates.begin(), rates.begin() + static_cast<std::ptrdiff_t>((i + 1) / 2), rates.begin() + ratesCount);
					double currentMedian = rates[(i + 1) / 2];

					if (std::abs(currentMedian - previousMedian) / currentMedian < ConvergenceThreshold) {
						break;
					}

					previousMedian = currentMedian;
				}
			}

			std::nth_element(rates.begin(), rates.begin() + static_cast<std::ptrdiff_t>(ratesCount / 2), rates.begin() + ratesCount);

			double const ticksPerNanoseconds = rates[ratesCount / 2];
			_nanosecondsPerTick = 1.0 / ticksPerNanoseconds;
		}

		RdtscClock::RdtscClock(std::chrono::nanoseconds resyncInterval)
			: _nanosecondsPerTick(RdtscTicks::instance().nanosecondsPerTick())

		{
			const double calcValue = static_cast<double>(resyncInterval.count()) * _nanosecondsPerTick;

			// Check for overflow and negative values
			if (calcValue >= static_cast<double>(std::numeric_limits<std::int64_t>::max()) || calcValue < 0) {
				_resyncIntervalTicks = std::numeric_limits<std::int64_t>::max();
			} else {
				_resyncIntervalTicks = static_cast<std::int64_t>(calcValue);
			}

			_resyncIntervalOriginal = _resyncIntervalTicks;

			if (!resync(2500)) {
				// Try to resync again with higher lag
				if (!resync(10000)) {
#	if defined(DEATH_DEBUG)
					LOGW("Failed to sync clock, timestamps will be incorrect");
#	endif
				}
			}
		}

		std::uint64_t RdtscClock::timeSinceEpoch(std::uint64_t rdtscValue) const noexcept
		{
			// Should only get called by the backend thread

			// Get the current index, this is only sef called my the thread that is doing the resync
			auto index = _version.load(std::memory_order_relaxed) & (_base.size() - 1);

			// Get rdtsc current value and compare the diff then add it to base wall time
			auto diff = static_cast<std::int64_t>(rdtscValue - _base[index].BaseTsc);

			// We need to sync after we calculated otherwise BaseTsc value will be ahead of passed tsc value
			if (diff > _resyncIntervalTicks) {
				resync(2500);
				diff = static_cast<std::int64_t>(rdtscValue - _base[index].BaseTsc);
			}

			return static_cast<std::uint64_t>(_base[index].BaseTime + static_cast<std::int64_t>(static_cast<double>(diff) * _nanosecondsPerTick));
		}

		std::uint64_t RdtscClock::timeSinceEpochSafe(std::uint64_t rdtscValue) const noexcept
		{
			// Thread-safe, can be called by anyone
			std::uint32_t version;
			std::uint64_t wallTs;

			do {
				version = _version.load(std::memory_order_acquire);
				std::uint64_t index = version & (_base.size() - 1);

				if DEATH_UNLIKELY(_base[index].BaseTsc == 0 && _base[index].BaseTime == 0) {
					return 0;
				}

				// Get rdtsc current value and compare the diff then add it to base wall time
				auto diff = static_cast<std::int64_t>(rdtscValue - _base[index].BaseTsc);
				wallTs = static_cast<std::uint64_t>(_base[index].BaseTime + static_cast<std::int64_t>(static_cast<double>(diff) * _nanosecondsPerTick));
			} while (version != _version.load(std::memory_order_acquire));

			return wallTs;
		}

		bool RdtscClock::resync(std::uint32_t lag) const noexcept
		{
			// Sometimes we might get an interrupt and might never resync, so we will try again up to max_attempts
			constexpr std::uint8_t MaxAttempts = 4;

			for (std::uint8_t attempt = 0; attempt < MaxAttempts; attempt++) {
				std::uint64_t beg = rdtsc();
				// We force convert to nanoseconds because the precision of system_clock::time-point is not portable across platforms.
				std::int64_t wallTime = std::chrono::nanoseconds{std::chrono::system_clock::now().time_since_epoch()}.count();
				std::uint64_t end = rdtsc();

				if DEATH_LIKELY(end - beg <= lag) {
					// Update the next index
					auto index = (_version.load(std::memory_order_relaxed) + 1) & (_base.size() - 1);
					_base[index].BaseTime = wallTime;
					_base[index].BaseTsc = fastAverage(beg, end);
					_version.fetch_add(1, std::memory_order_release);

					_resyncIntervalTicks = _resyncIntervalOriginal;
					return true;
				}
			}

			// We failed to return earlier and we never resynced, but we don't really want to keep retrying on each call
			// to timeSinceEpoch(), so we do non-accurate resync and we will increase the resync duration to resync later
			_resyncIntervalTicks = _resyncIntervalTicks * 2;
			return false;
		}
	}

	ThreadContextManager& ThreadContextManager::Get() noexcept
	{
		static ThreadContextManager instance;
		return instance;
	}

	void ThreadContextManager::RegisterThreadContext(std::shared_ptr<ThreadContext> const& threadContext) noexcept
	{
		_spinlock.lock();
		_threadContexts.push_back(threadContext);
		_spinlock.unlock();
		_newThreadContextFlag.store(true, std::memory_order_release);
	}

	void ThreadContextManager::AddInvalidThreadContext() noexcept
	{
		_invalidThreadContextCount.fetch_add(1, std::memory_order_relaxed);
	}

	bool ThreadContextManager::HasInvalidThreadContext() const noexcept
	{
		// Here we do relaxed, because if the value is not zero, we will look inside ThreadContext invalid flag that is
		// also a relaxed atomic, and then we will look into the SPSC queue size that is also atomic. Even if we don't
		// read everything in order, we will check again in the next circle.
		return _invalidThreadContextCount.load(std::memory_order_relaxed) != 0;
	}

	bool ThreadContextManager::HasNewThreadContext() noexcept
	{
		// Again relaxed memory model as in case it is false, we will acquire the lock
		if (_newThreadContextFlag.load(std::memory_order_relaxed)) {
			// If the variable was updated to true, set it to false. There should not be any race condition here as this
			// is the only place _changed is set to false, and we will return true anyway.
			_newThreadContextFlag.store(false, std::memory_order_relaxed);
			return true;
		}
		return false;
	}

	void ThreadContextManager::RemoveSharedInvalidatedThreadContext(ThreadContext const* threadContext) noexcept
	{
		std::unique_lock lock{_spinlock};

		auto threadContextIt = _threadContexts.end();
		for (auto it = _threadContexts.begin(); it != _threadContexts.end(); ++it) {
			if (it->get() == threadContext) {
				threadContextIt = it;
				break;
			}
		}

		DEATH_DEBUG_ASSERT(threadContextIt != _threadContexts.end(), "Attempting to remove a non existent thread context", );
		DEATH_DEBUG_ASSERT(!threadContextIt->get()->IsValid(), "Attempting to remove a valid thread context", );

#	if defined(DEATH_DEBUG)
		DEATH_DEBUG_ASSERT(threadContext->HasUnboundedQueueType() || threadContext->HasBoundedQueueType());

		if (threadContext->HasUnboundedQueueType()) {
			DEATH_DEBUG_ASSERT(threadContext->GetSpscQueueUnion().UnboundedSpscQueue.empty(),
				   "Attempting to remove a thread context with a non empty queue", );
		} else if (threadContext->HasBoundedQueueType()) {
			DEATH_DEBUG_ASSERT(threadContext->GetSpscQueueUnion().BoundedSpscQueue.empty(),
				   "Attempting to remove a thread context with a non empty queue", );
		}
#	endif

		_threadContexts.erase(threadContextIt);

		_invalidThreadContextCount.fetch_sub(1, std::memory_order_relaxed);
	}
#endif

	BacktraceStorage::BacktraceStorage()
		: _capacity(0), _index(0)
	{
	}

	void BacktraceStorage::Store(TransitEvent transitEvent, StringView threadId) noexcept
	{
		if (_storedEvents.size() < _capacity) {
			arrayAppend(_storedEvents, InPlaceInit, threadId, Death::move(transitEvent));
		} else {
			StoredTransitEvent& ste = _storedEvents[_index];

			ste = StoredTransitEvent{threadId, Death::move(transitEvent)};

			if (_index < _capacity - 1) {
				_index++;
			} else {
				_index = 0;
			}
		}
	}

	void BacktraceStorage::Process(Function<void(TransitEvent const& event, StringView threadId)>&& callback) noexcept
	{
		std::uint32_t index = _index;

		for (std::uint32_t i = 0; i < _storedEvents.size(); i++) {
			auto& e = _storedEvents[index];
			callback(e.Event, e.ThreadId);

			if (index < _storedEvents.size() - 1) {
				index++;
			} else {
				index = 0;
			}
		}

		arrayClear(_storedEvents);
		_index = 0;
	}

	void BacktraceStorage::SetCapacity(std::uint32_t capacity) noexcept
	{
		if (_capacity != capacity) {
			_capacity = capacity;
			_index = 0;
			arrayClear(_storedEvents);
			arrayReserve(_storedEvents, _capacity);
		}
	}

	BacktraceStorage::StoredTransitEvent::StoredTransitEvent(String threadId, TransitEvent transitEvent)
		: ThreadId(Death::move(threadId)), Event(Death::move(transitEvent))
	{
	}

	LoggerBackend::LoggerBackend()
		: _backtraceFlushLevel(TraceLevel::Unknown)
#if defined(DEATH_TRACE_ASYNC)
			, _rdtscClock(Implementation::RdtscResyncInterval), _lastRdtscResyncTime(std::chrono::system_clock::now()), _workerThreadAlive(false)
#endif
	{
	}

	LoggerBackend::~LoggerBackend()
	{
		Dispose();
	}

	void LoggerBackend::AttachSink(ITraceSink* sink)
	{
		_sinks.push_back(sink);

		if (_sinks.size() == 1) {
			Initialize();
		}
	}

	void LoggerBackend::RemoveSink(ITraceSink* sink)
	{
		for (std::size_t i = 0; i < _sinks.size(); i++) {
			if (_sinks[i] == sink) {
				_sinks.eraseUnordered(&_sinks[i]);
				if (_sinks.empty()) {
					Dispose();
				}
				break;
			}
		}
	}

	void LoggerBackend::Notify() noexcept
	{
#if defined(DEATH_TRACE_ASYNC)
		_wakeUpEvent.SetEvent();
#endif
	}

	TraceLevel LoggerBackend::GetBacktraceFlushLevel() const noexcept
	{
#if defined(DEATH_TRACE_ASYNC)
		return _backtraceFlushLevel.load(std::memory_order_relaxed);
#else
		return _backtraceFlushLevel;
#endif
	}

	void LoggerBackend::SetBacktraceFlushLevel(TraceLevel flushLevel) noexcept
	{
#if defined(DEATH_TRACE_ASYNC)
		_backtraceFlushLevel.store(flushLevel, std::memory_order_relaxed);
#else
		_backtraceFlushLevel = flushLevel;
#endif
	}

	void LoggerBackend::Initialize()
	{
#if defined(DEATH_TRACE_ASYNC)
		if (_workerThreadAlive.load(std::memory_order_relaxed)) {
			return;
		}

		std::thread workerThread([this]() {
			_workerThreadAlive.store(true);

			while DEATH_LIKELY(_workerThreadAlive.load(std::memory_order_relaxed)) {
				ProcessEvents();
			}

			CleanUpBeforeExit();
		});

		_workerThread.swap(workerThread);

		while (!_workerThreadAlive.load(std::memory_order_seq_cst)) {
			// Wait for the thread to start
			std::this_thread::sleep_for(std::chrono::microseconds{100});
		}
#endif
	}

	void LoggerBackend::Dispose()
	{
#if defined(DEATH_TRACE_ASYNC)
		if (!_workerThreadAlive.exchange(false)) {
			return;
		}

		_wakeUpEvent.SetEvent();

		// Wait the backend thread to join, if backend thread was never started it won't be joinable
		if (_workerThread.joinable()) {
			_workerThread.join();
		}
#endif
	}

#if defined(DEATH_TRACE_ASYNC)
	bool LoggerBackend::IsAlive() const noexcept
	{
		return _workerThreadAlive.load(std::memory_order_relaxed);
	}

	bool LoggerBackend::IsWorkerThread() const noexcept
	{
		return (std::this_thread::get_id() == _workerThread.get_id());
	}

	void LoggerBackend::CleanUpBeforeExit() noexcept
	{
		using namespace Implementation;

		while (true) {
			bool queuesAndEventsEmpty = (!WaitForQueuesToEmptyBeforeExit || CheckThreadQueuesAndCachedTransitEventsEmpty());
			if (queuesAndEventsEmpty) {
				// We are done, all queues are now empty
				//_check_failure_counter(_options.error_notifier);
				FlushActiveSinks();
				break;
			}

			std::uint64_t cachedTransitEventsCount = PopulateTransitEventsFromFrontendQueues();
			if (cachedTransitEventsCount > 0) {
				while (!HasPendingEventsForCachingWhenTransitEventBufferEmpty() && ProcessLowestTimestampTransitEvent()) {
					// We need to be cautious because there are log messages in the lock-free queues that have not yet
					// been cached in the transit event buffer. Logging only the cached messages can result in out-of-order
					// log entries, as messages with larger timestamps in the queue might be missed.
				}
			}
		}

		CleanUpInvalidatedThreadContexts();
	}

	void LoggerBackend::UpdateActiveThreadContextsCache() noexcept
	{
		ThreadContextManager& threadManager = ThreadContextManager::Get();

		// Check if _threadContexts has changed, this can happen only when a new thread context is added by any Logger
		if DEATH_UNLIKELY(threadManager.HasNewThreadContext()) {
			_activeThreadContextsCache.clear();
			threadManager.ForEachThreadContext([this](ThreadContext* threadContext) {
				_activeThreadContextsCache.push_back(threadContext);
			});
		}
	}

	void LoggerBackend::CleanUpInvalidatedThreadContexts() noexcept
	{
		ThreadContextManager& threadManager = ThreadContextManager::Get();

		if (!threadManager.HasInvalidThreadContext()) {
			return;
		}

		auto findInvalidAndEmptyThreadContextCallback = [](ThreadContext* threadContext) {
			// If the thread context is invalid, it means the thread that created it has now died.
			// We also want to empty the queue from all LogRecords before removing the thread context
			if (!threadContext->IsValid()) {
				DEATH_DEBUG_ASSERT(threadContext->HasUnboundedQueueType() || threadContext->HasBoundedQueueType());

				if (threadContext->HasUnboundedQueueType()) {
					return threadContext->GetSpscQueueUnion().UnboundedSpscQueue.empty() &&
						threadContext->_transitEventBuffer.empty();
				}

				if (threadContext->HasBoundedQueueType()) {
					return threadContext->GetSpscQueueUnion().BoundedSpscQueue.empty() &&
						threadContext->_transitEventBuffer.empty();
				}
			}

			return false;
		};

		// First we iterate our existing cache and we look for any invalidated contexts
		auto foundInvalidAndEmptyThreadContext =
			std::find_if(_activeThreadContextsCache.begin(), _activeThreadContextsCache.end(),
						 findInvalidAndEmptyThreadContextCallback);

		while DEATH_UNLIKELY(foundInvalidAndEmptyThreadContext != std::end(_activeThreadContextsCache)) {
			// If we found anything then remove it - Here if we have more than one to remove, we will try to acquire
			// the lock multiple times, but it should be fine as it is unlikely to have that many to remove
			threadManager.RemoveSharedInvalidatedThreadContext(*foundInvalidAndEmptyThreadContext);

			// We also need to remove it from _thread_context_cache, that is used only by the backend
			_activeThreadContextsCache.erase(foundInvalidAndEmptyThreadContext);

			// And then look again
			foundInvalidAndEmptyThreadContext = std::find_if(_activeThreadContextsCache.begin(),
				_activeThreadContextsCache.end(), findInvalidAndEmptyThreadContextCallback);
		}
	}

	bool LoggerBackend::PopulateTransitEventFromThreadQueue(const std::byte*& readPos, ThreadContext* threadContext, std::uint64_t tsNow) noexcept
	{
		using namespace Implementation;

		// Allocate a new TransitEvent or use an existing one to store the message from the queue
		TransitEvent* transitEvent = threadContext->_transitEventBuffer.back();

		transitEvent->Level = (TraceLevel)readPos[0];
		readPos += 1;

		std::memcpy(&transitEvent->Timestamp, readPos, sizeof(transitEvent->Timestamp));
		readPos += sizeof(transitEvent->Timestamp);

		// Convert the rdtsc value to nanoseconds since epoch
		transitEvent->Timestamp = _rdtscClock.timeSinceEpoch(transitEvent->Timestamp);

		// Ensure the message timestamp is not greater than ts_now
		if DEATH_UNLIKELY(transitEvent->Timestamp > tsNow) {
			// If the message timestamp is ahead of the current time, temporarily halt processing. This guarantees
			// the integrity of message order and avoids missed messages. We halt processing here to avoid introducing
			// out-of-sequence messages. This scenario prevents potential race conditions where timestamps from the last
			// queue could overwrite those from the first queue before they are included. We return at this point
			// without adding the current event to the buffer.
			return false;
		}

		std::uintptr_t functionName;
		std::memcpy(&functionName, readPos, sizeof(std::uintptr_t));
		readPos += sizeof(std::uintptr_t);

		std::uint32_t length;
		std::memcpy(&length, readPos, sizeof(std::uint32_t));
		readPos += sizeof(std::uint32_t);

		// We need to check and do not try to format the flush events as that wouldn't be valid
		if DEATH_UNLIKELY(transitEvent->Level == FlushRequested) {
			// If this is a flush event then we do not need to format anything for the TransitEvent, but we need
			// to set the transit event's FlushFlag pointer instead
			transitEvent->FlushFlag = reinterpret_cast<std::atomic<bool>*>(functionName);
		} else if DEATH_UNLIKELY(transitEvent->Level == InitializeBacktraceRequested) {
			transitEvent->Capacity = static_cast<std::uint32_t>(functionName);
		} else if DEATH_LIKELY(transitEvent->Level != FlushBacktraceRequested) {
			transitEvent->FunctionName = reinterpret_cast<const char*>(functionName);
			transitEvent->Message.resize(length);
			std::memcpy(&transitEvent->Message[0], readPos, length);
		}

		readPos += length;

		// Commit this transit event
		threadContext->_transitEventBuffer.push_back();

		return true;
	}

	std::size_t LoggerBackend::PopulateTransitEventsFromFrontendQueues() noexcept
	{
		using namespace Implementation;

		std::uint64_t const tsNow = LogTimestampOrderingGracePeriod.count()
			? static_cast<std::uint64_t>((std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::system_clock::now().time_since_epoch()) -
				LogTimestampOrderingGracePeriod).count())
			: std::numeric_limits<std::uint64_t>::max();

		std::size_t cachedTransitEventsCount = 0;

		for (ThreadContext* threadContext : _activeThreadContextsCache) {
			DEATH_DEBUG_ASSERT(threadContext->HasUnboundedQueueType() || threadContext->HasBoundedQueueType());

			if (threadContext->HasUnboundedQueueType()) {
				cachedTransitEventsCount += ReadAndDecodeThreadQueue(
				  threadContext->GetSpscQueueUnion().UnboundedSpscQueue, threadContext, tsNow);
			} else if (threadContext->HasBoundedQueueType()) {
				cachedTransitEventsCount += ReadAndDecodeThreadQueue(
				  threadContext->GetSpscQueueUnion().BoundedSpscQueue, threadContext, tsNow);
			}
		}

		return cachedTransitEventsCount;
	}

	bool LoggerBackend::HasPendingEventsForCachingWhenTransitEventBufferEmpty() noexcept
	{
		UpdateActiveThreadContextsCache();

		for (ThreadContext* threadContext : _activeThreadContextsCache) {
			if (threadContext->_transitEventBuffer.empty()) {
				// If there is no _transitEventBuffer yet, check only the queue
				if (threadContext->HasUnboundedQueueType() &&
					!threadContext->GetSpscQueueUnion().UnboundedSpscQueue.empty()) {
					return true;
				}

				if (threadContext->HasBoundedQueueType() &&
					!threadContext->GetSpscQueueUnion().BoundedSpscQueue.empty()) {
					return true;
				}
			}
		}

		return false;
	}

	bool LoggerBackend::CheckThreadQueuesAndCachedTransitEventsEmpty() noexcept
	{
		UpdateActiveThreadContextsCache();

		bool allEmpty = true;

		for (ThreadContext* threadContext : _activeThreadContextsCache) {
			DEATH_DEBUG_ASSERT(threadContext->HasUnboundedQueueType() || threadContext->HasBoundedQueueType());

			if (threadContext->HasUnboundedQueueType()) {
				allEmpty &= threadContext->GetSpscQueueUnion().UnboundedSpscQueue.empty();
			} else if (threadContext->HasBoundedQueueType()) {
				allEmpty &= threadContext->GetSpscQueueUnion().BoundedSpscQueue.empty();
			}

			allEmpty &= threadContext->_transitEventBuffer.empty();
		}

		return allEmpty;
	}

	void LoggerBackend::ResyncRdtscClock() noexcept
	{
		using namespace Implementation;

		if (auto now = std::chrono::system_clock::now();
			(now - _lastRdtscResyncTime) > RdtscResyncInterval) {
			if (_rdtscClock.resync(2500)) {
				_lastRdtscResyncTime = now;
			}
		}
	}

	void LoggerBackend::DispatchTransitEventToSinks(TransitEvent const& transitEvent, StringView threadId) noexcept
	{
		StringView functionNameView{transitEvent.FunctionName};
		StringView contentView{transitEvent.Message};

		for (std::size_t i = 0; i < _sinks.size(); i++) {
			_sinks[i]->OnTraceReceived(transitEvent.Level, transitEvent.Timestamp, threadId, functionNameView, contentView);
		}
	}

	void LoggerBackend::FlushActiveSinks() noexcept
	{
		for (std::size_t i = 0; i < _sinks.size(); i++) {
			_sinks[i]->OnTraceFlushed();
		}
	}

	void LoggerBackend::ProcessTransitEvent(ThreadContext const& threadContext, TransitEvent& transitEvent, std::atomic<bool>*& flushFlag) noexcept
	{
		using namespace Implementation;

		if DEATH_UNLIKELY(transitEvent.Level == FlushRequested) {
			FlushActiveSinks();

			// This is a flush event, so we capture the flush flag to notify the caller after processing
			flushFlag = transitEvent.FlushFlag;

			// Reset the flush flag as TransitEvents are re-used, preventing incorrect flag reuse
			transitEvent.FlushFlag = nullptr;

			// We defer notifying the caller until after this function completes
		} else if DEATH_UNLIKELY(transitEvent.Level == InitializeBacktraceRequested) {
			if (!_backtraceStorage) {
				_backtraceStorage = std::make_shared<BacktraceStorage>();
			}

			_backtraceStorage->SetCapacity(transitEvent.Capacity);
		} else if DEATH_UNLIKELY(transitEvent.Level == FlushBacktraceRequested) {
			if (_backtraceStorage) {
				_backtraceStorage->Process(
					[this](TransitEvent const& te, StringView threadId) { DispatchTransitEventToSinks(te, threadId); });
			}
		} else if DEATH_UNLIKELY(transitEvent.Level == TraceLevel::Deferred) {
			if (_backtraceStorage) {
				_backtraceStorage->Store(Death::move(transitEvent), threadContext.GetThreadId());
			} else {
				// If the backtrace storage is not initialized, we dispatch the event directly to the sinks
				DispatchTransitEventToSinks(transitEvent, threadContext.GetThreadId());
			}
		} else {
			// First, dispatch any deferred entries if the trace level is high enough
			TraceLevel backtraceFlushLevel = _backtraceFlushLevel.load(std::memory_order_relaxed);
			if DEATH_UNLIKELY(backtraceFlushLevel != TraceLevel::Unknown && transitEvent.Level >= backtraceFlushLevel) {
				if (_backtraceStorage) {
					_backtraceStorage->Process(
						[this](TransitEvent const& te, StringView threadId) { DispatchTransitEventToSinks(te, threadId); });
				}
			}

			DispatchTransitEventToSinks(transitEvent, threadContext.GetThreadId());
		}
	}

	bool LoggerBackend::ProcessLowestTimestampTransitEvent() noexcept
	{
		// Get the lowest timestamp
		std::uint64_t minTs = std::numeric_limits<std::uint64_t>::max();
		ThreadContext* threadContext = nullptr;

		for (ThreadContext* tc : _activeThreadContextsCache) {
			TransitEvent const* te = tc->_transitEventBuffer.front();
			if (te && (minTs > te->Timestamp)) {
				minTs = te->Timestamp;
				threadContext = tc;
			}
		}

		if (threadContext == nullptr) {
			// All transit event buffers are empty
			return false;
		}

		TransitEvent* transitEvent = threadContext->_transitEventBuffer.front();

		std::atomic<bool>* flushFlag = nullptr;
		ProcessTransitEvent(*threadContext, *transitEvent, flushFlag);

		threadContext->_transitEventBuffer.pop_front();

		if (flushFlag != nullptr) {
			// Process the second part of the flush event after it's been removed from the buffer,
			// ensuring that we are no longer interacting with the threadContext or transitEvent.

			// This is particularly important for handling cases when Quill is used as a DLL on Windows.
			// If `FreeLibrary` is called, the backend thread may attempt to access an invalidated
			// `ThreadContext`, which can lead to a crash due to invalid memory access.
			//
			// To prevent this, whenever we receive a Flush event, we clean up any invalidated thread contexts
			// before notifying the caller. This ensures that when flush is invoked in `DllMain` during
			// `DLL_PROCESS_DETACH`, the `ThreadContext` is properly cleaned up before the DLL exits.
			CleanUpInvalidatedThreadContexts();

			// Now it’s safe to notify the caller to continue execution
			flushFlag->store(true);
		}

		return true;
	}

	void LoggerBackend::ProcessEvents() noexcept
	{
		using namespace Implementation;

		UpdateActiveThreadContextsCache();

		// Read all frontend queues and cache the log statements and the metadata as TransitEvents
		std::size_t cachedTransitEventsCount = PopulateTransitEventsFromFrontendQueues();

		if (cachedTransitEventsCount != 0) {
			// There are cached events to process
			if (cachedTransitEventsCount < TransitEventsSoftLimit) {
				// Process a single transit event, then give priority to reading the thread queues again
				ProcessLowestTimestampTransitEvent();
			} else {
				// We want to process a batch of events
				while (!HasPendingEventsForCachingWhenTransitEventBufferEmpty() && ProcessLowestTimestampTransitEvent()) {
					// We need to be cautious because there are log messages in the lock-free queues that have not
					// yet been cached in the transit event buffer. Logging only the cached messages can result
					// in out-of-order log entries, as messages with larger timestamps in the queue might be missed.
				}
			}
		} else {
			// No cached transit events to process, minimal thread workload

			// Force flush all remaining messages
			FlushActiveSinks();

			// Check for any dropped messages / blocked threads
			//_check_failure_counter(_options.error_notifier);

			ResyncRdtscClock();

			// Also check if all queues are empty
			bool queuesAndEventsEmpty = CheckThreadQueuesAndCachedTransitEventsEmpty();
			if (queuesAndEventsEmpty) {
				CleanUpInvalidatedThreadContexts();

				// There is nothing left to do, and we can let this thread sleep for a while
				_wakeUpEvent.Wait();

				ResyncRdtscClock();
			}
		}
	}
#else
	void LoggerBackend::DispatchEntryToSinks(TraceLevel level, std::uint64_t timestamp, const void* functionName, const void* content, std::uint32_t contentLength, StringView threadId) noexcept
	{
		using namespace Implementation;

		char buffer[16];
		if (threadId.empty()) {
			if (std::uint32_t tid = GetNativeThreadId()) {
				std::int32_t length = snprintf(buffer, sizeof(buffer), "%u", tid);
				threadId = StringView{buffer, static_cast<std::size_t>(length)};
			}
		}

		StringView functionNameView{static_cast<const char*>(functionName)};
		StringView contentView{static_cast<const char*>(content), std::size_t(contentLength)};

		for (std::size_t i = 0; i < _sinks.size(); i++) {
			_sinks[i]->OnTraceReceived(level, timestamp, threadId, functionNameView, contentView);
		}
	}

	void LoggerBackend::FlushActiveSinks() noexcept
	{
		for (std::size_t i = 0; i < _sinks.size(); i++) {
			_sinks[i]->OnTraceFlushed();
		}
	}

	void LoggerBackend::InitializeBacktrace(std::uint32_t maxCapacity)
	{
		using namespace Implementation;

		if (!_backtraceStorage) {
			_backtraceStorage = std::make_shared<BacktraceStorage>();
		}

		_backtraceStorage->SetCapacity(maxCapacity);
	}

	void LoggerBackend::FlushBacktraceAsync() noexcept
	{
		if (_backtraceStorage) {
			_backtraceStorage->Process(
				[this](TransitEvent const& te, StringView threadId) { DispatchEntryToSinks(te.Level,
					te.Timestamp, te.FunctionName, te.Message.data(), static_cast<std::int32_t>(te.Message.size()), threadId); });
		}
	}

	void LoggerBackend::EnqueueEntryToBacktrace(std::uint64_t timestamp, const void* functionName, const void* content, std::uint32_t contentLength) noexcept
	{
		using namespace Implementation;

		if (_backtraceStorage) {
			StringView threadId;
			char buffer[16];
			if (std::uint32_t tid = GetNativeThreadId()) {
				std::int32_t length = snprintf(buffer, sizeof(buffer), "%u", tid);
				threadId = StringView{buffer, static_cast<std::size_t>(length)};
			}

			TransitEvent transitEvent;
			transitEvent.Timestamp = timestamp;
			transitEvent.FunctionName = static_cast<const char*>(functionName);
			transitEvent.Message.resize(contentLength);
			transitEvent.Level = TraceLevel::Deferred;
			std::memcpy(&transitEvent.Message[0], content, contentLength);

			_backtraceStorage->Store(Death::move(transitEvent), threadId);
		} else {
			DispatchEntryToSinks(TraceLevel::Deferred, timestamp, functionName, content, contentLength, {});
		}
	}
#endif

	void Logger::AttachSink(ITraceSink* sink)
	{
		_backend.AttachSink(sink);
	}

	void Logger::RemoveSink(ITraceSink* sink)
	{
		_backend.RemoveSink(sink);
	}

	bool Logger::Write(TraceLevel level, const char* functionName, const char* message, std::uint32_t messageLength)
	{
#if defined(DEATH_TRACE_ASYNC)
		std::uint64_t timestamp = Implementation::rdtsc();
#else
		std::uint64_t timestamp = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
#endif

		bool result = EnqueueEntry(level, timestamp, functionName, message, messageLength);

		if DEATH_UNLIKELY(level >= TraceLevel::Error) {
			// Flush all messages with level Error or higher because of potential immediate crash/termination
			Flush();
		} else {
			_backend.Notify();
		}

		return result;
	}

	void Logger::Flush(std::uint32_t sleepDurationNs) noexcept
	{
		using namespace Implementation;

#if defined(DEATH_TRACE_ASYNC)
		std::uint64_t timestamp = rdtsc();

		if (!_backend.IsAlive() || _backend.IsWorkerThread()) {
			// If the backend is not alive (yet) or it's called from worker thread itself (in case of error), don't try to wait for the flushing
			return;
		}

		std::atomic<bool> threadFlushed{false};
		std::atomic<bool>* threadFlushedPtr = &threadFlushed;

		// We do not want to drop the message if a dropping queue is used
		while (!EnqueueEntry(FlushRequested, timestamp, threadFlushedPtr, {}, 0)) {
			if (sleepDurationNs > 0) {
				std::this_thread::sleep_for(std::chrono::nanoseconds{sleepDurationNs});
			} else {
				std::this_thread::yield();
			}
		}

		_backend.Notify();

		// The caller thread keeps checking the flag until the backend thread flushes
		while (!threadFlushed.load()) {
			if (sleepDurationNs > 0) {
				std::this_thread::sleep_for(std::chrono::nanoseconds{sleepDurationNs});
			} else {
				std::this_thread::yield();
			}
		}
#endif
	}

	void Logger::InitializeBacktrace(std::uint32_t maxCapacity, TraceLevel flushLevel)
	{
		// All deferred entries are logged immediately if the backtrace storage is not initialized
#if defined(DEATH_TRACE_ASYNC)
		using namespace Implementation;

		while (!EnqueueEntry(InitializeBacktraceRequested, 0,
					reinterpret_cast<const void*>(static_cast<std::uintptr_t>(maxCapacity)), {}, 0)) {
			std::this_thread::sleep_for(std::chrono::nanoseconds{100});
		}

		_backend.SetBacktraceFlushLevel(flushLevel);
		_backend.Notify();
#else
		_backend.InitializeBacktrace(maxCapacity);
		_backend.SetBacktraceFlushLevel(flushLevel);
#endif
	}

	void Logger::FlushBacktraceAsync() noexcept
	{
#if defined(DEATH_TRACE_ASYNC)
		using namespace Implementation;

		while (!EnqueueEntry(FlushBacktraceRequested, 0, nullptr, {}, 0)) {
			std::this_thread::sleep_for(std::chrono::nanoseconds{100});
		}

		_backend.Notify();
#else
		_backend.FlushBacktraceAsync();
#endif
	}

#if defined(DEATH_TRACE_ASYNC)
	void Logger::ShrinkThreadLocalQueue(std::size_t capacity) noexcept
	{
		using namespace Implementation;

		if constexpr (DefaultQueueType == QueueType::UnboundedDropping || DefaultQueueType == QueueType::UnboundedBlocking) {
			if (_threadContext != nullptr) {
				_threadContext->GetSpscQueue<DefaultQueueType>().shrink(capacity);
			}
		}
	}

	std::size_t Logger::GetThreadLocalQueueCapacity() noexcept
	{
		using namespace Implementation;

		if constexpr (DefaultQueueType == QueueType::UnboundedDropping || DefaultQueueType == QueueType::UnboundedBlocking) {
			if (_threadContext != nullptr) {
				return _threadContext->GetSpscQueue<DefaultQueueType>().producerCapacity();
			}
		} else {
			if (_threadContext != nullptr) {
				return _threadContext->GetSpscQueue<DefaultQueueType>().capacity();
			}
		}

		return 0;
	}

	ThreadContext* Logger::GetLocalThreadContext() noexcept
	{
		using namespace Implementation;

		DEATH_THREAD_LOCAL ScopedThreadContext scopedThreadContext
			{DefaultQueueType, InitialQueueCapacity, HugePagesEnabled};

		return scopedThreadContext.GetThreadContext();
	}

	std::byte* Logger::PrepareWriteBuffer(std::size_t totalSize) noexcept
	{
		using namespace Implementation;

		return _threadContext->GetSpscQueue<DefaultQueueType>().prepareWrite(totalSize);
	}

	bool Logger::EnqueueEntry(TraceLevel level, std::uint64_t timestamp, const void* functionName, const void* content, std::uint32_t contentLength) noexcept
	{
		using namespace Implementation;

		if DEATH_UNLIKELY(_threadContext == nullptr) {
			_threadContext = GetLocalThreadContext();
		}

		std::size_t totalSize = /*Level*/ sizeof(std::byte) + /*Timestamp*/ sizeof(std::uint64_t) +
			/*FunctionName*/ sizeof(std::uintptr_t) + /*Length*/ sizeof(std::uint32_t) + /*Content*/ contentLength;
		std::byte* writeBuffer = PrepareWriteBuffer(totalSize);

		if constexpr (DefaultQueueType == QueueType::BoundedDropping ||
					  DefaultQueueType == QueueType::UnboundedDropping) {
			if DEATH_UNLIKELY(writeBuffer == nullptr) {
				// Not enough space to push to queue, message is dropped
				if (level != FlushRequested) {
					_threadContext->IncrementFailureCounter();
				}
				return false;
			}
		} else if constexpr (DefaultQueueType == QueueType::BoundedBlocking ||
							 DefaultQueueType == QueueType::UnboundedBlocking) {
			if DEATH_UNLIKELY(writeBuffer == nullptr) {
				if (level != FlushRequested) {
					_threadContext->IncrementFailureCounter();
				}

				do {
					if constexpr (BlockingQueueRetryIntervalNanoseconds > 0) {
						std::this_thread::sleep_for(std::chrono::nanoseconds{BlockingQueueRetryIntervalNanoseconds});
					}

					// Not enough space to push to queue, keep trying
					writeBuffer = PrepareWriteBuffer(totalSize);
				} while (writeBuffer == nullptr);
			}
		}

#	if defined(DEATH_DEBUG)
		std::byte const* const writeBegin = writeBuffer;
		DEATH_DEBUG_ASSERT(writeBegin != nullptr);
#	endif

		writeBuffer[0] = (std::byte)level;
		writeBuffer += 1;

		std::memcpy(writeBuffer, &timestamp, sizeof(std::uint64_t));
		writeBuffer += sizeof(std::uint64_t);

		std::memcpy(writeBuffer, &functionName, sizeof(std::uintptr_t));
		writeBuffer += sizeof(std::uintptr_t);

		std::memcpy(writeBuffer, &contentLength, sizeof(std::uint32_t));
		writeBuffer += sizeof(std::uint32_t);

		std::memcpy(writeBuffer, content, contentLength);
		writeBuffer += contentLength;

#	if defined(DEATH_DEBUG)
		DEATH_DEBUG_ASSERT(writeBuffer > writeBegin);
		DEATH_DEBUG_ASSERT(totalSize == (static_cast<std::size_t>(writeBuffer - writeBegin)));
#	endif

		_threadContext->GetSpscQueue<DefaultQueueType>().finishAndCommitWrite(totalSize);

		return true;
	}
#else
	bool Logger::EnqueueEntry(TraceLevel level, std::uint64_t timestamp, const void* functionName, const void* content, std::uint32_t contentLength) noexcept
	{
		if DEATH_UNLIKELY(level == TraceLevel::Deferred) {
			_backend.EnqueueEntryToBacktrace(timestamp, functionName, content, contentLength);
			return true;
		}

		TraceLevel backtraceFlushLevel = _backend.GetBacktraceFlushLevel();
		if DEATH_UNLIKELY(backtraceFlushLevel != TraceLevel::Unknown && level >= backtraceFlushLevel) {
			_backend.FlushBacktraceAsync();
		}

		_backend.DispatchEntryToSinks(level, timestamp, functionName, content, contentLength, {});
		return true;
	}
#endif

	static Trace::Logger& GetMainLogger()
	{
		static Trace::Logger logger;
		return logger;
	}

	void AttachSink(ITraceSink* sink)
	{
		GetMainLogger().AttachSink(sink);
	}

	void RemoveSink(ITraceSink* sink)
	{
		GetMainLogger().RemoveSink(sink);
	}

	void Flush() noexcept
	{
		GetMainLogger().Flush();
	}

	void InitializeBacktrace(std::uint32_t maxCapacity, TraceLevel flushLevel)
	{
		GetMainLogger().InitializeBacktrace(maxCapacity, flushLevel);
	}

	void FlushBacktraceAsync() noexcept
	{
		GetMainLogger().FlushBacktraceAsync();
	}

#if defined(DEATH_TRACE_ASYNC) || defined(DOXYGEN_GENERATING_OUTPUT)
	void ShrinkThreadLocalQueue(std::size_t capacity) noexcept
	{
		GetMainLogger().ShrinkThreadLocalQueue(capacity);
	}

	std::size_t GetThreadLocalQueueCapacity() noexcept
	{
		return GetMainLogger().GetThreadLocalQueueCapacity();
	}
#endif

}}

void DEATH_TRACE(TraceLevel level, const char* functionName, const char* message, std::uint32_t messageLength)
{
	using namespace Death::Trace;

	GetMainLogger().Write(level, functionName, message, messageLength);
}

#endif