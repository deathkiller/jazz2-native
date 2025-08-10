// Uses parts of Quill (https://github.com/odygrd/quill)
// Copyright © 2020-2024 Odysseas Georgoudis & contributors
// Copyright © 2024-2025 Dan R.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#include "ITraceSink.h"

#if defined(DEATH_TRACE)

#include "../CommonWindows.h"
#include "../Containers/Array.h"
#include "../Containers/Function.h"
#include "../Containers/SmallVector.h"
#include "../Containers/String.h"

#include <chrono>
#include <string>

#if defined(DEATH_TARGET_ANDROID) || defined(__linux__)
#	include <sys/syscall.h>
#	include <unistd.h>
#elif defined(__NetBSD__)
#	include <lwp.h>
#	include <unistd.h>
#elif defined(__FreeBSD__)
#	include <sys/thr.h>
#	include <unistd.h>
#elif defined(__DragonFly__)
#	include <sys/lwp.h>
#	include <unistd.h>
#elif !defined(DEATH_TARGET_WINDOWS)
#	include <pthread.h>
#	include <unistd.h>
#endif

#if defined(DEATH_TRACE_ASYNC)
#	include "../Base/Format.h"
#	include "../Containers/StaticArray.h"
#	include "../Containers/StringStl.h"
#	include "../Threading/Event.h"
#	include "../Threading/Spinlock.h"

#	include <atomic>
#	include <memory>
#	include <mutex>
#	include <thread>
#	include <limits>

	// BoundedSPSCQueue includes
#	if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_SWITCH)
#		include <malloc.h>
#	else
#		include <sys/mman.h>
#	endif

#	if defined(DEATH_TARGET_X86)
#		if defined(DEATH_TARGET_MSVC)
#			include <intrin.h>
#		else
#			if __has_include(<x86gprintrin.h>)
#				if defined(__GNUC__) && __GNUC__ > 10
#					include <emmintrin.h>
#					include <x86gprintrin.h>
#				elif defined(__clang_major__)
					// clang needs immintrin for _mm_clflushopt
#					include <immintrin.h>
#				endif
#			else
#				include <immintrin.h>
#				include <x86intrin.h>
#			endif
#		endif
#	endif
#endif

namespace Death { namespace Trace {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	namespace Implementation
	{
		DEATH_ALWAYS_INLINE std::uint32_t GetNativeThreadId() noexcept
		{
#	if defined(DEATH_TARGET_CYGWIN)
			return 0; // Not supported
#	elif defined(DEATH_TARGET_WINDOWS)
			return static_cast<std::uint32_t>(::GetCurrentThreadId());
#	elif defined(DEATH_TARGET_ANDROID)
			return static_cast<std::uint32_t>(::syscall(__NR_gettid));
#	elif defined(__linux__)
			return static_cast<std::uint32_t>(::syscall(SYS_gettid));
#	elif defined(DEATH_TARGET_APPLE)
			std::uint64_t tid64;
			pthread_threadid_np(nullptr, &tid64);
			return static_cast<std::uint32_t>(tid64);
#	elif defined(__NetBSD__)
			return static_cast<std::uint32_t>(_lwp_self());
#	elif defined(__FreeBSD__)
			long lwpid;
			thr_self(&lwpid);
			return static_cast<std::uint32_t>(lwpid);
#	elif defined(__DragonFly__)
			return static_cast<std::uint32_t>(lwp_gettid());
#	else
			return reinterpret_cast<std::uintptr_t>(pthread_self());
#	endif
		}
	}

#if defined(DEATH_TRACE_ASYNC)
	namespace Implementation
	{
		/** @brief Available queue types to be used inside logger */
		enum class QueueType
		{
			UnboundedBlocking,
			UnboundedDropping,
			BoundedBlocking,
			BoundedDropping
		};

		/** @brief Default type of underlying queue inside logger */
		static constexpr QueueType DefaultQueueType = QueueType::UnboundedBlocking;

		/** @brief Initial capacity of queue inside logger */
		static constexpr std::uint32_t InitialQueueCapacity = 128 * 1024;

		/** @brief Interval between retries for blocking queue types (in nanoseconds) */
		static constexpr std::uint32_t BlockingQueueRetryIntervalNanoseconds = 800;

		/** @brief Enables huge pages to be used for storage of underlying queue to reduce TBL misses, available only on Linux */
		static constexpr bool HugePagesEnabled = false;

		/** @brief Initial item capacity of transit event bufferper thread context, must be power of 2 */
		static constexpr std::uint32_t TransitEventBufferInitialCapacity = 128;

		/** @brief If enabled, the worker thread will process all remaining entries before exiting */
		static constexpr bool WaitForQueuesToEmptyBeforeExit = true;

		/** @brief Controls the frequency at which the backend recalculates and syncs the internal RdtscClock with the system time from the system wall clock */
		static constexpr std::chrono::milliseconds RdtscResyncInterval = std::chrono::milliseconds{500};

		/** @brief When the soft limit is reached, the worker thread will try to process a batch of cached transit events all at once */
		static constexpr std::size_t TransitEventsSoftLimit = 4096;

		/** @brief When hard limit is reached, the worker thread will stop reading the queues until there is space available in the buffer */
		static constexpr std::size_t TransitEventsHardLimit = 32768;

		/**
			@brief When this option is set to a non-zero value, the backend takes a timestamp (`now()`) before reading the queues.
		
			It uses that timestamp to ensure that each log message's timestamp from the frontend queues is less than
			or equal to the stored `now()` timestamp minus the specified grace period, guaranteeing ordering by timestamp.
			Messages that fail the above check remain in the lock-free queue and they are checked again in the next iteration.
			The timestamp check is performed with microsecond precision.
		*/
		static constexpr std::chrono::microseconds LogTimestampOrderingGracePeriod{250};

		/** @brief Special value for level to force immediate flushing of all buffers */
		static constexpr TraceLevel FlushRequested = TraceLevel(UINT8_MAX);
		/** @brief Special value for level to initialize backtrace storage */
		static constexpr TraceLevel InitializeBacktraceRequested = TraceLevel(UINT8_MAX - 1);
		/** @brief Special value for level to force immediate flushing of backtrace storage */
		static constexpr TraceLevel FlushBacktraceRequested = TraceLevel(UINT8_MAX - 2);

