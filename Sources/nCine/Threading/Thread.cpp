#include "Thread.h"
#include "../../Main.h"

#include <atomic>
#include <cstring>

#if defined(DEATH_TARGET_WINDOWS)
#	include <intrin.h>
#	include <process.h>
#	include <processthreadsapi.h>
#	include <synchapi.h>

#	include <Utf8.h>
#else
#	include <pthread.h>
#	include <signal.h>
#	include <sched.h>		// for sched_yield()
#	include <unistd.h>		// for sysconf()
#	include <utility>
#endif

#if defined(DEATH_TARGET_APPLE)
#	include <mach/thread_act.h>
#	include <mach/thread_policy.h>
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/emscripten.h>
#endif

#if defined(__FreeBSD__) || defined(__DragonFly__)
#	include <pthread_np.h>
#endif

#if defined(DEATH_TARGET_SWITCH)
#	include <switch.h>
#elif defined(DEATH_TARGET_VITA)
#	include <vitasdk.h>
#endif

#if defined(WITH_TRACY)
#	include "common/TracySystem.hpp"
#endif

namespace nCine
{
#if !defined(DEATH_TARGET_MSVC)
	DEATH_ALWAYS_INLINE void InterlockedOperationBarrier() noexcept
	{
#	if defined(DEATH_TARGET_ARM) || defined(DEATH_TARGET_RISCV)
		__sync_synchronize();
#	endif
	}
#endif

