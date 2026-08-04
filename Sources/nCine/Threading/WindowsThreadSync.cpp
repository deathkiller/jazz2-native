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
		::InitializeCriticalSection(&_handle);
	}

	Mutex::~Mutex()
	{
		::DeleteCriticalSection(&_handle);
	}

	void Mutex::Lock()
	{
		::EnterCriticalSection(&_handle);
	}

	void Mutex::Unlock()
	{
		::LeaveCriticalSection(&_handle);
	}

	int Mutex::TryLock()
	{
		return ::TryEnterCriticalSection(&_handle);
	}

	///////////////////////////////////////////////////////////
	// CondVariable CLASS
	///////////////////////////////////////////////////////////

	CondVariable::CondVariable()
		: _waitersCount(0)
	{
		_events[0] = ::CreateEvent(nullptr, FALSE, FALSE, nullptr); // Signal
		_events[1] = ::CreateEvent(nullptr, TRUE, FALSE, nullptr); // Broadcast
		::InitializeCriticalSection(&_waitersCountLock);
	}

	CondVariable::~CondVariable()
	{
		::CloseHandle(_events[0]); // Signal
		::CloseHandle(_events[1]); // Broadcast
		::DeleteCriticalSection(&_waitersCountLock);
	}

	void CondVariable::Wait(Mutex& mutex)
	{
		::EnterCriticalSection(&_waitersCountLock);
		_waitersCount++;
		::LeaveCriticalSection(&_waitersCountLock);

		mutex.Unlock();
		WaitEvents();
		mutex.Lock();
	}

	void CondVariable::Signal()
	{
		::EnterCriticalSection(&_waitersCountLock);
		const bool haveWaiters = (_waitersCount > 0);
		::LeaveCriticalSection(&_waitersCountLock);

		if (haveWaiters) {
			::SetEvent(_events[0]); // Signal
		}
	}

	void CondVariable::Broadcast()
	{
		::EnterCriticalSection(&_waitersCountLock);
		const bool haveWaiters = (_waitersCount > 0);
		::LeaveCriticalSection(&_waitersCountLock);

		if (haveWaiters) {
			::SetEvent(_events[1]); // Broadcast
		}
	}

	void CondVariable::WaitEvents()
	{
		const int result = ::WaitForMultipleObjects(2, _events, FALSE, INFINITE);

		::EnterCriticalSection(&_waitersCountLock);
		_waitersCount--;
		const bool isLastWaiter = (result == (WAIT_OBJECT_0 + 1)) && (_waitersCount == 0);
		::LeaveCriticalSection(&_waitersCountLock);

		if (isLastWaiter) {
			::ResetEvent(_events[1]); // Broadcast
		}
	}


	///////////////////////////////////////////////////////////
	// ReadWriteLock CLASS
	///////////////////////////////////////////////////////////

	ReadWriteLock::ReadWriteLock() : _rwlock(SRWLOCK_INIT)
	{
	}

	ReadWriteLock::~ReadWriteLock()
	{
	}
}

#endif

#endif