		static constexpr std::size_t CacheLineSize = 64u;
		static constexpr std::size_t CacheLineAligned = 2 * CacheLineSize;

		constexpr bool IsPowerOfTwo(std::uint64_t number) noexcept {
			return (number != 0) && ((number & (number - 1)) == 0);
		}

		template<typename T>
		constexpr T MaxPowerOfTwo() noexcept {
			return (std::numeric_limits<T>::max() >> 1) + 1;
		}

		template<typename T>
		T NextPowerOfTwo(T n) noexcept {
			constexpr T maxPowerOf2 = MaxPowerOfTwo<T>();

			if (n >= maxPowerOf2) {
				return maxPowerOf2;
			}

			if (IsPowerOfTwo(static_cast<std::uint64_t>(n))) {
				return n;
			}

			T result = 1;
			while (result < n) {
				result <<= 1;
			}

			DEATH_DEBUG_ASSERT(IsPowerOfTwo(static_cast<std::uint64_t>(result)));

			return result;
		}

		/** @brief Returns value of timestamp counter on current thread (if supported) */
#	if defined(__aarch64__)
		DEATH_ALWAYS_INLINE std::uint64_t rdtsc() noexcept {
			// System timer of ARMv8 runs at a different frequency than the CPU's.
			// The frequency is fixed, typically in the range 1-50MHz.  It can be
			// read at CNTFRQ special register.  We assume the OS has set up the virtual timer properly.
			std::int64_t virtualTimerValue;
			__asm__ volatile("mrs %0, cntvct_el0" : "=r"(virtualTimerValue));
			return static_cast<uint64_t>(virtualTimerValue);
		}
#	elif (defined(__ARM_ARCH) && !defined(DEATH_TARGET_MSVC))
		DEATH_ALWAYS_INLINE std::uint64_t rdtsc() noexcept {
#		if (__ARM_ARCH >= 6)
			// V6 is the earliest arch that has a standard cyclecount
			std::uint32_t pmccntr;
			std::uint32_t pmuseren;
			std::uint32_t pmcntenset;

			__asm__ volatile("mrc p15, 0, %0, c9, c14, 0" : "=r"(pmuseren));
			if (pmuseren & 1) {
				__asm__ volatile("mrc p15, 0, %0, c9, c12, 1" : "=r"(pmcntenset));
				if (pmcntenset & 0x80000000ul) {
					__asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(pmccntr));
					return (static_cast<uint64_t>(pmccntr)) * 64u;
				}
			}
#		endif

			return static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
		}
#	elif defined(__riscv)
		DEATH_ALWAYS_INLINE std::uint64_t rdtsc() noexcept {
			std::uint64_t tsc;
			__asm__ volatile("rdtime %0" : "=r"(tsc));
			return tsc;
		}
#	elif defined(__loongarch64)
		DEATH_ALWAYS_INLINE std::uint64_t rdtsc() noexcept {
			std::uint64_t tsc;
			__asm__ volatile("rdtime.d %0,$r0" : "=r" (tsc));
			return tsc;
		}
#	elif defined(__s390x__)
		DEATH_ALWAYS_INLINE std::uint64_t rdtsc() noexcept {
			std::uint64_t tsc;
			__asm__ volatile("stck %0" : "=Q" (tsc) : : "cc");
			return tsc;
		}
#	elif (defined(_M_ARM) || defined(_M_ARM64) || defined(__PPC64__))
		DEATH_ALWAYS_INLINE std::uint64_t rdtsc() noexcept {
			return static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
		}
#	else
		DEATH_ALWAYS_INLINE std::uint64_t rdtsc() noexcept {
			return __rdtsc();
		}
#	endif

		/** @brief Allows to convert timestamp counter values to Unix nanoseconds */
		class RdtscClock
		{
		private:
			class RdtscTicks
			{
			public:
				static RdtscTicks& instance();

				double nanosecondsPerTick() const noexcept {
					return _nanosecondsPerTick;
				}

			private:
				RdtscTicks();

				double _nanosecondsPerTick;
			};

		public:
			explicit RdtscClock(std::chrono::nanoseconds resyncInterval);

			std::uint64_t timeSinceEpoch(std::uint64_t rdtscValue) const noexcept;
			std::uint64_t timeSinceEpochSafe(std::uint64_t rdtscValue) const noexcept;

			bool resync(std::uint32_t lag) const noexcept;

			double nanosecondsPerTick() const noexcept {
				return _nanosecondsPerTick;
			}

		private:
			struct BaseTimeTsc
			{
				BaseTimeTsc()
					: BaseTime(0), BaseTsc(0) {}

				/** @brief Initial base time in nanoseconds since epoch */
				std::int64_t BaseTime;
				/** @brief Initial base tsc time */
				std::uint64_t BaseTsc;
			};

			mutable std::int64_t _resyncIntervalTicks;
			std::int64_t _resyncIntervalOriginal;
			double _nanosecondsPerTick;

			alignas(CacheLineAligned) mutable std::atomic<std::uint32_t> _version;
			mutable Containers::StaticArray<2, BaseTimeTsc> _base;

			static inline std::uint64_t fastAverage(std::uint64_t x, std::uint64_t y) noexcept {
				return (x & y) + ((x ^ y) >> 1);
			}
		};

		/**
		 * @brief Bounded single-producer single-consumer FIFO queue (ring buffer)
		 */
		template<typename T>
		class BoundedSPSCQueueImpl
		{
		public:
			explicit BoundedSPSCQueueImpl(T capacity, bool hugesPagesEnabled = false, T readerStorePercent = 5)
				: _capacity(NextPowerOfTwo(capacity)), _capacityMask(_capacity - 1),
					_bytesPerBatch(static_cast<T>(_capacity * static_cast<double>(readerStorePercent) / 100.0)),
					_storage(static_cast<std::uint8_t*>(allocAligned(2ULL * static_cast<std::uint64_t>(_capacity), CacheLineAligned, hugesPagesEnabled))),
					_hugePagesEnabled(hugesPagesEnabled)
			{
				std::memset(_storage, 0, 2ULL * static_cast<std::uint64_t>(_capacity));

				_atomicWriterPos.store(0);
				_atomicReaderPos.store(0);

#	if defined(DEATH_TARGET_X86) && defined(DEATH_TARGET_CLFLUSHOPT) && !defined(DEATH_TARGET_CLANG_CL)
				// Remove log memory from cache
				for (std::uint64_t i = 0; i < (2ULL * static_cast<std::uint64_t>(_capacity)); i += CacheLineSize) {
					_mm_clflush(_storage + i);
				}

				DEATH_DEBUG_ASSERT(_capacity >= 1024);

				std::uint64_t cacheLines = (_capacity >= 2048 ? 32 : 16);

				for (std::uint64_t i = 0; i < cacheLines; ++i) {
					_mm_prefetch(reinterpret_cast<char const*>(_storage + (CacheLineSize * i)), _MM_HINT_T0);
				}
#	endif
			}

