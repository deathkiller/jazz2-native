#pragma once

#include "Atomic.h"

#include <CommonWindows.h>

#if defined(DEATH_TARGET_WINDOWS)
#	include <process.h>
#	include <processthreadsapi.h>
#else
#	include <pthread.h>
#endif

#if defined(DEATH_TARGET_APPLE)
#	include <mach/mach_init.h>
#endif

namespace nCine
{
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH)

	/** @brief CPU affinity mask for a thread */
	class ThreadAffinityMask
	{
		friend class Thread;

	public:
		ThreadAffinityMask()
		{
			Zero();
		}

		ThreadAffinityMask(std::int32_t cpuNum)
		{
			Zero();
			Set(cpuNum);
		}

		/** @brief Clears the CPU set */
		void Zero();
		/** @brief Sets the specified CPU number to be included in the set */
		void Set(std::int32_t cpuNum);
		/** @brief Sets the specified CPU number to be excluded by the set */
		void Clear(std::int32_t cpuNum);
		/** @brief Returns true if the specified CPU number belongs to the set */
		bool IsSet(std::int32_t cpuNum);

	private:
#	if defined(DEATH_TARGET_WINDOWS)
		DWORD_PTR affinityMask_;
#	elif defined(DEATH_TARGET_APPLE)
		integer_t affinityTag_;
#	else
		cpu_set_t cpuSet_;
#	endif
	};

#endif

	/** @brief Thread class */
	class Thread
	{
	public:
		using ThreadFuncDelegate = void (*)(void*);

		/** @brief A default constructor for an object without the associated function */
		Thread();
		/** @brief Creates a thread around a function and runs it immediately */
		Thread(ThreadFuncDelegate threadFunc, void* threadArg);

		~Thread();

		Thread(const Thread& other)
		{
			// Copy constructor
			_sharedBlock = other._sharedBlock;

			if (_sharedBlock != nullptr) {
				_sharedBlock->_refCount.fetchAdd(1);
			}
		}

		Thread& operator=(const Thread& other)
		{
			Detach();

			// Copy assignment
			_sharedBlock = other._sharedBlock;

			if (_sharedBlock != nullptr) {
				_sharedBlock->_refCount.fetchAdd(1);
			}

			return *this;
		}

		Thread(Thread&& other) noexcept
		{
			// Move constructor
			_sharedBlock = other._sharedBlock;
			other._sharedBlock = nullptr;
		}

		Thread& operator=(Thread&& other) noexcept
		{
			Detach();

			// Move assignment
			_sharedBlock = other._sharedBlock;
			other._sharedBlock = nullptr;

			return *this;
		}

		/** @brief Returns the number of processors in the machine */
		static std::uint32_t GetProcessorCount();

		/** @brief Spawns a new thread if the object hasn't one already associated */
		void Run(ThreadFuncDelegate threadFunc, void* threadArg);
		/** @brief Joins the thread */
		bool Join();
		/** @brief Detaches the running thread from the object */
		void Detach();

		/** @brief Sets the thread name (not supported on Apple, Emscripten and Switch) */
		void SetName(const char* name);

		/** @brief Sets the calling thread name (not supported on Emscripten and Switch) */
		static void SetCurrentName(const char* name);

#if !defined(DEATH_TARGET_SWITCH)
		/** @brief Gets the thread priority */
		std::int32_t GetPriority() const;
		/** @brief Sets the thread priority */
		void SetPriority(std::int32_t priority);
#endif

		/** @brief Returns the calling thread ID */
		static std::uintptr_t GetCurrentId();
		/** @brief Terminates the calling thread */
		[[noreturn]] static void Exit();
		/** @brief Yields the calling thread in favour of another one with the same priority */
		static void YieldExecution();

#if !defined(DEATH_TARGET_ANDROID)
		/** @brief Tries to cancel or terminate the thread (depending on operating system) */
		bool Abort();

#	if !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH)
		/** @brief Gets the thread affinity mask */
		ThreadAffinityMask GetAffinityMask() const;
		/** @brief Sets the thread affinity mask*/ 
		void SetAffinityMask(ThreadAffinityMask affinityMask);
#	endif
#endif

	private:
		struct SharedBlock
		{
			Atomic32 _refCount;
#if defined(DEATH_TARGET_WINDOWS)
			HANDLE _handle;
#else
			pthread_t _handle;
#endif
			ThreadFuncDelegate _threadFunc;
			void* _threadArg;
		};

		Thread(SharedBlock* sharedBlock);

		SharedBlock* _sharedBlock;

#if defined(DEATH_TARGET_WINDOWS)
#	if defined(DEATH_TARGET_MINGW)
		static unsigned int(__attribute__((__stdcall__)) WrapperFunction)(void* arg);
#	else
		static unsigned int __stdcall WrapperFunction(void* arg);
#	endif
#else
		static void* WrapperFunction(void* arg);
#endif
	};
}