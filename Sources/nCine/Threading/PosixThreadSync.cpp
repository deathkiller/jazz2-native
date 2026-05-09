#if defined(WITH_THREADS)

#include "ThreadSync.h"
#include <errno.h>
#include <time.h>

#if !defined(DEATH_TARGET_WINDOWS)

namespace nCine
{
	///////////////////////////////////////////////////////////
	// Mutex CLASS
	///////////////////////////////////////////////////////////

	Mutex::Mutex()
	{
		pthread_mutex_init(&mutex_, nullptr);
	}

	Mutex::~Mutex()
	{
		pthread_mutex_destroy(&mutex_);
	}

	void Mutex::Lock()
	{
		pthread_mutex_lock(&mutex_);
	}

	void Mutex::Unlock()
	{
		pthread_mutex_unlock(&mutex_);
	}

	int Mutex::TryLock()
	{
		return pthread_mutex_trylock(&mutex_);
	}

	///////////////////////////////////////////////////////////
	// CondVariable CLASS
	///////////////////////////////////////////////////////////

	CondVariable::CondVariable()
	{
		pthread_cond_init(&cond_, nullptr);
	}

	CondVariable::~CondVariable()
	{
		pthread_cond_destroy(&cond_);
	}

	void CondVariable::Wait(Mutex& mutex)
	{
		pthread_cond_wait(&cond_, &(mutex.mutex_));
	}

	void CondVariable::Signal()
	{
		pthread_cond_signal(&cond_);
	}

	void CondVariable::Broadcast()
	{
		pthread_cond_broadcast(&cond_);
	}

	bool CondVariable::WaitTimed(Mutex& mutex, std::uint32_t milliseconds)
	{
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += static_cast<time_t>(milliseconds / 1000);
		ts.tv_nsec += static_cast<long>((milliseconds % 1000) * 1000000L);
		if (ts.tv_nsec >= 1000000000L) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000L;
		}
		return (pthread_cond_timedwait(&cond_, &(mutex.mutex_), &ts) != ETIMEDOUT);
	}

	///////////////////////////////////////////////////////////
	// ReadWriteLock CLASS
	///////////////////////////////////////////////////////////

	ReadWriteLock::ReadWriteLock()
	{
		pthread_rwlock_init(&rwlock_, nullptr);
	}

	ReadWriteLock::~ReadWriteLock()
	{
		pthread_rwlock_destroy(&rwlock_);
	}

	///////////////////////////////////////////////////////////
	// Barrier CLASS
	///////////////////////////////////////////////////////////

#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_APPLE)

	Barrier::Barrier(std::uint32_t count)
	{
		pthread_barrier_init(&barrier_, nullptr, count);
	}

	Barrier::~Barrier()
	{
		pthread_barrier_destroy(&barrier_);
	}

#endif

}

#endif

#endif