			~BoundedSPSCQueueImpl() noexcept {
				freeAligned(_storage);
			}

			BoundedSPSCQueueImpl(BoundedSPSCQueueImpl const&) = delete;
			BoundedSPSCQueueImpl& operator=(BoundedSPSCQueueImpl const&) = delete;

			std::uint8_t* prepareWrite(T n) noexcept {
				if ((_capacity - static_cast<T>(_writerPos - _cachedReaderPos)) < n) {
					// Not enough space, we need to load reader and re-check
					_cachedReaderPos = _atomicReaderPos.load(std::memory_order_acquire);

					if ((_capacity - static_cast<T>(_writerPos - _cachedReaderPos)) < n) {
						return nullptr;
					}
				}

				return &_storage[_writerPos & _capacityMask];
			}

			void finishWrite(T nbytes) noexcept {
				_writerPos += nbytes;
			}

			void commitWrite() noexcept {
				// Set the atomic flag, so the reader can see write
				_atomicWriterPos.store(_writerPos, std::memory_order_release);

#	if defined(DEATH_TARGET_X86) && defined(DEATH_TARGET_CLFLUSHOPT) && !defined(DEATH_TARGET_CLANG_CL)
				// Flush writen cache lines
				flushCacheLines(_lastFlushedWriterPos, _writerPos);

				// Prefetch a future cache line
				_mm_prefetch(reinterpret_cast<char const*>(_storage + (_writerPos & _capacityMask) + (CacheLineSize * 10)), _MM_HINT_T0);
#	endif
			}

			void finishAndCommitWrite(T nbytes) noexcept {
				finishWrite(nbytes);
				commitWrite();
			}

			const std::uint8_t* prepareRead() noexcept {
				if (empty()) {
					return nullptr;
				}

				return &_storage[_readerPos & _capacityMask];
			}

			void finishRead(T nbytes) noexcept {
				_readerPos += nbytes;
			}

			void commitRead() noexcept {
				if (static_cast<T>(_readerPos - _atomicReaderPos.load(std::memory_order_relaxed)) >= _bytesPerBatch) {
					_atomicReaderPos.store(_readerPos, std::memory_order_release);

#	if defined(DEATH_TARGET_X86) && defined(DEATH_TARGET_CLFLUSHOPT) && !defined(DEATH_TARGET_CLANG_CL)
					flushCacheLines(_lastFlushedReaderPos, _readerPos);
#	endif
				}
			}

			/** @brief Checks if the queue is empty, should be called only by the reader */
			bool empty() const noexcept {
				if (_writerPosCache == _readerPos) {
					// if we think the queue is empty we also load the atomic variable to check further
					_writerPosCache = _atomicWriterPos.load(std::memory_order_acquire);

					if (_writerPosCache == _readerPos) {
						return true;
					}
				}

				return false;
			}

			T capacity() const noexcept {
				return static_cast<T>(_capacity);
			}

			bool hugePagesEnabled() const noexcept {
				return _hugePagesEnabled;
			}

		private:
			static constexpr T CacheLineMask{CacheLineSize - 1};

			const T _capacity;
			const T _capacityMask;
			const T _bytesPerBatch;
			std::uint8_t* _storage{nullptr};
			const bool _hugePagesEnabled;

			alignas(CacheLineAligned) std::atomic<T> _atomicWriterPos{0};
			alignas(CacheLineAligned) T _writerPos{0};
			T _cachedReaderPos{0};
			T _lastFlushedWriterPos{0};

			alignas(CacheLineAligned) std::atomic<T> _atomicReaderPos{0};
			alignas(CacheLineAligned) T _readerPos{0};
			mutable T _writerPosCache{0};
			T _lastFlushedReaderPos{0};

#	if defined(DEATH_TARGET_X86) && defined(DEATH_TARGET_CLFLUSHOPT) && !defined(DEATH_TARGET_CLANG_CL)
			// _mm_clflushopt is supported only since Skylake and requires "-mclflushopt" option on GCC/clang, and is undefined on Clang-CL
			void flushCacheLines(T& last, T offset) noexcept {
				T lastDiff = last - (last & CacheLineMask);
				T curDiff = offset - (offset & CacheLineMask);

				while (curDiff > lastDiff) {
					_mm_clflushopt(_storage + (lastDiff & _capacityMask));
					lastDiff += CacheLineSize;
					last = lastDiff;
				}
			}
#	endif

			static std::uint8_t* alignPointer(void* pointer, std::size_t alignment) noexcept {
				DEATH_DEBUG_ASSERT(IsPowerOfTwo(alignment), "alignment must be a power of two", reinterpret_cast<std::uint8_t*>(pointer));
				return reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(pointer) + (alignment - 1ul)) &
													~(alignment - 1ul));
			}

			static void* allocAligned(std::size_t size, std::size_t alignment, DEATH_UNUSED bool hugesPagesEnabled) noexcept {
#	if defined(DEATH_TARGET_WINDOWS)
				void* p = _aligned_malloc(size, alignment);
				DEATH_DEBUG_ASSERT(p != nullptr);
				return p;
#	elif defined(DEATH_TARGET_SWITCH)
				void* p = ::memalign(alignment, size);
				DEATH_DEBUG_ASSERT(p != nullptr);
				return p;
#	else
				// Calculate the total size including the metadata and alignment
				constexpr std::size_t MetadataSize = 2u * sizeof(std::size_t);
				std::size_t totalSize = size + MetadataSize + alignment;

				// Allocate the memory
				int flags = MAP_PRIVATE | MAP_ANONYMOUS;

#		if defined(__linux__)
				if (hugesPagesEnabled) {
					flags |= MAP_HUGETLB;
				}
#		endif

				void* mem = ::mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, flags, -1, 0);

#		if defined(__linux__)
				if (mem == MAP_FAILED && hugesPagesEnabled) {
					flags &= ~MAP_HUGETLB;
					mem = ::mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, flags, -1, 0);
				}
