#pragma once

#include <CommonWindows.h>

#if defined(WITH_THREADS) || defined(DOXYGEN_GENERATING_OUTPUT)
#	if defined(DEATH_TARGET_APPLE)
#		include <mach/mach_init.h>
#	elif !defined(DEATH_TARGET_WINDOWS)
#		include <pthread.h>
#	endif
#endif

namespace nCine
{
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH)

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
		/** @brief Returns `true` if the specified CPU number belongs to the set */
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

	/** @brief Thread */
	class Thread
	{
	public:
		/** @brief Puts the current thread to sleep for the specified number of milliseconds */
		static void Sleep(std::uint32_t milliseconds);

#if defined(WITH_THREADS) || defined(DOXYGEN_GENERATING_OUTPUT)
		using ThreadFuncDelegate = void (*)(void*);

		/** @brief Default constructor */
		Thread();
		/** @brief Creates a thread around a function and runs it immediately */
		Thread(ThreadFuncDelegate threadFunc, void* threadArg);

		~Thread();

		Thread(const Thread& other);
		Thread& operator=(const Thread& other);
		Thread(Thread&& other) noexcept;
		Thread& operator=(Thread&& other) noexcept;

		/** @brief Whether the thread is running */
		explicit operator bool() const;

		/** @brief Returns the number of processors in the machine */
		static std::uint32_t GetProcessorCount();

		/** @brief Joins the thread */
		bool Join();
		/** @brief Detaches the running thread from the object */
		void Detach();

		/** @brief Sets the thread name (not supported on Apple, Emscripten and Switch) */
		void SetName(const char* name);

		/** @brief Sets the calling thread name (not supported on Emscripten and Switch) */
		static void SetCurrentName(const char* name);

#	if !defined(DEATH_TARGET_SWITCH)
		/** @brief Gets the thread priority */
		std::int32_t GetPriority() const;
		/** @brief Sets the thread priority */
		void SetPriority(std::int32_t priority);
#	endif

		/** @brief Returns the calling thread ID */
		static std::uintptr_t GetCurrentId();
		/** @brief Terminates the calling thread */
		[[noreturn]] static void Exit();
		/** @brief Yields the calling thread in favour of another one with the same priority */
		static void YieldExecution();

		/** @brief Tries to cancel or terminate the thread (depending on operating system) */
		bool Abort();

#	if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH)
		/** @brief Gets the thread affinity mask */
		ThreadAffinityMask GetAffinityMask() const;
		/** @brief Sets the thread affinity mask*/ 
		void SetAffinityMask(ThreadAffinityMask affinityMask);
#	endif

	private:
		struct SharedBlock;

		Thread(SharedBlock* sharedBlock);

		SharedBlock* _sharedBlock;

		/** @brief Spawns a new thread if the object hasn't one already associated */
		void Run(ThreadFuncDelegate threadFunc, void* threadArg);

#	if defined(DEATH_TARGET_WINDOWS)
#		if defined(DEATH_TARGET_MINGW)
		static unsigned int(__attribute__((__stdcall__)) WrapperFunction)(void* arg);
#		else
		static unsigned int __stdcall WrapperFunction(void* arg);
#		endif
#	else
		static void* WrapperFunction(void* arg);
#	endif
#else
		Thread() = delete;
		~Thread() = delete;
#endif
	};
}