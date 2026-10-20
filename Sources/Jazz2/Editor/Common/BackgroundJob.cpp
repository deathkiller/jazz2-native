#include "BackgroundJob.h"

#if defined(WITH_EDITOR)

namespace Jazz2::Editor
{
	BackgroundJob::BackgroundJob()
		: _done(false)
#if defined(NCINE_BACKGROUND_JOB_THREADED)
			, _threadName(nullptr), _started(false)
#endif
	{
	}

	BackgroundJob::~BackgroundJob()
	{
		// Gone before the work ended, as when the editor is closed, the work is stopped
		Cancel();
		Wait();
	}

	bool BackgroundJob::Start(Function<void(JobProgress&)>&& work, const char* threadName)
	{
		_work = Death::move(work);
#if defined(NCINE_BACKGROUND_JOB_THREADED)
		_threadName = threadName;
		_worker = Thread(RunOnThread, this);
		// A thread that isn't running has either done the work already or never started, the work is then done here
		if (!_worker && !_done.load(std::memory_order_acquire)) {
			LOGE("Thread \"{}\" couldn't be started, the work is done right away", threadName);
			RunWork();
			return false;
		}
		_started = true;
		return true;
#else
		RunWork();
		return false;
#endif
	}

	void BackgroundJob::RunHere(Function<void(JobProgress&)>&& work)
	{
		_work = Death::move(work);
		RunWork();
	}

	void BackgroundJob::Wait()
	{
#if defined(NCINE_BACKGROUND_JOB_THREADED)
		if (_started) {
			_worker.Join();
			_started = false;
		}
#endif
	}

	void BackgroundJob::RunWork()
	{
		if (_work) {
			_work(_progress);
		}
		_done.store(true, std::memory_order_release);
	}

#if defined(NCINE_BACKGROUND_JOB_THREADED)
	void BackgroundJob::RunOnThread(void* arg)
	{
		auto* job = static_cast<BackgroundJob*>(arg);
		Thread::SetCurrentName(job->_threadName);
		job->RunWork();
	}
#endif
}

#endif