#		endif

				DEATH_DEBUG_ASSERT(mem != MAP_FAILED, ("mmap() failed with error {} ({})", errno, strerror(errno)), nullptr);

				// Calculate the aligned address after the metadata
				std::uint8_t* alignedAddress = alignPointer(static_cast<std::uint8_t*>(mem) + MetadataSize, alignment);

				// Calculate the offset from the original memory location
				std::size_t offset = static_cast<std::size_t>(alignedAddress - static_cast<std::uint8_t*>(mem));

				// Store the size and offset information in the metadata
				std::memcpy(alignedAddress - sizeof(std::size_t), &totalSize, sizeof(totalSize));
				std::memcpy(alignedAddress - (2u * sizeof(std::size_t)), &offset, sizeof(offset));

				return alignedAddress;
#	endif
			}

			static void freeAligned(void* ptr) noexcept {
#	if defined(DEATH_TARGET_WINDOWS)
				_aligned_free(ptr);
#	elif defined(DEATH_TARGET_SWITCH)
				::free(ptr);
#	else
				// Retrieve the size and offset information from the metadata
				std::size_t offset;
				std::memcpy(&offset, static_cast<std::uint8_t*>(ptr) - (2u * sizeof(std::size_t)), sizeof(offset));

				std::size_t totalSize;
				std::memcpy(&totalSize, static_cast<std::uint8_t*>(ptr) - sizeof(std::size_t), sizeof(totalSize));

				// Calculate the original memory block address
				void* mem = static_cast<std::uint8_t*>(ptr) - offset;

				::munmap(mem, totalSize);
#	endif
			}
		};

		using BoundedSPSCQueue = BoundedSPSCQueueImpl<std::size_t>;

		/**
			@brief Unbounded single-producer single-consumer FIFO queue (ring buffer)

			When the internal circular buffer becomes full, a new one will be created and the production will continue
			in the new buffer. Consumption is wait free. If no data is available, a special value is returned.
			If a new buffer is created from the producer, the consumer first consumes everything in the old buffer
			and then moves to the new buffer.
		*/
		class UnboundedSPSCQueue
		{
		private:
			struct Node
			{
				explicit Node(std::size_t boundedQueueCapacity, bool hugePagesEnabled)
					: boundedQueue(boundedQueueCapacity, hugePagesEnabled) {}

				std::atomic<Node*> next{nullptr};
				BoundedSPSCQueue boundedQueue;
			};

		public:
			struct ReadResult
			{
				explicit ReadResult(const std::uint8_t* readPosition) : readPos(readPosition) {}

				const std::uint8_t* readPos;
				std::size_t previousCapacity{0};
				std::size_t newCapacity{0};
				bool allocation{false};
			};

			explicit UnboundedSPSCQueue(std::size_t initialBoundedQueueCapacity, bool hugesPagesEnabled = false)
				: _producer(new Node(initialBoundedQueueCapacity, hugesPagesEnabled)), _consumer(_producer) {}

			~UnboundedSPSCQueue() noexcept {
				// Get the current consumer node
				Node* currentNode = _consumer;

				// Look for extra nodes to delete
				while (currentNode != nullptr) {
					Node* toDelete = currentNode;
					currentNode = currentNode->next;
					delete toDelete;
				}
			}

			UnboundedSPSCQueue(UnboundedSPSCQueue const&) = delete;
			UnboundedSPSCQueue& operator=(UnboundedSPSCQueue const&) = delete;

			std::uint8_t* prepareWrite(std::size_t nbytes) noexcept {
				// Try to reserve the bounded queue
				std::uint8_t* writePos = _producer->boundedQueue.prepareWrite(nbytes);

				if DEATH_LIKELY(writePos != nullptr) {
					return writePos;
				}

				return handleFullQueue(nbytes);
			}

			void finishWrite(std::size_t nbytes) noexcept {
				_producer->boundedQueue.finishWrite(nbytes);
			}

			void commitWrite() noexcept {
				_producer->boundedQueue.commitWrite();
			}

			void finishAndCommitWrite(std::size_t nbytes) noexcept {
				finishWrite(nbytes);
				commitWrite();
			}

			std::size_t producerCapacity() const noexcept {
				return _producer->boundedQueue.capacity();
			}

			void shrink(std::size_t capacity) noexcept {
				if (capacity > (_producer->boundedQueue.capacity() >> 1)) {
					// We should only shrink if the new capacity is less or at least equal to the previous_power_of_2
					return;
				}

				// We want to shrink the queue, we will create a new queue with a smaller size
				// the consumer will switch to the newer queue after emptying and deallocating the older queue
				auto const nextNode = new Node{capacity, _producer->boundedQueue.hugePagesEnabled()};

				// Store the new node pointer as next in the current node
				_producer->next.store(nextNode, std::memory_order_release);

				// Producer is now using the next node
				_producer = nextNode;
			}

			ReadResult prepareRead() noexcept {
				ReadResult readResult{_consumer->boundedQueue.prepareRead()};

				if (readResult.readPos != nullptr) {
					return readResult;
				}

				// The buffer is empty, check if another buffer exists
				Node* const nextNode = _consumer->next.load(std::memory_order_acquire);

				if (nextNode != nullptr) {
					return readNextQueue(nextNode);
				}

				// Queue is empty and no new queue exists
				return readResult;
			}

			void finishRead(std::size_t nbytes) noexcept {
				_consumer->boundedQueue.finishRead(nbytes);
			}

			void commitRead() noexcept {
				_consumer->boundedQueue.commitRead();
			}

			std::size_t capacity() const noexcept {
				return _consumer->boundedQueue.capacity();
			}

			bool empty() const noexcept {
				return _consumer->boundedQueue.empty() && (_consumer->next.load(std::memory_order_relaxed) == nullptr);
			}

		private:
			// Modified by either the producer or consumer but never both
			alignas(CacheLineAligned) Node* _producer{nullptr};
			alignas(CacheLineAligned) Node* _consumer{nullptr};

			std::uint8_t* handleFullQueue(std::size_t nbytes) noexcept {
				// Then it means the queue doesn't have enough size
				std::size_t capacity = _producer->boundedQueue.capacity() * 2ULL;
				while (capacity < (nbytes + 1)) {
					capacity = capacity * 2ULL;
				}

				// Apply some hard limits also on UnboundedSPSCQueue
				constexpr std::size_t MaxBoundedQueueSize = 2ULL * 1024 * 1024 * 1024; // 2 GB
				if DEATH_UNLIKELY(capacity > MaxBoundedQueueSize) {
					DEATH_DEBUG_ASSERT(nbytes <= MaxBoundedQueueSize);
					// We reached the MaxBoundedQueueSize, we won't be allocating more, instead return nullptr to block or drop
					return nullptr;
				}

				// Commit previous write to the old queue before switching
				_producer->boundedQueue.commitWrite();

				// We failed to reserve because the queue was full, create a new node with a new queue
				Node* nextNode = new Node{capacity, _producer->boundedQueue.hugePagesEnabled()};

				// Store the new node pointer as next in the current node
				_producer->next.store(nextNode, std::memory_order_release);

				// Producer is now using the next node
				_producer = nextNode;

				// Reserve again, this time we know we will always succeed, cast to void* to ignore
				std::uint8_t* const writePos = _producer->boundedQueue.prepareWrite(nbytes);
				DEATH_DEBUG_ASSERT(writePos != nullptr);

				return writePos;
			}

			ReadResult readNextQueue(Node* nextNode) noexcept {
				// New buffer was added by the producer, this happens only when we have allocated a new queue

				// Try the existing buffer once more
				ReadResult readResult{_consumer->boundedQueue.prepareRead()};
				if (readResult.readPos != nullptr) {
					return readResult;
				}

				// Switch to the new buffer for reading commit the previous reads before deleting the queue
				_consumer->boundedQueue.commitRead();

				// Switch to the new buffer, existing one is deleted
				std::size_t previousCapacity = _consumer->boundedQueue.capacity();
				delete _consumer;

				_consumer = nextNode;
				readResult.readPos = _consumer->boundedQueue.prepareRead();

				// We switched to a new here, so we store the capacity info to return it
				readResult.allocation = true;
				readResult.newCapacity = _consumer->boundedQueue.capacity();
				readResult.previousCapacity = previousCapacity;

				return readResult;
			}
		};
	}
