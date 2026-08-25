/*
 *  IXLogger.cpp
 *  Copyright (c) 2026 Dan R.
 */

#include "IXLogger.h"

#include <mutex>
#include <stdio.h>

namespace ix
{
	namespace
	{
		struct LogSink
		{
			std::mutex mutex;
			LogCallback callback;
		};

		// Deliberately never destroyed - a connection thread that reports something while the process
		// is already tearing itself down would otherwise touch it after static destruction ran
		LogSink& logSink()
		{
			static LogSink* sink = new LogSink();
			return *sink;
		}
	}

	void setLogCallback(const LogCallback& callback)
	{
		LogSink& sink = logSink();

		std::lock_guard<std::mutex> lock(sink.mutex);
		sink.callback = callback;
	}

	void log(LogLevel level, const char* functionName, const std::string& message)
	{
		LogSink& sink = logSink();

		// The lock is held across the call, so the sink sees one message at a time and doesn't have to
		// be thread-safe itself. It also keeps a sink from being replaced while it's being called.
		std::lock_guard<std::mutex> lock(sink.mutex);

		if (sink.callback) {
			sink.callback(level, functionName, message);
			return;
		}

		FILE* stream = (level == LogLevel::Error ? stderr : stdout);
		if (functionName != nullptr) {
			fprintf(stream, "%s %s\n", functionName, message.c_str());
		} else {
			fprintf(stream, "%s\n", message.c_str());
		}
	}
}
