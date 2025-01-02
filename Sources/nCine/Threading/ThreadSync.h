#pragma once

#include <CommonWindows.h>

#if !defined(DEATH_TARGET_WINDOWS)
#	include <pthread.h>
#endif

namespace nCine
{
	/// Mutex for threads synchronization
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

	/// Condition variable for threads synchronization
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

	/// Read/write lock for threads synchronization
	class ReadWriteLock
	{
	public:
		ReadWriteLock();
		~ReadWriteLock();

		inline void EnterReadLock() {
#if defined(DEATH_TARGET_WINDOWS)
			::AcquireSRWLockShared(&rwlock_);
#else
			pthread_rwlock_rdlock(&rwlock_);
#endif
		}
		inline void EnterWriteLock() {
#if defined(DEATH_TARGET_WINDOWS)
			::AcquireSRWLockExclusive(&rwlock_);
#else
			pthread_rwlock_wrlock(&rwlock_);
#endif
		}
		inline int TryEnterReadLock() {
#if defined(DEATH_TARGET_WINDOWS)
			return ::TryAcquireSRWLockShared(&rwlock_);
#else
			return pthread_rwlock_tryrdlock(&rwlock_);
#endif
		}
		inline int TryEnterWriteLock() {
#if defined(DEATH_TARGET_WINDOWS)
			return ::TryAcquireSRWLockExclusive(&rwlock_);
#else
			return pthread_rwlock_trywrlock(&rwlock_);
#endif
		}
		inline void ExitReadLock() {
#if defined(DEATH_TARGET_WINDOWS)
			::ReleaseSRWLockShared(&rwlock_);
#else
			pthread_rwlock_unlock(&rwlock_);
#endif
		}
		inline void ExitWriteLock() {
#if defined(DEATH_TARGET_WINDOWS)
			::ReleaseSRWLockExclusive(&rwlock_);
#else
			pthread_rwlock_unlock(&rwlock_);
#endif
		}

	private:
#if defined(DEATH_TARGET_WINDOWS)
		SRWLOCK rwlock_;
#else
		pthread_rwlock_t rwlock_;
#endif

		/// Deleted copy constructor
		ReadWriteLock(const ReadWriteLock&) = delete;
		/// Deleted assignment operator
		ReadWriteLock& operator=(const ReadWriteLock&) = delete;
	};

#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_WINDOWS)

	/// Barrier for threads synchronization
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

#endif

}