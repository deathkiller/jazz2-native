#pragma once

#include <CommonWindows.h>

#if defined(WITH_THREADS) || defined(DOXYGEN_GENERATING_OUTPUT)

#if !defined(DEATH_TARGET_WINDOWS)
#	include <pthread.h>
#endif

namespace nCine
{
	/**
		@brief Mutex for thread synchronization
		
		Wraps a platform mutex (a critical section on Windows, a `pthread_mutex_t` otherwise).
		Non-copyable. Used together with @ref CondVariable for condition waiting.
	*/
	class Mutex
	{
		friend class CondVariable;

	public:
		Mutex();
		~Mutex();

		Mutex(const Mutex&) = delete;
		Mutex& operator=(const Mutex&) = delete;

		/** @brief Acquires the lock, blocking until it becomes available */
		void Lock();
		/** @brief Releases the lock */
		void Unlock();
		/** @brief Tries to acquire the lock without blocking, returning zero on success */
		int TryLock();

#if defined(WITH_TRACY)
		inline int try_lock() {
			return TryLock();
		}
#endif

	private:
#if defined(DEATH_TARGET_WINDOWS)
		CRITICAL_SECTION _handle;
#else
		pthread_mutex_t _mutex;
#endif
	};

	/**
		@brief Condition variable for thread synchronization
		
		The Windows implementation is based on the *TinyThread++* library; see
		http://www.cs.wustl.edu/~schmidt/win32-cv-1.html for the underlying technique.
		Non-copyable.
	*/
	class CondVariable
	{
	public:
		CondVariable();
		~CondVariable();

		CondVariable(const CondVariable&) = delete;
		CondVariable& operator=(const CondVariable&) = delete;

		/** @brief Atomically releases the mutex and blocks until the variable is signalled, then reacquires it */
		void Wait(Mutex& mutex);
		/** @brief Wakes up one thread waiting on the variable */
		void Signal();
		/** @brief Wakes up all threads waiting on the variable */
		void Broadcast();

	private:
#if defined(DEATH_TARGET_WINDOWS)
		HANDLE _events[2];
		unsigned int _waitersCount;
		CRITICAL_SECTION _waitersCountLock;

		void WaitEvents();
#else
		pthread_cond_t _cond;
#endif
	};

	/**
		@brief Read/write lock for thread synchronization
		
		Wraps a slim reader/writer lock on Windows and a `pthread_rwlock_t` otherwise. Allows any
		number of concurrent readers but only one exclusive writer at a time. Non-copyable.
	*/
	class ReadWriteLock
	{
	public:
		ReadWriteLock();
		~ReadWriteLock();

		ReadWriteLock(const ReadWriteLock&) = delete;
		ReadWriteLock& operator=(const ReadWriteLock&) = delete;

		/** @brief Acquires the lock for shared (read) access, blocking until available */
		inline void EnterReadLock() {
#if defined(DEATH_TARGET_WINDOWS)
			::AcquireSRWLockShared(&_rwlock);
#else
			pthread_rwlock_rdlock(&_rwlock);
#endif
		}
		/** @brief Acquires the lock for exclusive (write) access, blocking until available */
		inline void EnterWriteLock() {
#if defined(DEATH_TARGET_WINDOWS)
			::AcquireSRWLockExclusive(&_rwlock);
#else
			pthread_rwlock_wrlock(&_rwlock);
#endif
		}
		/** @brief Tries to acquire shared (read) access without blocking */
		inline int TryEnterReadLock() {
#if defined(DEATH_TARGET_WINDOWS)
			return ::TryAcquireSRWLockShared(&_rwlock);
#else
			return pthread_rwlock_tryrdlock(&_rwlock);
#endif
		}
		/** @brief Tries to acquire exclusive (write) access without blocking */
		inline int TryEnterWriteLock() {
#if defined(DEATH_TARGET_WINDOWS)
			return ::TryAcquireSRWLockExclusive(&_rwlock);
#else
			return pthread_rwlock_trywrlock(&_rwlock);
#endif
		}
		/** @brief Releases shared (read) access */
		inline void ExitReadLock() {
#if defined(DEATH_TARGET_WINDOWS)
			::ReleaseSRWLockShared(&_rwlock);
#else
			pthread_rwlock_unlock(&_rwlock);
#endif
		}
		/** @brief Releases exclusive (write) access */
		inline void ExitWriteLock() {
#if defined(DEATH_TARGET_WINDOWS)
			::ReleaseSRWLockExclusive(&_rwlock);
#else
			pthread_rwlock_unlock(&_rwlock);
#endif
		}

	private:
#if defined(DEATH_TARGET_WINDOWS)
		SRWLOCK _rwlock;
#else
		pthread_rwlock_t _rwlock;
#endif
	};

#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_WINDOWS)

	/**
		@brief Barrier for thread synchronization
		
		Blocks each arriving thread until the configured number of threads have reached the barrier,
		then releases them all at once. Wraps a `pthread_barrier_t` and is therefore unavailable on
		Android, Apple and Windows. Non-copyable.
	*/
	class Barrier
	{
	public:
		/** @brief Creates a barrier for the given number of waiting threads */
		explicit Barrier(std::uint32_t count);
		~Barrier();

		Barrier(const Barrier&) = delete;
		Barrier& operator=(const Barrier&) = delete;

		/** @brief Blocks the calling thread until the required number of threads have arrived */
		inline std::int32_t Wait() {
			return pthread_barrier_wait(&_barrier);
		}

	private:
		pthread_barrier_t _barrier;
	};

#endif

}

#endif