#if defined(WITH_THREADS)

#include "ThreadSync.h"

#if defined(DEATH_TARGET_WINDOWS)

namespace nCine
{
	///////////////////////////////////////////////////////////
	// Mutex CLASS
	///////////////////////////////////////////////////////////

	Mutex::Mutex()
	{
		::InitializeCriticalSection(&handle_);
	}

	Mutex::~Mutex()
	{
		::DeleteCriticalSection(&handle_);
	}

	void Mutex::Lock()
	{
		::EnterCriticalSection(&handle_);
	}

	void Mutex::Unlock()
	{
		::LeaveCriticalSection(&handle_);
	}

	int Mutex::TryLock()
	{
		return ::TryEnterCriticalSection(&handle_);
	}

	///////////////////////////////////////////////////////////
	// CondVariable CLASS
	///////////////////////////////////////////////////////////

	CondVariable::CondVariable()
		: waitersCount_(0)
	{
		events_[0] = ::CreateEvent(nullptr, FALSE, FALSE, nullptr); // Signal
		events_[1] = ::CreateEvent(nullptr, TRUE, FALSE, nullptr); // Broadcast
		::InitializeCriticalSection(&waitersCountLock_);
	}

	CondVariable::~CondVariable()
	{
		::CloseHandle(events_[0]); // Signal
		::CloseHandle(events_[1]); // Broadcast
		::DeleteCriticalSection(&waitersCountLock_);
	}

	void CondVariable::Wait(Mutex& mutex)
	{
		::EnterCriticalSection(&waitersCountLock_);
		waitersCount_++;
		::LeaveCriticalSection(&waitersCountLock_);

		mutex.Unlock();
		WaitEvents();
		mutex.Lock();
	}

	void CondVariable::Signal()
	{
		::EnterCriticalSection(&waitersCountLock_);
		const bool haveWaiters = (waitersCount_ > 0);
		::LeaveCriticalSection(&waitersCountLock_);

		if (haveWaiters) {
			::SetEvent(events_[0]); // Signal
		}
	}

	void CondVariable::Broadcast()
	{
		::EnterCriticalSection(&waitersCountLock_);
		const bool haveWaiters = (waitersCount_ > 0);
		::LeaveCriticalSection(&waitersCountLock_);

		if (haveWaiters) {
			::SetEvent(events_[1]); // Broadcast
		}
	}

	void CondVariable::WaitEvents()
	{
		const int result = ::WaitForMultipleObjects(2, events_, FALSE, INFINITE);

		::EnterCriticalSection(&waitersCountLock_);
		waitersCount_--;
		const bool isLastWaiter = (result == (WAIT_OBJECT_0 + 1)) && (waitersCount_ == 0);
		::LeaveCriticalSection(&waitersCountLock_);

		if (isLastWaiter) {
			::ResetEvent(events_[1]); // Broadcast
		}
	}


	///////////////////////////////////////////////////////////
	// ReadWriteLock CLASS
	///////////////////////////////////////////////////////////

	ReadWriteLock::ReadWriteLock() : rwlock_(SRWLOCK_INIT)
	{
	}

	ReadWriteLock::~ReadWriteLock()
	{
	}
}

#endif

#endif