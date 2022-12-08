#pragma once

#include <Common.h>

#if !defined(DEATH_TARGET_WINDOWS)
#	include <pthread.h>
#endif

namespace nCine
{
	/// Mutex class (threads synchronization)
	class Mutex
	{
	public:
		Mutex();
		~Mutex();

		void Lock();
		void Unlock();
		int TryLock();

#if defined(WITH_TRACY)
		inline int try_lock() {
			return TryLock();
		}
#endif

	private:
#if defined(DEATH_TARGET_WINDOWS)
		CRITICAL_SECTION handle_;
#else
		pthread_mutex_t mutex_;
#endif

		/// Deleted copy constructor
		Mutex(const Mutex&) = delete;
		/// Deleted assignment operator
		Mutex& operator=(const Mutex&) = delete;

		friend class CondVariable;
	};

	/// Condition variable class (threads synchronization)
	/*! Windows version based on the <em>TinyThread++</em> library implementation.
	 *  More info at: http://www.cs.wustl.edu/~schmidt/win32-cv-1.html */
	class CondVariable
	{
	public:
		CondVariable();
		~CondVariable();

		void Wait(Mutex& mutex);
		void Signal();
		void Broadcast();

	private:
#if defined(DEATH_TARGET_WINDOWS)
		HANDLE events_[2];
		unsigned int waitersCount_;
		CRITICAL_SECTION waitersCountLock_;

		void WaitEvents();
#else
		pthread_cond_t cond_;
#endif

		/// Deleted copy constructor
		CondVariable(const CondVariable&) = delete;
		/// Deleted assignment operator
		CondVariable& operator=(const CondVariable&) = delete;
	};

#if !defined(DEATH_TARGET_WINDOWS)

	/// Read/write lock class (threads synchronization)
	class RWLock
	{
	public:
		RWLock();
		~RWLock();

		inline void EnterReadLock() {
			pthread_rwlock_rdlock(&rwlock_);
		}
		inline void EnterWriteLock() {
			pthread_rwlock_wrlock(&rwlock_);
		}
		inline int TryEnterReadLock() {
			return pthread_rwlock_tryrdlock(&rwlock_);
		}
		inline int TryEnterWriteLock() {
			return pthread_rwlock_trywrlock(&rwlock_);
		}
		inline void Exit() {
			pthread_rwlock_unlock(&rwlock_);
		}

	private:
		pthread_rwlock_t rwlock_;

		/// Deleted copy constructor
		RWLock(const RWLock&) = delete;
		/// Deleted assignment operator
		RWLock& operator=(const RWLock&) = delete;
	};

#	if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_APPLE)

	/// Barrier class (threads synchronization)
	class Barrier
	{
	public:
		/// Creates a barrier for the specified amount of waiting threads
		explicit Barrier(unsigned int count);
		~Barrier();

		/// The calling thread waits at the barrier
		inline int Wait() {
			return pthread_barrier_wait(&barrier_);
		}

	private:
		pthread_barrier_t barrier_;

		/// Deleted copy constructor
		Barrier(const Barrier&) = delete;
		/// Deleted assignment operator
		Barrier& operator=(const Barrier&) = delete;
	};

#	endif
#endif

}