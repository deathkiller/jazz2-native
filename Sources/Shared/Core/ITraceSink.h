#pragma once

#include "../Asserts.h"

#if defined(DEATH_TRACE)

#include "../Containers/StringView.h"

namespace Death
{
	// Forward declaration
	namespace Trace
	{
		class LoggerBackend;
	}

	/**
		@brief Interface for sink to be used by logger writing to it
		
		The sink needs to be registered using @ref Trace::AttachSink() and unregistered using
		@ref Trace::DetachSink(). Then all registered sinks are automatically used by
		`LOGD`/`LOGB`/`LOGI`/`LOGW`/`LOGE` calls and asserts. See also @ref Asserts.h for more details.
		It does *not* have to be thread-safe, because it is only written to from one thread at a time.
	*/
	class ITraceSink
	{
		friend class Trace::LoggerBackend;

	public:
		ITraceSink() {}
		virtual ~ITraceSink() {}

		ITraceSink(ITraceSink const&) = delete;
		ITraceSink& operator=(ITraceSink const&) = delete;

	protected:
		/** @brief Called when a new trace entry is received and should be written to the sink destination */
		virtual void OnTraceReceived(TraceLevel level, std::uint64_t timestamp, Containers::StringView threadId,
			Containers::StringView functionName, Containers::StringView content) = 0;
		/** @brief Called when all buffers of the sink should be flushed immediately */
		virtual void OnTraceFlushed() = 0;
	};

	namespace Trace
	{
		/** @brief Registers the sink and initializes the event logger if no sink was attached before */
		void AttachSink(ITraceSink* sink);

		/** @brief Unregisters the sink and uninitializes the event logger if no sink left */
		void RemoveSink(ITraceSink* sink);

		/**
			@brief Waits until all prior entries are written to all sinks

			All enqueued entries from all threads will be processed, after which all sinks
			will be flushed. Deferred entries in the backtrace storage will *not* be affected,
			see @ref FlushBacktraceAsync().
		*/
		void Flush() noexcept;

		/**
			@brief Initializes the backtrace storage for entries logged with @ref TraceLevel::Deferred

			If the backtrace storage is not initialized, all deferred entries are written
			immediately to all sinks. After initialization, deferred entries are stored
			in the backtrace storage and written to sinks only when the backtrace is flushed.
			Flush can be triggered by calling @ref FlushBacktraceAsync() or automatically
			when an entry with a level equal or higher than the flush level is enqueued.
		*/
		void InitializeBacktrace(std::uint32_t maxCapacity, TraceLevel flushLevel = TraceLevel::Unknown);

		/** @brief Writes any deferred entries stored in the backtrace storage to all sinks asynchronously */
		void FlushBacktraceAsync() noexcept;

#	if defined(DEATH_TRACE_ASYNC) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
			@brief Shrinks the thread-local queue to the specified target capacity

			This function is only available with asynchronous tracing enabled,
			see @cpp DEATH_TRACE_ASYNC @ce in @ref building-config-params-adv.
		*/
		void ShrinkThreadLocalQueue(std::size_t capacity) noexcept;

		/**
			@brief Returns the current capacity of the thread-local queue

			This function is only available with asynchronous tracing enabled,
			see @cpp DEATH_TRACE_ASYNC @ce in @ref building-config-params-adv.
		*/
		std::size_t GetThreadLocalQueueCapacity() noexcept;
#	endif
	}
}

#endif