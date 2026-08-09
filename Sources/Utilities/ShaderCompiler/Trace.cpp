/**
	@file Trace.cpp

	Event-log sink for the offline ShaderCompiler. The engine routes DEATH_TRACE through
	Death::Trace::Logger (Shared/Core/Logger.cpp), which this tool doesn't build, so provide the
	simplest possible synchronous implementation: write every message straight to stderr. This is
	only needed because the shared code the tool compiles uses DEATH_ASSERT, which funnels through
	the trace sink when DEATH_TRACE is enabled.

	Everything below Warning is dropped: Death::IO logs every file open and close, and the tool's
	stderr is a diagnostic surface the build parses, not a place for per-file chatter. The tool's
	own messages never go through DEATH_TRACE, they are written to stdout/stderr directly.
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
		case TraceLevel::Deferred:
		case TraceLevel::Info:		return;
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
