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
		
		The sink needs to be registered using @ref Trace::AttachSink() and unregistered using @ref Trace::DetachSink().
		Then all registered sinks are automatically used by `LOGD`/`LOGB`/`LOGI`/`LOGW`/`LOGE` calls and asserts.
		See also @ref Asserts.h for more details.
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
		/** @brief Called when all sink buffers should be flushed immediately */
		virtual void OnTraceFlushed() = 0;
	};

	namespace Trace
	{
		/** @brief Registers the sink and initializes the event logger if no sink was attached before */
		void AttachSink(ITraceSink* sink);

		/** @brief Unregisters the sink and uninitializes the event logger if no sink left */
		void RemoveSink(ITraceSink* sink);

		/** @brief Flushes and waits until all prior entries are written to all sinks */
		void Flush();

		/**
			@brief Initializes backtrace storage for @ref TraceLevel::Deferred

			If the backtrace storage is not initialized, all deferred entries are written immediately to all sinks.
			After initialization, deferred entries are stored in the backtrace storage and written to sinks
			only when the backtrace is flushed. Flush can be triggered by calling @ref FlushBacktraceAsync()
			or automatically when an entry with a level equal or higher than the flush level is enqueued.
		*/
		void InitializeBacktrace(std::uint32_t maxCapacity, TraceLevel flushLevel = TraceLevel::Unknown);

		/** @brief Writes any stored deferred entries to all sinks asynchronously */
		void FlushBacktraceAsync();
	}
}

#endif