/*
 *  IXLogger.h
 *  Copyright (c) 2026 Dan R.
 */

#pragma once

#include <functional>
#include <string>

// Name of the function a message is reported from. Clang defines __GNUC__ as well and reports
// __PRETTY_FUNCTION__, so it has to be tested first.
#if defined(__clang__) || defined(__GNUC__)
#	define IX_CURRENT_FUNCTION __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
#	define IX_CURRENT_FUNCTION __FUNCTION__ "()"
#else
#	define IX_CURRENT_FUNCTION __func__
#endif

namespace ix
{
	/**
		@brief Severity of a message reported by the library

		See also: @ref LogCallback, @ref setLogCallback()
	*/
	enum class LogLevel
	{
		Error,		/**< A failure the operator of the application should know about */
		Warning,	/**< Something unexpected that the library recovered from on its own */
		Info,		/**< A regular progress message */
		Debug		/**< Details that are only interesting while diagnosing a problem */
	};

	/**
	 * @brief Sink the library hands its messages to
	 * @param level Severity of the message (@ref LogLevel).
	 * @param functionName Function the message was reported from, or `nullptr` if it has none.
	 * @param message The message itself, without a trailing newline.
	 *
	 * Called from any of the library's threads, one message at a time.
	 */
	using LogCallback = std::function<void(LogLevel level, const char* functionName, const std::string& message)>;

	/**
	 * @brief Redirects the library's messages to the given sink
	 * @param callback Sink to use, or an empty callback to go back to standard output.
	 *
	 * Set it before starting anything, so that no message is written anywhere else first. An empty
	 * callback restores the default, which writes errors to `stderr` and everything else to `stdout`.
	 */
	void setLogCallback(const LogCallback& callback);

	/**
	 * @brief Reports a message to the sink installed by @ref setLogCallback()
	 * @param level Severity of the message (@ref LogLevel).
	 * @param functionName Function the message is reported from, usually @cpp IX_CURRENT_FUNCTION @ce.
	 * @param message The message itself, without a trailing newline.
	 */
	void log(LogLevel level, const char* functionName, const std::string& message);
}
