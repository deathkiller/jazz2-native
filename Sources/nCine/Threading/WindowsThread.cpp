#if defined(WITH_THREADS)

#include "Thread.h"

#if defined(DEATH_TARGET_WINDOWS)

#include "../../Common.h"
#include "../tracy.h"

#include <utility>

#include <Utf8.h>

namespace nCine
{
	namespace
	{
#if !defined(PTW32_VERSION) && !defined(__WINPTHREADS_VERSION)
		constexpr std::uint32_t MaxThreadNameLength = 256;

		void SetThreadName(HANDLE handle, const char* name)
		{
			if (handle == NULL) {
				return;
			}

			using _SetThreadDescription = HRESULT(WINAPI*)(HANDLE hThread, PCWSTR lpThreadDescription);

			static auto setThreadDescription = reinterpret_cast<_SetThreadDescription>(::GetProcAddress(::GetModuleHandle(L"kernel32.dll"), "SetThreadDescription"));		
			if (setThreadDescription != nullptr) {
				wchar_t buffer[MaxThreadNameLength];
				Death::Utf8::ToUtf16(buffer, name);
				const HANDLE threadHandle = (handle != reinterpret_cast<HANDLE>(-1)) ? handle : ::GetCurrentThread();
				setThreadDescription(threadHandle, buffer);
				return;
			}

#	if !defined(DEATH_TARGET_MINGW)
			constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

#		pragma pack(push, 8)
			struct THREADNAME_INFO {
				DWORD dwType;
				LPCSTR szName;
				DWORD dwThreadID;
				DWORD dwFlags;
			};
#		pragma pack(pop)

			THREADNAME_INFO info;
			info.dwType = 0x1000;
			info.szName = name;
			info.dwThreadID = (handle == reinterpret_cast<HANDLE>(-1) ? ::GetCurrentThreadId() : ::GetThreadId(handle));
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

	void ThreadAffinityMask::Set(std::int32_t cpuNum)
	{
		affinityMask_ |= 1LL << cpuNum;
	}

	void ThreadAffinityMask::Clear(std::int32_t cpuNum)
	{
		affinityMask_ &= ~(1LL << cpuNum);
	}

	bool ThreadAffinityMask::IsSet(std::int32_t cpuNum)
	{
		return ((affinityMask_ >> cpuNum) & 1LL) != 0;
	}

	Thread::Thread()
		: _sharedBlock(nullptr)
	{
	}

	Thread::Thread(ThreadFuncDelegate threadFunc, void* threadArg)
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

	std::uint32_t Thread::GetProcessorCount()
	{
		SYSTEM_INFO si;
		::GetSystemInfo(&si);
		return si.dwNumberOfProcessors;
	}

	void Thread::Run(ThreadFuncDelegate threadFunc, void* threadArg)
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

	bool Thread::Join()
	{
		return (_sharedBlock != nullptr && ::WaitForSingleObject(_sharedBlock->_handle, INFINITE) == WAIT_OBJECT_0);
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

	void Thread::SetCurrentName(const char* name)
	{
#if defined(WITH_TRACY)
		tracy::SetThreadName(name);
#else
		SetThreadName(reinterpret_cast<HANDLE>(-1), name);
#endif
	}

	std::int32_t Thread::GetPriority() const
	{
		return (_sharedBlock != nullptr ? ::GetThreadPriority(_sharedBlock->_handle) : 0);
	}

	void Thread::SetPriority(std::int32_t priority)
	{
		if (_sharedBlock != nullptr) {
			::SetThreadPriority(_sharedBlock->_handle, priority);
		}
	}

	std::uintptr_t Thread::GetCurrentId()
	{
		return static_cast<std::uintptr_t>(::GetCurrentThreadId());
	}

	[[noreturn]] void Thread::Exit()
	{
		_endthreadex(0);
	}

	void Thread::YieldExecution()
	{
		::Sleep(0);
	}

	bool Thread::Abort()
	{
#if defined(DEATH_TARGET_WINDOWS_RT)
		// TerminateThread() is not supported on WinRT
		return false;
#else
		return (_sharedBlock != nullptr && ::TerminateThread(_sharedBlock->_handle, 1));
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
		auto threadFunc = t._sharedBlock->_threadFunc;
		auto threadArg = t._sharedBlock->_threadArg;
		t.Detach();

		threadFunc(threadArg);
		_endthreadex(0);
		return 0;
	}
}

#endif

#endif