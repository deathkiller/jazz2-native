#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENAL
#include "../CommonHeaders.h"
#endif

#include "../../Main.h"

/**
 * @brief Logs any pending OpenAL error
 *
 * Expands to an `alGetError()` check that logs a warning when an error is pending.
 * Compiles to a no-op unless both `DEATH_TRACE` and `DEATH_TRACE_VERBOSE_AL` are defined.
 */
#if defined(DEATH_TRACE) && defined(DEATH_TRACE_VERBOSE_AL)
#	define AL_LOG_ERRORS()											\
		do {														\
			ALenum __err = alGetError();							\
			if (__err != AL_NO_ERROR) {								\
				LOGW("OpenAL returned error: 0x{:x}", __err);		\
			}														\
		} while (false)
#else
#	define AL_LOG_ERRORS() do {} while (false)
#endif