#endif

	/**
		@brief Stores required information about a logged entry to be dispatched to sinks
		
		This class should not usually be used directly.
	*/
	struct TransitEvent
	{
		/** @brief Timestamp */
		std::uint64_t Timestamp;
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 cannot handle anonymous unions properly
		union {
#endif
			/** @brief Function name */
			const char* FunctionName;
			/** @brief Pointer to flush flag in case of flush event */
			std::atomic<bool>* FlushFlag;
			/** @brief Requested capacity in case of initialization event */
			std::uint32_t Capacity;
#ifndef DOXYGEN_GENERATING_OUTPUT
		};
#endif
		/** @brief Message */
		std::string Message;
		/** @brief Trace level */
		TraceLevel Level;

		TransitEvent()
			: Timestamp(0), FunctionName(nullptr), Level(TraceLevel::Unknown) {}

		~TransitEvent() = default;

		TransitEvent(TransitEvent const& other) = delete;
		TransitEvent& operator=(TransitEvent const& other) = delete;

		TransitEvent(TransitEvent&& other) noexcept
			: Timestamp(other.Timestamp), Message(Death::move(other.Message)), FlushFlag(other.FlushFlag), Level(other.Level) {}

		TransitEvent& operator=(TransitEvent&& other) noexcept {
			if (this != &other) {
				Timestamp = other.Timestamp;
				Message = Death::move(other.Message);
				FlushFlag = other.FlushFlag;
				Level = other.Level;
			}

			return *this;
		}
	};