	template<typename T>
	DEATH_ALWAYS_INLINE T ExchangePointer(T volatile* destination, T value) noexcept
	{
#if defined(DEATH_TARGET_MSVC)
#	if !defined(DEATH_TARGET_32BIT)
		return (T)_InterlockedExchangePointer((void* volatile*)destination, value);
#	else
		return (T)_InterlockedExchange((long volatile*)(void* volatile*)destination, (long)(void*)value);
#	endif
#else
		T result = (T)__atomic_exchange_n((void* volatile*)destination, value, __ATOMIC_ACQ_REL);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE T ExchangePointer(T volatile* destination, std::nullptr_t value) noexcept
	{
#if defined(DEATH_TARGET_MSVC)
#	if !defined(DEATH_TARGET_32BIT)
		return (T)_InterlockedExchangePointer((void* volatile*)destination, value);
#	else
		return (T)_InterlockedExchange((long volatile*)(void* volatile*)destination, (long)(void*)value);
#	endif
#else
		T result = (T)__atomic_exchange_n((void* volatile*)destination, value, __ATOMIC_ACQ_REL);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	void Thread::Sleep(std::uint32_t milliseconds) noexcept
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		emscripten_sleep(milliseconds);
#elif defined(DEATH_TARGET_SWITCH)
		std::int64_t nanoseconds = static_cast<std::int64_t>(milliseconds) * 1000000;
		svcSleepThread(nanoseconds);
#elif defined(DEATH_TARGET_VITA)
		std::uint32_t microseconds = static_cast<std::uint32_t>(milliseconds) * 1000;
		sceKernelDelayThread(microseconds);
#elif defined(DEATH_TARGET_WINDOWS)
		::SleepEx(static_cast<DWORD>(milliseconds), FALSE);
#else
		std::uint32_t microseconds = static_cast<std::uint32_t>(milliseconds) * 1000;
		::usleep(microseconds);
#endif
	}

#if defined(WITH_THREADS)

	namespace
	{
#if defined(DEATH_TARGET_WINDOWS) && !defined(PTW32_VERSION) && !defined(__WINPTHREADS_VERSION)
		constexpr std::uint32_t MaxThreadNameLength = 256;

		void SetThreadName(HANDLE handle, const char* name)
		{
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
#else
		const std::uint32_t MaxThreadNameLength = 16;
#endif
	}

#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH)

	void ThreadAffinityMask::Zero()
	{
#	if defined(DEATH_TARGET_WINDOWS)
		affinityMask_ = 0LL;
#	elif defined(DEATH_TARGET_APPLE)
		affinityTag_ = THREAD_AFFINITY_TAG_NULL;
#	else
		CPU_ZERO(&cpuSet_);
#	endif
	}

	void ThreadAffinityMask::Set(std::int32_t cpuNum)
	{
#	if defined(DEATH_TARGET_WINDOWS)
		affinityMask_ |= 1LL << cpuNum;
#	elif defined(DEATH_TARGET_APPLE)
		affinityTag_ |= 1 << cpuNum;
#	else
		CPU_SET(cpuNum, &cpuSet_);
#	endif
	}

	void ThreadAffinityMask::Clear(std::int32_t cpuNum)
	{
#	if defined(DEATH_TARGET_WINDOWS)
		affinityMask_ &= ~(1LL << cpuNum);
#	elif defined(DEATH_TARGET_APPLE)
		affinityTag_ &= ~(1 << cpuNum);
#	else
		CPU_CLR(cpuNum, &cpuSet_);
#	endif
	}

	bool ThreadAffinityMask::IsSet(std::int32_t cpuNum)
	{
#	if defined(DEATH_TARGET_WINDOWS)
		return ((affinityMask_ >> cpuNum) & 1LL) != 0;
#	elif defined(DEATH_TARGET_APPLE)
		return ((affinityTag_ >> cpuNum) & 1) != 0;
#	else
		return CPU_ISSET(cpuNum, &cpuSet_) != 0;
#	endif
	}

#endif

	struct Thread::SharedBlock
	{
		std::atomic_int32_t _refCount;
#if defined(DEATH_TARGET_WINDOWS)
		HANDLE _handle;
#else
		pthread_t _handle;
#endif
		ThreadFuncDelegate _threadFunc;
		void* _threadArg;
	};

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

	Thread::Thread(const Thread& other)
	{
		_sharedBlock = other._sharedBlock;

		if (_sharedBlock != nullptr) {
			++_sharedBlock->_refCount;
		}
	}

	Thread& Thread::operator=(const Thread& other)
	{
		Detach();

		_sharedBlock = other._sharedBlock;

		if (_sharedBlock != nullptr) {
			++_sharedBlock->_refCount;
		}

		return *this;
	}

	Thread::Thread(Thread&& other) noexcept
	{
		_sharedBlock = other._sharedBlock;
		other._sharedBlock = nullptr;
	}

	Thread& Thread::operator=(Thread&& other) noexcept
	{
		Detach();

		_sharedBlock = other._sharedBlock;
		other._sharedBlock = nullptr;

		return *this;
	}

	Thread::operator bool() const noexcept
	{
		if (_sharedBlock != nullptr) {
			return false;
		}

#if defined(DEATH_TARGET_WINDOWS)
		DWORD exitCode = 0;
		::GetExitCodeThread(_sharedBlock->_handle, &exitCode);
		return (exitCode == STILL_ACTIVE);
#else
		pthread_t handle = _sharedBlock->_handle;
		return (handle != 0 && pthread_kill(handle, 0) == 0);
#endif
	}

	std::uint32_t Thread::GetProcessorCount() noexcept
	{
#if defined(DEATH_TARGET_WINDOWS)
		SYSTEM_INFO si;
		::GetSystemInfo(&si);
		return si.dwNumberOfProcessors;
#elif defined(DEATH_TARGET_SWITCH)
		return svcGetCurrentProcessorNumber();
#else
		long int confRet = -1;
#	if defined(_SC_NPROCESSORS_ONLN)
		confRet = sysconf(_SC_NPROCESSORS_ONLN);
#	elif defined(_SC_NPROC_ONLN)
		confRet = sysconf(_SC_NPROC_ONLN);
#	endif
		return (confRet > 0 ? static_cast<std::uint32_t>(confRet) : 0);
#endif
	}

	bool Thread::Run(ThreadFuncDelegate threadFunc, void* threadArg)
	{
		if (_sharedBlock != nullptr) {
			LOGW("Thread {} is already running", std::size_t(_sharedBlock->_handle));
			return false;
		}

		_sharedBlock = new SharedBlock();
		_sharedBlock->_refCount = 2;	// Ref. count is decreased in Process()
		_sharedBlock->_threadFunc = threadFunc;
		_sharedBlock->_threadArg = threadArg;
#if defined(DEATH_TARGET_WINDOWS)
		_sharedBlock->_handle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, Thread::Process, _sharedBlock, 0, nullptr));
		if (_sharedBlock->_handle == NULL) {
			DWORD error = ::GetLastError();
			delete _sharedBlock;
			_sharedBlock = nullptr;
			LOGF("_beginthreadex() failed with error 0x{:.8x}", error);
			return false;
		}
#else
#	if defined(DEATH_TARGET_VITA)
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, 256 * 1024);
		const int error = pthread_create(&_sharedBlock->_handle, &attr, Thread::Process, _sharedBlock);
		pthread_attr_destroy(&attr);
#	else
		const int error = pthread_create(&_sharedBlock->_handle, nullptr, Thread::Process, _sharedBlock);
#	endif
		if (error != 0) {
			delete _sharedBlock;
			_sharedBlock = nullptr;
			LOGF("pthread_create() failed with error {}", error);
			return false;
		}
#endif

