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

	/** @brief Interface for sink to be used by logger writing to it */
	class ITraceSink
	{
		friend class Trace::LoggerBackend;

	public:
		ITraceSink() {}
		virtual ~ITraceSink() {}

		ITraceSink(ITraceSink const&) = delete;
		ITraceSink& operator=(ITraceSink const&) = delete;

	protected:
		/** @brief Called when new trace item is received and should be written to the sink destination */
		virtual void OnTraceReceived(TraceLevel level, std::uint64_t timestamp, Containers::StringView threadId, Containers::StringView message) = 0;
		/** @brief Called when all sink buffers should be flushed */
		virtual void OnTraceFlushed() = 0;
	};

	namespace Trace
	{
		/** @brief Registers the sink and initializes logger if no sink was attached before */
		void AttachSink(ITraceSink* sink);

		/** @brief Unregisters the sink and uninitializes logger if no sink left */
		void DetachSink(ITraceSink* sink);
	}
}

#endif