#if defined(DEATH_TRACE_ASYNC) || defined(DOXYGEN_GENERATING_OUTPUT)
	/**
		@brief Circular buffer for transit events
		
		This class should not usually be used directly.
	*/
	class TransitEventBuffer
	{
	public:
		explicit TransitEventBuffer(std::size_t initialCapacity)
			: _capacity(Implementation::NextPowerOfTwo(initialCapacity)), _storage(std::make_unique<TransitEvent[]>(_capacity)),
				_capacityMask(_capacity - 1u), _readerPos(0), _writerPos(0) {}

		TransitEventBuffer(TransitEventBuffer const&) = delete;
		TransitEventBuffer& operator=(TransitEventBuffer const&) = delete;

		TransitEventBuffer(TransitEventBuffer&& other) noexcept
			: _capacity(other._capacity), _storage(Death::move(other._storage)), _capacityMask(other._capacityMask),
				_readerPos(other._readerPos), _writerPos(other._writerPos)
		{
			other._capacity = 0;
			other._capacityMask = 0;
			other._readerPos = 0;
			other._writerPos = 0;
		}

		TransitEventBuffer& operator=(TransitEventBuffer&& other) noexcept {
			if (this != &other) {
				_capacity = other._capacity;
				_storage = Death::move(other._storage);
				_capacityMask = other._capacityMask;
				_readerPos = other._readerPos;
				_writerPos = other._writerPos;

				other._capacity = 0;
				other._capacityMask = 0;
				other._readerPos = 0;
				other._writerPos = 0;
			}
			return *this;
		}

		/** @brief Returns a pointer to the first transit event in the buffer, or `nullptr` if the buffer is empty */
		TransitEvent* front() noexcept {
			if (_readerPos == _writerPos) {
				return nullptr;
			}
			return &_storage[_readerPos & _capacityMask];
		}

		/** @brief Consumes the first transit event from the buffer */
		void pop_front() noexcept {
			++_readerPos;
		}

		/** @brief Returns a pointer to the last transit event in the buffer, or expands the buffer if it is full */
		TransitEvent* back() noexcept {
			if (_capacity == size()) {
				// Buffer is full, need to expand
				expand();
			}
			return &_storage[_writerPos & _capacityMask];
		}

		/** @brief Adds a new transit event to be consumed */
		void push_back() noexcept {
			++_writerPos;
		}

		/** @brief Returns the number of unconsumed events */
		std::size_t size() const noexcept {
			return _writerPos - _readerPos;
		}

		/** @brief Returns the capacity of the buffer */
		std::size_t capacity() const noexcept {
			return _capacity;
		}

		/** @brief Returns `true` if the buffer is empty */
		bool empty() const noexcept {
			return _readerPos == _writerPos;
		}

	private:
		std::size_t _capacity;
		std::unique_ptr<TransitEvent[]> _storage;
		std::size_t _capacityMask;
		std::size_t _readerPos;
		std::size_t _writerPos;

		void expand() noexcept {
			std::size_t newCapacity = _capacity * 2;
			auto newStorage = std::make_unique<TransitEvent[]>(newCapacity);

			// Move existing elements from the old storage to the new storage.
			// Since the buffer is full, this moves all the previous TransitEvents, preserving their order.
			// The reader position and mask are used to handle the circular buffer's wraparound.
			std::size_t currentSize = size();
			for (std::size_t i = 0; i < currentSize; ++i) {
				newStorage[i] = Death::move(_storage[(_readerPos + i) & _capacityMask]);
			}

			_storage = Death::move(newStorage);
			_capacity = newCapacity;
			_capacityMask = _capacity - 1;
			_writerPos = currentSize;
			_readerPos = 0;
		}
	};

	class LoggerBackend;

	/**
		@brief Stores information about the current thread and allows to receive logged entries
		
		This class should not usually be used directly.
	*/
	class ThreadContext
	{
		friend class LoggerBackend;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		union SpscQueueUnion
		{
			Implementation::UnboundedSPSCQueue UnboundedSpscQueue;
			Implementation::BoundedSPSCQueue BoundedSpscQueue;

			SpscQueueUnion() {}
			~SpscQueueUnion() {}
		};
#endif

	public:
		ThreadContext(Implementation::QueueType queueType, std::uint32_t initialSpscQueueCapacity, bool hugesPagesEnabled)
			: _threadId(format("{}", Implementation::GetNativeThreadId())), _transitEventBuffer(Implementation::TransitEventBufferInitialCapacity),
				_queueType(queueType), _valid{true}, _failureCounter{0}
		{
			if (HasUnboundedQueueType()) {
				new (&_spscQueueUnion.UnboundedSpscQueue) Implementation::UnboundedSPSCQueue{initialSpscQueueCapacity, hugesPagesEnabled};
			} else if (HasBoundedQueueType()) {
				new (&_spscQueueUnion.BoundedSpscQueue) Implementation::BoundedSPSCQueue{initialSpscQueueCapacity, hugesPagesEnabled};
			}
		}

		~ThreadContext() noexcept {
			if (HasUnboundedQueueType()) {
				_spscQueueUnion.UnboundedSpscQueue.~UnboundedSPSCQueue();
			} else if (HasBoundedQueueType()) {
				_spscQueueUnion.BoundedSpscQueue.~BoundedSPSCQueueImpl();
			}
		}

		ThreadContext(ThreadContext const&) = delete;
		ThreadContext& operator=(ThreadContext const&) = delete;

#ifdef DOXYGEN_GENERATING_OUTPUT
		/** @brief Returns single-producer single-consumer queue for logged entries */
		Implementation::SPSCQueue& GetSpscQueue() noexcept;
#else
		SpscQueueUnion const& GetSpscQueueUnion() const noexcept {
			return _spscQueueUnion;
		}

		SpscQueueUnion& GetSpscQueueUnion() noexcept {
			return _spscQueueUnion;
		}

		template<Implementation::QueueType queueType_>
		std::conditional_t<queueType_ == Implementation::QueueType::UnboundedBlocking || queueType_ == Implementation::QueueType::UnboundedDropping,
			Implementation::UnboundedSPSCQueue, Implementation::BoundedSPSCQueue>& GetSpscQueue() noexcept
		{
			DEATH_DEBUG_ASSERT(_queueType == queueType_);

			if constexpr (queueType_ == Implementation::QueueType::UnboundedBlocking || queueType_ == Implementation::QueueType::UnboundedDropping) {
				return _spscQueueUnion.UnboundedSpscQueue;
			} else {
				return _spscQueueUnion.BoundedSpscQueue;
			}
		}

		template<Implementation::QueueType queueType_>
		std::conditional_t<queueType_ == Implementation::QueueType::UnboundedBlocking || queueType_ == Implementation::QueueType::UnboundedDropping,
			Implementation::UnboundedSPSCQueue, Implementation::BoundedSPSCQueue> const& GetSpscQueue() const noexcept
		{
			DEATH_DEBUG_ASSERT(_queueType == queueType_);

			if constexpr (queueType_ == Implementation::QueueType::UnboundedBlocking || queueType_ == Implementation::QueueType::UnboundedDropping) {
				return _spscQueueUnion.UnboundedSpscQueue;
			} else {
				return _spscQueueUnion.BoundedSpscQueue;
			}
		}
#endif

		/** @brief Returns `true` if a bounded queue is used */
		bool HasBoundedQueueType() const noexcept {
			return (_queueType == Implementation::QueueType::BoundedBlocking) || (_queueType == Implementation::QueueType::BoundedDropping);
		}

		/** @brief Returns `true` if an unbounded queue is used */
		bool HasUnboundedQueueType() const noexcept {
			return (_queueType == Implementation::QueueType::UnboundedBlocking) || (_queueType == Implementation::QueueType::UnboundedDropping);
		}

		/** @brief Returns `true` if a dropping queue is used */
		bool HasDroppingQueue() const noexcept {
			return (_queueType == Implementation::QueueType::UnboundedDropping) || (_queueType == Implementation::QueueType::BoundedDropping);
		}

		/** @brief Returns `true` if a blocking queue is used */
		bool HasBlockingQueue() const noexcept {
			return (_queueType == Implementation::QueueType::UnboundedBlocking) || (_queueType == Implementation::QueueType::BoundedBlocking);
		}

		/** @brief Returns the thread ID of the current thread context */
		Containers::StringView GetThreadId() const noexcept {
			return _threadId;
		}

		/** @brief Marks the thread context as invalid */
		void MarkInvalid() noexcept {
			_valid.store(false, std::memory_order_relaxed);
		}

		/** @brief Returns `true` if the thread context is still valid */
		bool IsValid() const noexcept {
			return _valid.load(std::memory_order_relaxed);
		}

		/** @brief Increments number of failures */
		void IncrementFailureCounter() noexcept {
			_failureCounter.fetch_add(1, std::memory_order_relaxed);
		}

		/** @brief Returns the current number of failures and resets the counter */
		std::size_t GetAndResetFailureCounter() noexcept {
			if DEATH_LIKELY(_failureCounter.load(std::memory_order_relaxed) == 0) {
				return 0;
			}
			return _failureCounter.exchange(0, std::memory_order_relaxed);
		}

	private:
		SpscQueueUnion _spscQueueUnion;
		Containers::String _threadId;
		TransitEventBuffer _transitEventBuffer;
		Implementation::QueueType _queueType;
		std::atomic<bool> _valid;
		alignas(Implementation::CacheLineAligned) std::atomic<std::size_t> _failureCounter;
	};

	/**
		@brief Manages thread contexts for @ref LoggerBackend
		
		This class should not usually be used directly.
	*/
	class ThreadContextManager
	{
	public:
		/** @brief Returns static instance of thread context manager */
		static ThreadContextManager& Get() noexcept;

		ThreadContextManager(ThreadContextManager const&) = delete;
		ThreadContextManager& operator=(ThreadContextManager const&) = delete;

		/** @brief Calls the specified callback for each registered thread context */
		template<typename TCallback>
		void ForEachThreadContext(TCallback cb) noexcept {
			std::unique_lock lock{_spinlock};

			for (auto const& elem : _threadContexts) {
				cb(elem.get());
			}
		}

		/** @brief Registers a new thread context */
		void RegisterThreadContext(std::shared_ptr<ThreadContext> const& threadContext) noexcept;
		/** @brief Adds an invalid thread context */
		void AddInvalidThreadContext() noexcept;
		/** @brief Returns `true` if an invalid thread context is present */
		bool HasInvalidThreadContext() const noexcept;
		/** @brief Returns `true` if a new thread context is present */
		bool HasNewThreadContext() noexcept;
		/** @brief Removes shared invalidated thread context */
		void RemoveSharedInvalidatedThreadContext(ThreadContext const* threadContext) noexcept;

	private:
		Containers::SmallVector<std::shared_ptr<ThreadContext>, 0> _threadContexts;
		Threading::Spinlock _spinlock;
		std::atomic<bool> _newThreadContextFlag{false};
		std::atomic<std::uint8_t> _invalidThreadContextCount{0};

		ThreadContextManager() = default;
		~ThreadContextManager() = default;
	};

	/**
		@brief Handles lifetime of the thread context
		
		This class should not usually be used directly.
	*/
	class ScopedThreadContext
	{
	public:
		ScopedThreadContext(Implementation::QueueType queueType, std::uint32_t spscQueueCapacity, bool hugePagesEnabled)
			: _threadContext(std::make_shared<ThreadContext>(queueType, spscQueueCapacity, hugePagesEnabled))
		{
			ThreadContextManager::Get().RegisterThreadContext(_threadContext);
		}

		~ScopedThreadContext() noexcept {
			// This destructor will get called when the thread that created this wrapper stops. We will only invalidate
			// the thread context, so the backend thread will empty an invalidated ThreadContext and then remove it from
			// the ThreadContextManager. Main thread is only exception for the thread who owns the ThreadContextManager.
			// The thread context of the main thread can get deleted before getting invalidated
			_threadContext->MarkInvalid();

			// Notify the backend thread that one context has been removed
			ThreadContextManager::Get().AddInvalidThreadContext();
		}

		ScopedThreadContext(ScopedThreadContext const&) = delete;
		ScopedThreadContext& operator=(ScopedThreadContext const&) = delete;

		/** @brief Returns the assigned thread context */
		ThreadContext* GetThreadContext() const noexcept {
			DEATH_DEBUG_ASSERT(_threadContext != nullptr);
			return _threadContext.get();
		}

	private:
		std::shared_ptr<ThreadContext> _threadContext;
	};
