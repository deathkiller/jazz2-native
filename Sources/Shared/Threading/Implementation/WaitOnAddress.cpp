#include "WaitOnAddress.h"
#include "../../Asserts.h"

namespace Death { namespace Threading { namespace Implementation {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

#if defined(DEATH_TARGET_WINDOWS)
	WaitOnAddressDelegate* _waitOnAddress = nullptr;
	WakeByAddressAllDelegate* _wakeByAddressAll = nullptr;
	WakeByAddressAllDelegate* _wakeByAddressSingle = nullptr;

	void InitializeWaitOnAddress()
	{
		if (_waitOnAddress == nullptr || _wakeByAddressAll == nullptr || _wakeByAddressSingle == nullptr) {
			HMODULE kernelBase = ::GetModuleHandle(L"KernelBase.dll");
			_waitOnAddress = GetProcAddressByFunctionDeclaration(kernelBase, WaitOnAddress);
			DEATH_DEBUG_ASSERT(_waitOnAddress != nullptr);
			_wakeByAddressAll = GetProcAddressByFunctionDeclaration(kernelBase, WakeByAddressAll);
			DEATH_DEBUG_ASSERT(_wakeByAddressAll != nullptr);
			_wakeByAddressSingle = GetProcAddressByFunctionDeclaration(kernelBase, WakeByAddressSingle);
			DEATH_DEBUG_ASSERT(_wakeByAddressSingle != nullptr);
		}
	}
#endif

}}}