		return true;
	}

	bool Thread::Join() noexcept
	{
		bool result = false;

		auto* sharedBlock = _sharedBlock;
		if (sharedBlock != nullptr) {
			++sharedBlock->_refCount;

#if defined(DEATH_TARGET_WINDOWS)
			result = ::WaitForSingleObject(sharedBlock->_handle, INFINITE) == WAIT_OBJECT_0;
			if (--sharedBlock->_refCount == 0) {
				::CloseHandle(sharedBlock->_handle);
				delete sharedBlock;
			}
#else
			result = (_sharedBlock->_handle != 0 && pthread_join(_sharedBlock->_handle, nullptr) == 0);
			if (result) {
				sharedBlock->_handle = 0;
			}
			if (--sharedBlock->_refCount == 0) {
				if (sharedBlock->_handle != 0) {
					pthread_detach(sharedBlock->_handle);
				}
				delete sharedBlock;
			}
#endif
		}

		return result;
	}
	
	void Thread::Detach() noexcept
	{
		auto* sharedBlock = ExchangePointer(&_sharedBlock, nullptr);
		if (sharedBlock == nullptr) {
			return;
		}

		// This returns the value before decrementing
		if (--sharedBlock->_refCount == 0) {
#if defined(DEATH_TARGET_WINDOWS)
			::CloseHandle(sharedBlock->_handle);
#else
			if (sharedBlock->_handle != 0) {
				pthread_detach(sharedBlock->_handle);
			}
#endif
			delete sharedBlock;
		}
	}

	void Thread::SetName(const char* name)
	{
		if (_sharedBlock == nullptr || _sharedBlock->_handle == 0) {
			return;
		}

#if defined(DEATH_TARGET_WINDOWS)
		SetThreadName(_sharedBlock->_handle, name);
#elif !defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_VITA)
		const auto nameLength = strnlen(name, MaxThreadNameLength);
		if (nameLength <= MaxThreadNameLength - 1) {
			pthread_setname_np(_sharedBlock->_handle, name);
		} else {
			char buffer[MaxThreadNameLength];
			memcpy(buffer, name, MaxThreadNameLength - 1);
			buffer[MaxThreadNameLength - 1] = '\0';
			pthread_setname_np(_sharedBlock->_handle, name);
		}
#endif
	}

	void Thread::SetCurrentName(const char* name)
	{
#if defined(WITH_TRACY)
		tracy::SetThreadName(name);
#elif defined(DEATH_TARGET_WINDOWS)
		SetThreadName(reinterpret_cast<HANDLE>(-1), name);
#elif !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_VITA)
		const auto nameLength = strnlen(name, MaxThreadNameLength);
		if (nameLength <= MaxThreadNameLength - 1) {
#	if !defined(DEATH_TARGET_APPLE)
			pthread_setname_np(pthread_self(), name);
#	else
			pthread_setname_np(name);
#	endif
		} else {
			char buffer[MaxThreadNameLength];
			memcpy(buffer, name, MaxThreadNameLength - 1);
			buffer[MaxThreadNameLength - 1] = '\0';
#	if !defined(DEATH_TARGET_APPLE)
			pthread_setname_np(pthread_self(), name);
#	else
			pthread_setname_np(name);
#	endif
		}
#endif
	}

#if !defined(DEATH_TARGET_SWITCH)
	std::int32_t Thread::GetPriority() const
	{
		if (_sharedBlock == nullptr || _sharedBlock->_handle == 0) {
			return 0;
		}

#	if defined(DEATH_TARGET_WINDOWS)
		return ::GetThreadPriority(_sharedBlock->_handle);
#	else
		int policy;
		struct sched_param param;
		pthread_getschedparam(_sharedBlock->_handle, &policy, &param);
		return param.sched_priority;
#	endif
	}

	void Thread::SetPriority(std::int32_t priority)
	{
		if (_sharedBlock == nullptr || _sharedBlock->_handle == 0) {
			return;
		}

#	if defined(DEATH_TARGET_WINDOWS)
		::SetThreadPriority(_sharedBlock->_handle, priority);
#	else
		int policy;
		struct sched_param param;
		pthread_getschedparam(_sharedBlock->_handle, &policy, &param);

		param.sched_priority = priority;
		pthread_setschedparam(_sharedBlock->_handle, policy, &param);
#	endif
	}
