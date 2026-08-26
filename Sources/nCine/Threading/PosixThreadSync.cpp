#if defined(WITH_THREADS)

#include "ThreadSync.h"

#if !defined(DEATH_TARGET_WINDOWS)

namespace nCine
{
	///////////////////////////////////////////////////////////
	// Mutex CLASS
	///////////////////////////////////////////////////////////

	Mutex::Mutex()
	{
		pthread_mutex_init(&_mutex, nullptr);
	}

	Mutex::~Mutex()
	{
		pthread_mutex_destroy(&_mutex);
	}

	void Mutex::Lock()
	{
		pthread_mutex_lock(&_mutex);
	}

	void Mutex::Unlock()
	{
		pthread_mutex_unlock(&_mutex);
	}

	int Mutex::TryLock()
	{
		return pthread_mutex_trylock(&_mutex);
	}

	///////////////////////////////////////////////////////////
	// CondVariable CLASS
	///////////////////////////////////////////////////////////

	CondVariable::CondVariable()
	{
		pthread_cond_init(&_cond, nullptr);
	}

	CondVariable::~CondVariable()
	{
		pthread_cond_destroy(&_cond);
	}

	void CondVariable::Wait(Mutex& mutex)
	{
		pthread_cond_wait(&_cond, &(mutex._mutex));
	}

	void CondVariable::Signal()
	{
		pthread_cond_signal(&_cond);
	}

	void CondVariable::Broadcast()
	{
		pthread_cond_broadcast(&_cond);
	}

	///////////////////////////////////////////////////////////
	// ReadWriteLock CLASS
	///////////////////////////////////////////////////////////

	ReadWriteLock::ReadWriteLock()
	{
		pthread_rwlock_init(&_rwlock, nullptr);
	}

	ReadWriteLock::~ReadWriteLock()
	{
		pthread_rwlock_destroy(&_rwlock);
	}

	///////////////////////////////////////////////////////////
	// Barrier CLASS
	///////////////////////////////////////////////////////////

#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_AMIGAOS4)

	Barrier::Barrier(std::uint32_t count)
	{
		pthread_barrier_init(&_barrier, nullptr, count);
	}

	Barrier::~Barrier()
	{
		pthread_barrier_destroy(&_barrier);
	}

#endif

}

#endif

#endif