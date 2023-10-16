#if defined(WITH_THREADS)

#include "Thread.h"

#if defined(DEATH_TARGET_WINDOWS)

#include "../../Common.h"

#include <utility>

namespace nCine
{
	namespace
	{
#if !defined(PTW32_VERSION) && !defined(__WINPTHREADS_VERSION)
		constexpr unsigned int MaxThreadNameLength = 256;

		void SetThreadName(HANDLE handle, const char* name)
		{
			if (handle == NULL) {
				return;
			}

// Don't use SetThreadDescription() yet, because the function was introduced in Windows 10, version 1607
//#	if defined(NTDDI_WIN10_RS2) && NTDDI_VERSION >= NTDDI_WIN10_RS2
#if 0
			wchar_t buffer[MaxThreadNameLength];
			size_t charsConverted;
			mbstowcs_s(&charsConverted, buffer, name, MaxThreadNameLength);
			const HANDLE threadHandle = (handle != reinterpret_cast<HANDLE>(-1)) ? handle : ::GetCurrentThread();
			::SetThreadDescription(threadHandle, buffer);
#	elif !defined(DEATH_TARGET_MINGW)
			constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

#		pragma pack(push, 8)
			struct THREADNAME_INFO {
				DWORD dwType;
				LPCSTR szName;
				DWORD dwThreadID;
				DWORD dwFlags;
			};
#		pragma pack(pop)

			const DWORD threadId = (handle == reinterpret_cast<HANDLE>(-1) ? ::GetCurrentThreadId() : ::GetThreadId(handle));
			THREADNAME_INFO info;
			info.dwType = 0x1000;
			info.szName = name;
			info.dwThreadID = threadId;
			info.dwFlags = 0;

			__try {
				::RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), reinterpret_cast<ULONG_PTR*>(&info));
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
#	endif
		}
#endif
	}

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

	Thread::Thread()
		: _sharedBlock(nullptr)
	{
	}

	Thread::Thread(ThreadFunctionPtr threadFunc, void* threadArg)
		: Thread()
	{
		Run(threadFunc, threadArg);
	}

	Thread::Thread(SharedBlock* sharedBlock)
		: _sharedBlock(sharedBlock)
	{
	}

	Thread::~Thread()
	{
		Detach();
	}

	unsigned int Thread::GetProcessorCount()
	{
		SYSTEM_INFO si;
		::GetSystemInfo(&si);
		return si.dwNumberOfProcessors;
	}

	void Thread::Run(ThreadFunctionPtr threadFunc, void* threadArg)
	{
		if (_sharedBlock != nullptr) {
			LOGW("Thread %u is already running", _sharedBlock->_handle);
			return;
		}

		_sharedBlock = new SharedBlock();
		_sharedBlock->_refCount = 2;	// Ref. count is decreased in WrapperFunction()
		_sharedBlock->_threadFunc = threadFunc;
		_sharedBlock->_threadArg = threadArg;
		_sharedBlock->_handle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, Thread::WrapperFunction, _sharedBlock, 0, nullptr));
		if (_sharedBlock->_handle == NULL) {
			DWORD error = ::GetLastError();
			delete _sharedBlock;
			_sharedBlock = nullptr;
			FATAL_MSG("_beginthreadex() failed with error 0x%08x", error);
		}
	}

	void* Thread::Join()
	{
		if (_sharedBlock != nullptr) {
			::WaitForSingleObject(_sharedBlock->_handle, INFINITE);
			//::CloseHandle(_sharedBlock->_handle);
			//_sharedBlock->_handle = NULL;
		}
		return nullptr;
	}

	void Thread::Detach()
	{
		if (_sharedBlock == nullptr) {
			return;
		}

		// This returns the value before decrementing
		int32_t refCount = _sharedBlock->_refCount.fetchSub(1);
		if (refCount == 1) {
			::CloseHandle(_sharedBlock->_handle);
			delete _sharedBlock;
		}

		_sharedBlock = nullptr;
	}

	void Thread::SetName(const char* name)
	{
		if (_sharedBlock != nullptr) {
			SetThreadName(_sharedBlock->_handle, name);
		}
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
		return (_sharedBlock != nullptr ? ::GetThreadPriority(_sharedBlock->_handle) : 0);
	}

	void Thread::SetPriority(int priority)
	{
		if (_sharedBlock != nullptr) {
			::SetThreadPriority(_sharedBlock->_handle, priority);
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
		::Sleep(0);
	}

	void Thread::Abort()
	{
#if !defined(DEATH_TARGET_WINDOWS_RT)
		if (_sharedBlock != nullptr) {
			// TerminateThread() is not supported on WinRT
			::TerminateThread(_sharedBlock->_handle, 0);
		}
#endif
	}

	ThreadAffinityMask Thread::GetAffinityMask() const
	{
		ThreadAffinityMask affinityMask;

		if (_sharedBlock != nullptr) {
			affinityMask.affinityMask_ = ::SetThreadAffinityMask(_sharedBlock->_handle, ~0);
			::SetThreadAffinityMask(_sharedBlock->_handle, affinityMask.affinityMask_);
		} else {
			LOGW("Cannot get the affinity for a thread that has not been created yet");
		}

		return affinityMask;
	}

	void Thread::SetAffinityMask(ThreadAffinityMask affinityMask)
	{
		if (_sharedBlock != nullptr)
			::SetThreadAffinityMask(_sharedBlock->_handle, affinityMask.affinityMask_);
		else {
			LOGW("Cannot set the affinity mask for a not yet created thread");
		}
	}

	unsigned int Thread::WrapperFunction(void* arg)
	{
		Thread t(static_cast<SharedBlock*>(arg));
		ThreadFunctionPtr threadFunc = t._sharedBlock->_threadFunc;
		void* threadArg = t._sharedBlock->_threadArg;
		t.Detach();

		threadFunc(threadArg);
		_endthreadex(0);
		return 0;
	}
}

#endif

#endif