#endif

	std::uintptr_t Thread::GetCurrentId() noexcept
	{
#if defined(DEATH_TARGET_WINDOWS)
		return static_cast<std::uintptr_t>(::GetCurrentThreadId());
#elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_SWITCH) || defined(__FreeBSD__) || defined(__DragonFly__)
		return reinterpret_cast<std::uintptr_t>(pthread_self());
#else
		return static_cast<std::uintptr_t>(pthread_self());
#endif
	}

	static inline std::uintptr_t GetCurrentStackPointer() noexcept
	{
#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG)
		return reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
#elif defined(DEATH_TARGET_MSVC) && defined(_M_X64)
		return reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
#elif defined(DEATH_TARGET_MSVC) && defined(_M_IX86)
		__asm { mov eax, esp }
#else
		// Portable fallback - stack variable address
		volatile char probe = 0;
		return reinterpret_cast<std::uintptr_t>(&probe);
#endif
	}

	void Thread::GetCurrentStackInfo(std::size_t& stackSize, std::size_t& stackRemaining) noexcept
	{
		const std::uintptr_t sp = GetCurrentStackPointer();
		stackSize = 0; stackRemaining = 0;

#if defined(DEATH_TARGET_WINDOWS)
		MEMORY_BASIC_INFORMATION mbi{};
		if (!::VirtualQuery(reinterpret_cast<LPCVOID>(sp), &mbi, sizeof(mbi))) {
			return;
		}

		// Scan upward from AllocationBase to find the top of this reservation
		// and accumulate guard page bytes along the way
		std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mbi.AllocationBase);
		std::uintptr_t cursor = base, guardBytes = 0, top = 0;
		while (::VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi))) {
			if (reinterpret_cast<std::uintptr_t>(mbi.AllocationBase) != base) {
				break;
			}
			if (mbi.Protect & PAGE_GUARD) {
				guardBytes += mbi.RegionSize;
			}
			top = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
			cursor = top;
			if (mbi.RegionSize == 0) {
				break;
			}
		}

		if (top > base) {
			const std::size_t reserved = static_cast<std::size_t>(top - base);
			const std::size_t used = static_cast<std::size_t>(top - sp);
			stackSize = reserved;
			if (reserved > used + guardBytes) {
				stackRemaining = reserved - used - guardBytes;
			}
		}
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		// Emscripten provides __stack_high and __stack_low as linker-generated symbols (available since 3.1.x
		// with -sSTACK_SIZE). Stack grows DOWN from __stack_high to __stack_low.
#	if defined(__has_builtin) && __has_builtin(__builtin_wasm_memory_size)
		extern "C" {
			extern char __stack_high;
			extern char __stack_low;
		}
		const auto high = reinterpret_cast<std::uintptr_t>(&__stack_high);
		const auto low = reinterpret_cast<std::uintptr_t>(&__stack_low);
		if (high > low) {
			stackSize = static_cast<std::size_t>(high - low);
			if (sp >= low && sp <= high) {
				stackRemaining = static_cast<std::size_t>(sp - low);
			}
		}
#	endif
#elif defined(DEATH_TARGET_SWITCH)
		const Thread* t = threadGetSelf();
		if (!t || !t->stack_mem || t->stack_sz == 0) {
			return;
		}

		stackSize = t->stack_sz;
		const std::uintptr_t sp = GetCurrentStackPointer();
		const std::uintptr_t low = reinterpret_cast<std::uintptr_t>(t->stack_mem);
		if (sp >= low && sp <= low + t->stack_sz) {
			stackRemaining = static_cast<std::size_t>(sp - low);
		}
#elif defined(DEATH_TARGET_VITA)
		SceUID thid = sceKernelGetThreadId();
		if (thid >= 0) {
			SceKernelThreadInfo tinfo {};
			tinfo.size = sizeof(SceKernelThreadInfo);
			if (sceKernelGetThreadInfo(thid, &tinfo) == 0 && tinfo.stackSize > 0) {
				stackSize = static_cast<std::size_t>(tinfo.stackSize);
				const std::uintptr_t sp = GetCurrentStackPointer();
				const std::uintptr_t low = reinterpret_cast<std::uintptr_t>(tinfo.stack);
				if (sp > low && sp <= low + tinfo.stackSize) {
					stackRemaining = static_cast<std::size_t>(sp - low);
				}
			}
		}
#else
		pthread_t self = pthread_self();
		pthread_attr_t attr;

#	if defined(DEATH_TARGET_APPLE)
		void* stackAddr = pthread_get_stackaddr_np(self);
		stackSize = pthread_get_stacksize_np(self);

		// stackAddr is the HIGH end (base) on macOS (stack grows down)
		const auto base = reinterpret_cast<std::uintptr_t>(stackAddr);
		if (sp < base) {
			stackRemaining = static_cast<std::size_t>(sp - (base - stackSize));
		}
