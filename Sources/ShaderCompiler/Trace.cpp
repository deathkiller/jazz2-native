/**
	@file Trace.cpp

	Event-log sink for the offline ShaderCompiler. The engine routes DEATH_TRACE through
	Death::Trace::Logger (Shared/Core/Logger.cpp), which this tool doesn't build, so provide the
	simplest possible synchronous implementation: write every message straight to stderr. This is
	only needed because the shared code the tool compiles uses DEATH_ASSERT, which funnels through
	the trace sink when DEATH_TRACE is enabled.
*/

#include <Asserts.h>

#if defined(DEATH_TRACE)

#include <cstdint>
#include <cstdio>

void DEATH_TRACE(TraceLevel level, const char* functionName, const char* message, std::uint32_t messageLength) noexcept
{
	static_cast<void>(functionName);

	const char* prefix;
	switch (level) {
		case TraceLevel::Debug:
		case TraceLevel::Deferred:	prefix = "[DEBUG] "; break;
		case TraceLevel::Info:		prefix = "[INFO] "; break;
		case TraceLevel::Warning:	prefix = "[WARN] "; break;
		case TraceLevel::Error:		prefix = "[ERROR] "; break;
		case TraceLevel::Assert:	prefix = "[ASSERT] "; break;
		case TraceLevel::Fatal:		prefix = "[FATAL] "; break;
		default:					prefix = ""; break;
	}

	std::fputs(prefix, stderr);
	if (message != nullptr && messageLength > 0) {
		std::fwrite(message, 1, messageLength, stderr);
	}
	std::fputc('\n', stderr);
	std::fflush(stderr);
}

#endif
