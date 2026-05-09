#pragma once

#include <CommonWindows.h>

#if defined(WITH_THREADS) || defined(DOXYGEN_GENERATING_OUTPUT)

#if !defined(DEATH_TARGET_WINDOWS)
#	include <pthread.h>
#endif

namespace nCine
{
	/// Mutex for threads synchronization
	class Mutex
	{
		friend class CondVariable;

	public:
		Mutex();
		~Mutex();

		Mutex(const Mutex&) = delete;
		Mutex& operator=(const Mutex&) = delete;

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
	};

	/// Condition variable for threads synchronization
	/*! Windows version based on the <em>TinyThread++</em> library implementation.
	 *  More info at: http://www.cs.wustl.edu/~schmidt/win32-cv-1.html */
	class CondVariable
	{
	public:
		CondVariable();
		~CondVariable();

		CondVariable(const CondVariable&) = delete;
		CondVariable& operator=(const CondVariable&) = delete;

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
	};

	/// Read/write lock for threads synchronization
	class ReadWriteLock
	{
	public:
		ReadWriteLock();
		~ReadWriteLock();

		ReadWriteLock(const ReadWriteLock&) = delete;
		ReadWriteLock& operator=(const ReadWriteLock&) = delete;

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
	};

#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_WINDOWS)

	/// Barrier for threads synchronization
	class Barrier
	{
	public:
		/// Creates a barrier for the specified amount of waiting threads
		explicit Barrier(std::uint32_t count);
		~Barrier();

		Barrier(const Barrier&) = delete;
		Barrier& operator=(const Barrier&) = delete;

		/// The calling thread waits at the barrier
		inline std::int32_t Wait() {
			return pthread_barrier_wait(&barrier_);
		}

	private:
		pthread_barrier_t barrier_;
	};

#endif

}

#endif