#	else
		if (pthread_getattr_np(self, &attr) == 0) {
			void* stackAddr = nullptr;
			std::size_t stackSize2 = 0;
			if (pthread_attr_getstack(&attr, &stackAddr, &stackSize2) == 0) {
				std::size_t guard = 0;
				pthread_attr_getguardsize(&attr, &guard);

				stackSize = stackSize2;
				const auto low = reinterpret_cast<std::uintptr_t>(stackAddr);
				if (sp >= low && sp <= low + stackSize2) {
					stackRemaining = static_cast<std::size_t>((sp - low) > guard ? (sp - low) - guard : 0u);
				}
			}
			pthread_attr_destroy(&attr);
		}
#	endif
#endif
	}

	[[noreturn]] void Thread::Exit() noexcept
	{
#if defined(DEATH_TARGET_WINDOWS)
		_endthreadex(0);
#else
		pthread_exit(nullptr);
#endif
	}

	void Thread::YieldExecution() noexcept
	{
#if defined(DEATH_TARGET_WINDOWS)
		::Sleep(0);
#else
		sched_yield();
#endif
	}

	bool Thread::Abort()
	{
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_WINDOWS_RT)
		// TerminateThread() is not supported on WinRT and Android doesn't have any similar function
		return false;
#elif defined(DEATH_TARGET_WINDOWS)
		return (_sharedBlock != nullptr && ::TerminateThread(_sharedBlock->_handle, 1));
#else
		return (_sharedBlock != nullptr && _sharedBlock->_handle != 0 && pthread_cancel(_sharedBlock->_handle) == 0);
#endif
	}

#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH)
	ThreadAffinityMask Thread::GetAffinityMask() const
	{
		ThreadAffinityMask affinityMask;

		if (_sharedBlock == nullptr || _sharedBlock->_handle == 0) {
			LOGW("Can't get the affinity for a thread that has not been created yet");
			return affinityMask;
		}

#	if defined(DEATH_TARGET_WINDOWS)
		affinityMask.affinityMask_ = ::SetThreadAffinityMask(_sharedBlock->_handle, ~0);
		::SetThreadAffinityMask(_sharedBlock->_handle, affinityMask.affinityMask_);
#	elif defined(DEATH_TARGET_APPLE)
		thread_affinity_policy_data_t threadAffinityPolicy;
		thread_port_t threadPort = pthread_mach_thread_np(_sharedBlock->_handle);
		mach_msg_type_number_t policyCount = THREAD_AFFINITY_POLICY_COUNT;
		boolean_t getDefault = FALSE;
		thread_policy_get(threadPort, THREAD_AFFINITY_POLICY, reinterpret_cast<thread_policy_t>(&threadAffinityPolicy), &policyCount, &getDefault);
		affinityMask.affinityTag_ = threadAffinityPolicy.affinity_tag;
#	else
		pthread_getaffinity_np(_sharedBlock->_handle, sizeof(cpu_set_t), &affinityMask.cpuSet_);
#	endif

		return affinityMask;
	}

	void Thread::SetAffinityMask(ThreadAffinityMask affinityMask)
	{
		if (_sharedBlock == nullptr || _sharedBlock->_handle == 0) {
			LOGW("Can't set the affinity mask for a not yet created thread");
			return;
		}

#	if defined(DEATH_TARGET_WINDOWS)
		::SetThreadAffinityMask(_sharedBlock->_handle, affinityMask.affinityMask_);
#	elif defined(DEATH_TARGET_APPLE)
		thread_affinity_policy_data_t threadAffinityPolicy = { affinityMask.affinityTag_ };
		thread_port_t threadPort = pthread_mach_thread_np(_sharedBlock->_handle);
		thread_policy_set(threadPort, THREAD_AFFINITY_POLICY, reinterpret_cast<thread_policy_t>(&threadAffinityPolicy), THREAD_AFFINITY_POLICY_COUNT);
#	else
		pthread_setaffinity_np(_sharedBlock->_handle, sizeof(cpu_set_t), &affinityMask.cpuSet_);
#	endif
	}
#endif

#if defined(DEATH_TARGET_WINDOWS)
	unsigned int Thread::Process(void* arg)
#else
	void* Thread::Process(void* arg)
#endif
	{
		Thread t(static_cast<SharedBlock*>(arg));
		auto threadFunc = t._sharedBlock->_threadFunc;
		auto threadArg = t._sharedBlock->_threadArg;
		t.Detach();

		threadFunc(threadArg);

#if defined(DEATH_TARGET_WINDOWS)
#	if defined(DEATH_TARGET_MSVC)
		_Cnd_do_broadcast_at_thread_exit();
#	endif
		return 0;
#else
		return nullptr;
#endif
	}

#endif
}
