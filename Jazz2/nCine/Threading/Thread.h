#pragma once

#include <Common.h>

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
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN)

	/// A class representing the CPU affinity mask for a thread
	class ThreadAffinityMask
	{
	public:
		ThreadAffinityMask() {
			zero();
		}
		ThreadAffinityMask(int cpuNum)
		{
			zero();
			set(cpuNum);
		}

		/// Clears the CPU set
		void zero();
		/// Sets the specified CPU number to be included in the set
		void set(int cpuNum);
		/// Sets the specified CPU number to be excluded by the set
		void clear(int cpuNum);
		/// Returns true if the specified CPU number belongs to the set
		bool isSet(int cpuNum);

	private:
#if defined(DEATH_TARGET_WINDOWS)
		DWORD_PTR affinityMask_;
#elif defined(DEATH_TARGET_APPLE)
		integer_t affinityTag_;
#else
		cpu_set_t cpuSet_;
#endif

		friend class Thread;
	};

#endif

	/// Thread class
	class Thread
	{
	public:
		using ThreadFunctionPtr = void (*)(void*);

		/// A default constructor for an object without the associated function
		Thread();
		/// Creates a thread around a function and runs it
		Thread(ThreadFunctionPtr startFunction, void* arg);

		/// Returns the number of processors in the machine
		static unsigned int numProcessors();

		/// Spawns a new thread if the object hasn't one already associated
		void run(ThreadFunctionPtr startFunction, void* arg);
		/// Joins the thread
		void* join();

#ifndef DEATH_TARGET_EMSCRIPTEN
#ifndef __APPLE__
		/// Sets the thread name
		void setName(const char* name);
#endif

		/// Sets the calling thread name
		static void setSelfName(const char* name);
#endif

		/// Gets the thread priority
		int priority() const;
		/// Sets the thread priority
		void setPriority(int priority);

		/// Returns the calling thread id
		static long int self();
		/// Terminates the calling thread
		[[noreturn]] static void exit(void* retVal);
		/// Yields the calling thread in favour of another one with the same priority
		static void yieldExecution();

#ifndef DEATH_TARGET_ANDROID
		/// Asks the thread for termination
		void cancel();

#ifndef DEATH_TARGET_EMSCRIPTEN
		/// Gets the thread affinity mask
		ThreadAffinityMask affinityMask() const;
		/// Sets the thread affinity mask
		void setAffinityMask(ThreadAffinityMask affinityMask);
#endif
#endif

	private:
		/// The structure wrapping the information for thread creation
		struct ThreadInfo
		{
			ThreadInfo()
				: startFunction(nullptr), threadArg(nullptr) {}
			ThreadFunctionPtr startFunction;
			void* threadArg;
		};

#if defined(DEATH_TARGET_WINDOWS)
		HANDLE handle_;
#else
		pthread_t tid_;
#endif

		ThreadInfo threadInfo_;
		/// The wrapper start function for thread creation
#if defined(DEATH_TARGET_WINDOWS)
#	if defined(DEATH_TARGET_MINGW)
		static unsigned int(__attribute__((__stdcall__)) wrapperFunction)(void* arg);
#	else
		static unsigned int __stdcall wrapperFunction(void* arg);
#	endif
#else
		static void* wrapperFunction(void* arg);
#endif
	};

}
