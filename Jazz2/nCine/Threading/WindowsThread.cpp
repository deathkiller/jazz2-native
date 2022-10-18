#if defined(WITH_THREADS)

#include "Thread.h"

#if defined(DEATH_TARGET_WINDOWS)
#include "../../Common.h"

#include <utility>

namespace nCine
{
	namespace
	{
#if !defined PTW32_VERSION && !defined __WINPTHREADS_VERSION
		const unsigned int MaxThreadNameLength = 256;

		void SetThreadName(HANDLE handle, const char* name)
		{
			if (handle == 0) return;

#	if defined(NTDDI_WIN10_RS2) && NTDDI_VERSION >= NTDDI_WIN10_RS2
			wchar_t buffer[MaxThreadNameLength];
			size_t charsConverted;
			mbstowcs_s(&charsConverted, buffer, name, MaxThreadNameLength);
			const HANDLE threadHandle = (handle != reinterpret_cast<HANDLE>(-1)) ? handle : GetCurrentThread();
			::SetThreadDescription(threadHandle, buffer);
#	elif !defined(DEATH_TARGET_MINGW)
			constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

#		pragma pack(push, 8)
			struct THREADNAME_INFO
			{
				DWORD dwType;
				LPCSTR szName;
				DWORD dwThreadID;
				DWORD dwFlags;
			};
#		pragma pack(pop)

			const DWORD threadId = (handle != reinterpret_cast<HANDLE>(-1)) ? GetThreadId(handle) : GetCurrentThreadId();
			THREADNAME_INFO info;
			info.dwType = 0x1000;
			info.szName = name;
			info.dwThreadID = threadId;
			info.dwFlags = 0;

			__try {
				RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), reinterpret_cast<ULONG_PTR*>(&info));
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
#	endif
		}
#endif
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void ThreadAffinityMask::Zero()
	{
		affinityMask_ = 0LL;
	}

	void ThreadAffinityMask::Set(int cpuNum)
	{
		affinityMask_ |= 1LL << cpuNum;
	}

	void ThreadAffinityMask::Clear(int cpuNum)
	{
		affinityMask_ &= ~(1LL << cpuNum);
	}

	bool ThreadAffinityMask::IsSet(int cpuNum)
	{
		return ((affinityMask_ >> cpuNum) & 1LL) != 0;
	}

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	Thread::Thread()
		: handle_(0)
	{
	}

	Thread::Thread(ThreadFunctionPtr startFunction, void* arg)
		: handle_(0)
	{
		Run(startFunction, arg);
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	unsigned int Thread::GetProcessorCount()
	{
		unsigned int numProcs = 0;

		SYSTEM_INFO si;
		GetSystemInfo(&si);
		numProcs = si.dwNumberOfProcessors;

		return numProcs;
	}

	void Thread::Run(ThreadFunctionPtr startFunction, void* arg)
	{
		if (handle_ == 0) {
			threadInfo_.startFunction = startFunction;
			threadInfo_.threadArg = arg;
			handle_ = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, WrapperFunction, &threadInfo_, 0, nullptr));
			FATAL_ASSERT_MSG(handle_, "_beginthreadex() failed");
		} else {
			LOGW_X("Thread %u is already running", handle_);
		}
	}

	void* Thread::Join()
	{
		::WaitForSingleObject(handle_, INFINITE);
		handle_ = 0;
		return nullptr;
	}

	void Thread::SetName(const char* name)
	{
		SetThreadName(handle_, name);
	}

	void Thread::SetSelfName(const char* name)
	{
#if defined(WITH_TRACY)
		tracy::SetThreadName(name);
#else
		SetThreadName(reinterpret_cast<HANDLE>(-1), name);
#endif
	}

	int Thread::GetPriority() const
	{
		return (handle_ != 0 ? GetThreadPriority(handle_) : 0);
	}

	void Thread::SetPriority(int priority)
	{
		if (handle_ != 0) {
			::SetThreadPriority(handle_, priority);
		}
	}

	long int Thread::Self()
	{
		return ::GetCurrentThreadId();
	}

	[[noreturn]] void Thread::Exit(void* retVal)
	{
		_endthreadex(0);
		*static_cast<unsigned int*>(retVal) = 0;
	}

	void Thread::YieldExecution()
	{
		Sleep(0);
	}

	void Thread::Abort()
	{
#if !defined(DEATH_TARGET_WINDOWS_RT)
		// TerminateThread() is not supported on WinRT
		::TerminateThread(handle_, 0);
#endif
		handle_ = 0;
	}

	ThreadAffinityMask Thread::GetAffinityMask() const
	{
		ThreadAffinityMask affinityMask;

		if (handle_ != 0) {
			// A neutral value for the temporary mask
			DWORD_PTR allCpus = ~(allCpus & 0);

			affinityMask.affinityMask_ = ::SetThreadAffinityMask(handle_, allCpus);
			::SetThreadAffinityMask(handle_, affinityMask.affinityMask_);
		} else {
			LOGW("Cannot get the affinity for a thread that has not been created yet");
		}

		return affinityMask;
	}

	void Thread::SetAffinityMask(ThreadAffinityMask affinityMask)
	{
		if (handle_ != 0)
			::SetThreadAffinityMask(handle_, affinityMask.affinityMask_);
		else {
			LOGW("Cannot set the affinity mask for a not yet created thread");
		}
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	unsigned int Thread::WrapperFunction(void* arg)
	{
		ThreadInfo* threadInfo = static_cast<ThreadInfo*>(arg);
		threadInfo->startFunction(threadInfo->threadArg);

		return 0;
	}

}
#endif

#endif