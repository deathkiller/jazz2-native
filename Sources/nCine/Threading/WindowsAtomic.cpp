#include "Atomic.h"
#include "../../Common.h"

#include <CommonWindows.h>

namespace nCine
{
	int32_t Atomic32::load(MemoryModel memModel)
	{
#if defined(DEATH_TARGET_WINDOWS_RT)
		// ReadNoFence/ReadAcquire/ReadRelease is not supported on Windows RT
		return value_;
#else
		switch (memModel) {
			case MemoryModel::RELAXED:
				return ReadNoFence(reinterpret_cast<volatile long*>(&value_));
			case MemoryModel::ACQUIRE:
				return ReadAcquire(reinterpret_cast<volatile long*>(&value_));
			case MemoryModel::RELEASE:
				FATAL_MSG("Incompatible memory model");
				return 0;
			case MemoryModel::SEQ_CST:
			default:
				return ReadAcquire(reinterpret_cast<volatile long*>(&value_));
		}
#endif
	}

	void Atomic32::store(int32_t value, MemoryModel memModel)
	{
#if defined(DEATH_TARGET_WINDOWS_RT)
		// WriteNoFence/WriteAcquire/WriteRelease is not supported on Windows RT
		value_ = value;
#else
		switch (memModel) {
			case MemoryModel::RELAXED:
				WriteNoFence(reinterpret_cast<volatile long*>(&value_), value);
				break;
			case MemoryModel::ACQUIRE:
				FATAL_MSG("Incompatible memory model");
				break;
			case MemoryModel::RELEASE:
				WriteRelease(reinterpret_cast<volatile long*>(&value_), value);
				break;
			case MemoryModel::SEQ_CST:
			default:
				WriteRelease(reinterpret_cast<volatile long*>(&value_), value);
				break;
		}
#endif
	}

	int32_t Atomic32::fetchAdd(int32_t value, MemoryModel memModel)
	{
		switch (memModel) {
			case MemoryModel::RELAXED:
				return InterlockedExchangeAddNoFence(reinterpret_cast<volatile long*>(&value_), value);
			case MemoryModel::ACQUIRE:
				return InterlockedExchangeAddAcquire(reinterpret_cast<volatile long*>(&value_), value);
			case MemoryModel::RELEASE:
				return InterlockedExchangeAddRelease(reinterpret_cast<volatile long*>(&value_), value);
			case MemoryModel::SEQ_CST:
			default:
				return InterlockedExchangeAdd(reinterpret_cast<volatile long*>(&value_), value);
		}
	}

	int32_t Atomic32::fetchSub(int32_t value, MemoryModel memModel)
	{
		switch (memModel) {
			case MemoryModel::RELAXED:
				return InterlockedExchangeAddNoFence(reinterpret_cast<volatile long*>(&value_), -value);
			case MemoryModel::ACQUIRE:
				return InterlockedExchangeAddAcquire(reinterpret_cast<volatile long*>(&value_), -value);
			case MemoryModel::RELEASE:
				return InterlockedExchangeAddRelease(reinterpret_cast<volatile long*>(&value_), -value);
			case MemoryModel::SEQ_CST:
			default:
				return InterlockedExchangeAdd(reinterpret_cast<volatile long*>(&value_), -value);
		}
	}

	bool Atomic32::cmpExchange(int32_t newValue, int32_t cmpValue, MemoryModel memModel)
	{
		switch (memModel) {
			case MemoryModel::RELAXED:
				return InterlockedCompareExchangeNoFence(reinterpret_cast<volatile long*>(&value_), newValue, cmpValue) == cmpValue;
			case MemoryModel::ACQUIRE:
				return InterlockedCompareExchangeAcquire(reinterpret_cast<volatile long*>(&value_), newValue, cmpValue) == cmpValue;
			case MemoryModel::RELEASE:
				return InterlockedCompareExchangeRelease(reinterpret_cast<volatile long*>(&value_), newValue, cmpValue) == cmpValue;
			case MemoryModel::SEQ_CST:
			default:
				return InterlockedCompareExchange(reinterpret_cast<volatile long*>(&value_), newValue, cmpValue) == cmpValue;
		}
	}

