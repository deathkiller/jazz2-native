#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/Function.h>
#include <Containers/StringView.h>

#include <atomic>

// Work is done on a thread of its own where the platform has threads
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
#	define NCINE_BACKGROUND_JOB_THREADED
#	include "../../../nCine/Threading/Thread.h"
#endif

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
	using Death::Containers::StringView;
#	if defined(NCINE_BACKGROUND_JOB_THREADED)
	using nCine::Thread;
#	endif
#endif

	/**
		@brief How far work done off the user interface is, shared by the work and the window that waits for it
	*/
	class JobProgress
	{
	public:
		JobProgress() : _value(0.0f), _cancelled(false) {}

		/** @brief Sets how far the work is, from 0 to 1, returns `false` if it should stop */
		bool Report(float value) {
			_value.store(value, std::memory_order_relaxed);
			return !_cancelled.load(std::memory_order_relaxed);
		}
		/** @brief Returns `true` if the work should stop */
		bool IsCancelled() const {
			return _cancelled.load(std::memory_order_relaxed);
		}
		/** @brief Returns how far the work is, from 0 to 1 */
		float GetValue() const {
			return _value.load(std::memory_order_relaxed);
		}
		/** @brief Asks the work to stop */
		void Cancel() {
			_cancelled.store(true, std::memory_order_relaxed);
		}

	private:
		std::atomic<float> _value;
		std::atomic<bool> _cancelled;
	};

	/**
		@brief Work done off the user interface, on a thread of its own where the platform has threads

		The work reports how far it is through the @ref JobProgress it gets, and stops when it's asked to. It must not
		touch anything the user interface uses meanwhile, only what it was given. Once @ref IsDone() returns `true`,
		the results can be taken after @ref Wait(), which returns at once then. A job that goes away before its work
		ended asks it to stop and waits for it. A job runs its work once, it can't be moved, the thread refers to it.
	*/
	class BackgroundJob
	{
	public:
		BackgroundJob();
		~BackgroundJob();

		BackgroundJob(const BackgroundJob&) = delete;
		BackgroundJob& operator=(const BackgroundJob&) = delete;

		/**
		 * @brief Starts the work on a thread of its own named @p threadName
		 *
		 * Without threads, or if the thread can't be started, the work is done right away. Returns `true` if it runs
		 * on the thread.
		 */
		bool Start(Function<void(JobProgress&)>&& work, const char* threadName);
		/** @brief Does the work right away, on the calling thread */
		void RunHere(Function<void(JobProgress&)>&& work);
		/** @brief Returns `true` once the work ended, done or stopped */
		bool IsDone() const {
			return _done.load(std::memory_order_acquire);
		}
		/** @brief Waits for the work to end, the thread is joined then */
		void Wait();
		/** @brief Returns how far the work is */
		JobProgress& GetProgress() {
			return _progress;
		}
		/** @brief Asks the work to stop, see @ref JobProgress::IsCancelled() */
		void Cancel() {
			_progress.Cancel();
		}

	private:
		JobProgress _progress;
		Function<void(JobProgress&)> _work;
		std::atomic<bool> _done;
#if defined(NCINE_BACKGROUND_JOB_THREADED)
		Thread _worker;
		const char* _threadName;
		bool _started;

		static void RunOnThread(void* arg);
#endif

		// Does the work and tells it's done
		void RunWork();
	};
}

#endif