#endif
	
	/**
		@brief Storage for deferred transit events
		
		This class should not usually be used directly.
	*/
	class BacktraceStorage
	{
	public:
		BacktraceStorage();

		/** @brief Stores the specified transit event */
		void Store(TransitEvent transitEvent, Containers::StringView threadId) noexcept;
		/** @brief Processes all stored transit events */
		void Process(Containers::Function<void(TransitEvent const& event, Containers::StringView threadId)>&& callback) noexcept;
		/** @brief Resizes the storage to the specified capacity */
		void SetCapacity(std::uint32_t capacity) noexcept;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct StoredTransitEvent
		{
			StoredTransitEvent(Containers::String threadId, TransitEvent transitEvent);

			Containers::String ThreadId;
			TransitEvent Event;
		};
#endif

		std::uint32_t _capacity;
		std::uint32_t _index;
		Containers::Array<StoredTransitEvent> _storedEvents;
	};

	/**
		@brief Logger backend processes trace items in the background
		
		This class should not usually be used directly.
	*/
	class LoggerBackend
	{
	public:
		LoggerBackend();
		~LoggerBackend();

		LoggerBackend(LoggerBackend const&) = delete;
		LoggerBackend& operator=(LoggerBackend const&) = delete;

		/** @brief Registers the sink */
		void AttachSink(ITraceSink* sink);
		/** @brief Unregisters the sink */
		void RemoveSink(ITraceSink* sink);

		/** @brief Notifies the background worker about new entries in the queue */
		void Notify() noexcept;

#if defined(DEATH_TRACE_ASYNC) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Returns `true` if the background worker is alive */
		bool IsAlive() const noexcept;
		/** @brief Returns `true` if the current thread is the background worker thread */
		bool IsWorkerThread() const noexcept;
#else
		/** @brief Dispatches the specified entry to all sinks */
		void DispatchEntryToSinks(TraceLevel level, std::uint64_t timestamp, const void* functionName, const void* content, std::uint32_t contentLength, Containers::StringView threadId) noexcept;
		/** @brief Flushes and waits until all prior entries are written to all sinks */
		void FlushActiveSinks() noexcept;

		/** @brief Initializes backtrace storage to be able to use @ref TraceLevel::Deferred */
		void InitializeBacktrace(std::uint32_t maxCapacity);
		/** @brief Writes any stored deferred entries to all sinks asynchronously */
		void FlushBacktraceAsync() noexcept;
		/** @brief Enqueues the specified entry to backtrace storage */
		void EnqueueEntryToBacktrace(std::uint64_t timestamp, const void* functionName, const void* content, std::uint32_t contentLength) noexcept;
#endif

		/** @brief Returns minimum trace level to trigger automatic flushing of deferred entries */
		TraceLevel GetBacktraceFlushLevel() const noexcept;
		/** @brief Sets minimum trace level to trigger automatic flushing of deferred entries */
		void SetBacktraceFlushLevel(TraceLevel flushLevel) noexcept;

	private:
		Containers::SmallVector<ITraceSink*, 1> _sinks;
		std::shared_ptr<BacktraceStorage> _backtraceStorage;

		void Initialize();
		void Dispose();

#if defined(DEATH_TRACE_ASYNC)
		std::thread _workerThread;
		Death::Threading::AutoResetEvent _wakeUpEvent;
		std::atomic<TraceLevel> _backtraceFlushLevel;
		std::atomic<bool> _workerThreadAlive;
		Implementation::RdtscClock _rdtscClock;
		Containers::SmallVector<ThreadContext*, 0> _activeThreadContextsCache;
		std::chrono::system_clock::time_point _lastRdtscResyncTime;

		void CleanUpBeforeExit() noexcept;
		void UpdateActiveThreadContextsCache() noexcept;
		void CleanUpInvalidatedThreadContexts() noexcept;
		bool PopulateTransitEventFromThreadQueue(const std::uint8_t*& readPos, ThreadContext* threadContext, std::uint64_t tsNow) noexcept;

		const std::uint8_t* ReadUnboundedThreadQueue(Implementation::UnboundedSPSCQueue& frontendQueue, ThreadContext* threadContext) const noexcept {
			auto readResult = frontendQueue.prepareRead();

			/*if (readResult.allocation) {
				LOGD("Allocated new queue with capacity of {} kB (previously {} kB) from thread {}",
					(readResult.newCapacity / 1024), (readResult.previousCapacity / 1024), threadContext->GetThreadId());
			}*/

			return readResult.readPos;
		}

		template<typename TThreadQueue>
		std::size_t ReadAndDecodeThreadQueue(TThreadQueue& frontendQueue, ThreadContext* threadContext, std::uint64_t tsNow) noexcept {
			// Note: The producer commits only complete messages to the queue.
			// Therefore, if even a single byte is present in the queue, it signifies a full message.
			std::size_t queueCapacity = frontendQueue.capacity();
			std::size_t totalBytesRead = 0;

			do {
				const std::uint8_t* readPos;
				if constexpr (std::is_same_v<TThreadQueue, Implementation::UnboundedSPSCQueue>) {
					readPos = ReadUnboundedThreadQueue(frontendQueue, threadContext);
				} else {
					readPos = frontendQueue.prepareRead();
				}

				if (readPos == nullptr) {
					// Nothing to read
					break;
				}

				const std::uint8_t* readBegin = readPos;

				if (!PopulateTransitEventFromThreadQueue(readPos, threadContext, tsNow)) {
					break;
				}

				// Finish reading
				DEATH_DEBUG_ASSERT(readPos >= readBegin, "readBuffer should be greater or equal to readBegin", 0);
				std::size_t bytesRead = static_cast<std::size_t>(readPos - readBegin);
				frontendQueue.finishRead(bytesRead);
				totalBytesRead += bytesRead;
				// Reads a maximum of one full frontend queue or the transit events' hard limit to prevent getting stuck on the same producer.
			} while (totalBytesRead < queueCapacity && threadContext->_transitEventBuffer.size() < Implementation::TransitEventsHardLimit);

			if (totalBytesRead != 0) {
				// If we read something from the queue, we commit all the reads together at the end. This strategy
				// enhances cache coherence performance by updating the shared atomic flag only once.
				frontendQueue.commitRead();
			}

			return threadContext->_transitEventBuffer.size();
		}

		std::size_t PopulateTransitEventsFromFrontendQueues() noexcept;
		bool HasPendingEventsForCachingWhenTransitEventBufferEmpty() noexcept;
		bool CheckThreadQueuesAndCachedTransitEventsEmpty() noexcept;
		void ResyncRdtscClock() noexcept;
		void DispatchTransitEventToSinks(TransitEvent const& transitEvent, Containers::StringView threadId) noexcept;
		void FlushActiveSinks() noexcept;
		void ProcessTransitEvent(ThreadContext const& threadContext, TransitEvent& transitEvent, std::atomic<bool>*& flushFlag) noexcept;
		bool ProcessLowestTimestampTransitEvent() noexcept;
		void ProcessEvents() noexcept;
#else
		TraceLevel _backtraceFlushLevel;
#endif
	};

	/**
		@brief Logger enqueues trace items for processing
	
		This class should not usually be used directly.
	*/
	class Logger
	{
	public:
		Logger() {}
		virtual ~Logger() = default;

		Logger(Logger const&) = delete;
		Logger& operator=(Logger const&) = delete;

		/** @brief Registers the sink */
		void AttachSink(ITraceSink* sink);
		/** @brief Unregisters the sink */
		void RemoveSink(ITraceSink* sink);

		/** @brief Writes the specified entry to all sinks */
		bool Write(TraceLevel level, const char* functionName, const char* message, std::uint32_t messageLength);
		/** @brief Flushes and waits until all prior entries are written to all sinks */
		void Flush(std::uint32_t sleepDurationNs = 100) noexcept;

		/** @brief Initializes backtrace storage to be able to use @ref TraceLevel::Deferred */
		DEATH_NEVER_INLINE void InitializeBacktrace(std::uint32_t maxCapacity, TraceLevel flushLevel = TraceLevel::Unknown);
		/** @brief Writes any stored deferred entries to all sinks asynchronously */
		void FlushBacktraceAsync() noexcept;

#if defined(DEATH_TRACE_ASYNC) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Shrinks the thread-local queue to the specified target capacity */
		void ShrinkThreadLocalQueue(std::size_t capacity) noexcept;
		/** @brief Returns the current capacity of the thread-local queue */
		std::size_t GetThreadLocalQueueCapacity() noexcept;
#endif

	private:
		LoggerBackend _backend;

#if defined(DEATH_TRACE_ASYNC)
		static inline DEATH_THREAD_LOCAL ThreadContext* _threadContext = nullptr;

		static ThreadContext* GetLocalThreadContext() noexcept;
#endif

		bool EnqueueEntry(TraceLevel level, std::uint64_t timestamp, const void* functionName, const void* content, std::uint32_t contentLength) noexcept;
	};

}}

#endif