	int64_t Atomic64::load(MemoryModel memModel)
	{
#if defined(DEATH_TARGET_WINDOWS_RT)
		// ReadNoFence/ReadAcquire/ReadRelease is not supported on Windows RT
		return value_;
#else
		switch (memModel) {
			case MemoryModel::RELAXED:
				return ReadNoFence64(reinterpret_cast<volatile LONG64*>(&value_));
			case MemoryModel::ACQUIRE:
				return ReadAcquire64(reinterpret_cast<volatile LONG64*>(&value_));
			case MemoryModel::RELEASE:
				FATAL_MSG("Incompatible memory model");
				return 0;
			case MemoryModel::SEQ_CST:
			default:
				return ReadAcquire64(reinterpret_cast<volatile LONG64*>(&value_));
		}
#endif
	}

	void Atomic64::store(int64_t value, MemoryModel memModel)
	{
#if defined(DEATH_TARGET_WINDOWS_RT)
		// WriteNoFence/WriteAcquire/WriteRelease is not supported on Windows RT
		value_ = value;
#else
		switch (memModel) {
			case MemoryModel::RELAXED:
				WriteNoFence64(reinterpret_cast<volatile LONG64*>(&value_), value);
				break;
			case MemoryModel::ACQUIRE:
				FATAL_MSG("Incompatible memory model");
				break;
			case MemoryModel::RELEASE:
				WriteRelease64(reinterpret_cast<volatile LONG64*>(&value_), value);
				break;
			case MemoryModel::SEQ_CST:
			default:
				WriteRelease64(reinterpret_cast<volatile LONG64*>(&value_), value);
				break;
		}
#endif
	}

	int64_t Atomic64::fetchAdd(int64_t value, MemoryModel memModel)
	{
		switch (memModel) {
			case MemoryModel::RELAXED:
				return InterlockedExchangeAddNoFence64(reinterpret_cast<volatile LONG64*>(&value_), value);
			case MemoryModel::ACQUIRE:
#if defined(DEATH_TARGET_X86) && defined(DEATH_TARGET_32BIT)
				FATAL_MSG("Incompatible memory model");
				return 0;
#else
				return InterlockedExchangeAddAcquire64(reinterpret_cast<volatile LONG64*>(&value_), value);
#endif
			case MemoryModel::RELEASE:
#if defined(DEATH_TARGET_X86) && defined(DEATH_TARGET_32BIT)
				FATAL_MSG("Incompatible memory model");
				return 0;
#else
				return InterlockedExchangeAddRelease64(reinterpret_cast<volatile LONG64*>(&value_), value);
#endif
			case MemoryModel::SEQ_CST:
			default:
				return InterlockedExchangeAdd64(reinterpret_cast<volatile LONG64*>(&value_), value);
		}
	}

	int64_t Atomic64::fetchSub(int64_t value, MemoryModel memModel)
	{
		switch (memModel) {
			case MemoryModel::RELAXED:
				return InterlockedExchangeAddNoFence64(reinterpret_cast<volatile LONG64*>(&value_), -value);
			case MemoryModel::ACQUIRE:
#if defined(DEATH_TARGET_X86) && defined(DEATH_TARGET_32BIT)
				FATAL_MSG("Incompatible memory model");
				return 0;
#else
				return InterlockedExchangeAddAcquire64(reinterpret_cast<volatile LONG64*>(&value_), -value);
#endif
			case MemoryModel::RELEASE:
#if defined(DEATH_TARGET_X86) && defined(DEATH_TARGET_32BIT)
				FATAL_MSG("Incompatible memory model");
				return 0;
#else
				return InterlockedExchangeAddRelease64(reinterpret_cast<volatile LONG64*>(&value_), -value);
#endif
			case MemoryModel::SEQ_CST:
			default:
				return InterlockedExchangeAdd64(reinterpret_cast<volatile LONG64*>(&value_), -value);
		}
	}

	bool Atomic64::cmpExchange(int64_t newValue, int64_t cmpValue, MemoryModel memModel)
	{
		switch (memModel) {
			case MemoryModel::RELAXED:
				return InterlockedCompareExchangeNoFence64(reinterpret_cast<volatile LONG64*>(&value_), newValue, cmpValue) == cmpValue;
			case MemoryModel::ACQUIRE:
				return InterlockedCompareExchangeAcquire64(reinterpret_cast<volatile LONG64*>(&value_), newValue, cmpValue) == cmpValue;
			case MemoryModel::RELEASE:
				return InterlockedCompareExchangeRelease64(reinterpret_cast<volatile LONG64*>(&value_), newValue, cmpValue) == cmpValue;
			case MemoryModel::SEQ_CST:
			default:
				return InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(&value_), newValue, cmpValue) == cmpValue;
		}
	}
}