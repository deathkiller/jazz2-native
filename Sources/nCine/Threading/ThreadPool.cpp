#if defined(WITH_THREADS)

#include "ThreadPool.h"
#include "../../Main.h"

namespace nCine
{
	ThreadPool::ThreadPool()
		: ThreadPool(Thread::GetProcessorCount())
	{
	}

	ThreadPool::ThreadPool(std::size_t numThreads)
	{
		_threadStruct.queue = &_queue;
		_threadStruct.queueMutex = &_queueMutex;
		_threadStruct.queueCV = &_queueCV;
		_threadStruct.shouldQuit = false;

		// Only reserve the storage, the workers are appended below --- sizing the container here would
		// prepend that many default-constructed threads and the destructor would join those instead
		_threads.reserve(numThreads);

		for (std::size_t i = 0; i < numThreads; i++) {
			_threads.emplace_back(WorkerFunction, &_threadStruct);
		}
	}

	ThreadPool::~ThreadPool()
	{
		// The flag has to be set under the same lock the workers use to evaluate it, otherwise a worker
		// that is between the check and the wait would miss the broadcast and never wake up again
		_queueMutex.Lock();
		_threadStruct.shouldQuit = true;
		_queueMutex.Unlock();

		_queueCV.Broadcast();

		// All workers must be joined before the queue, the mutex and the condition variable they still
		// reference are destroyed with this object
		for (auto& thread : _threads) {
			thread.Join();
		}
	}

	void ThreadPool::EnqueueCommand(std::unique_ptr<IThreadCommand>&& threadCommand)
	{
		DEATH_ASSERT(threadCommand);

		_queueMutex.Lock();
		_queue.push_back(std::move(threadCommand));
		_queueCV.Broadcast();
		_queueMutex.Unlock();
	}

	void ThreadPool::WorkerFunction(void* arg)
	{
		ThreadStruct* threadStruct = static_cast<ThreadStruct*>(arg);

		LOGD("Worker thread {} is starting", Thread::GetCurrentId());

		while (true) {
			threadStruct->queueMutex->Lock();
			while (threadStruct->queue->empty() && !threadStruct->shouldQuit) {
				threadStruct->queueCV->Wait(*(threadStruct->queueMutex));
			}

			if (threadStruct->shouldQuit) {
				threadStruct->queueMutex->Unlock();
				break;
			}

			std::unique_ptr<IThreadCommand> threadCommand = std::move(threadStruct->queue->front());
			threadStruct->queue->pop_front();
			threadStruct->queueMutex->Unlock();

			LOGD("Worker thread {} is executing its command", Thread::GetCurrentId());
			threadCommand->Execute();
		}

		LOGD("Worker thread {} is exiting", Thread::GetCurrentId());
	}

}

#endif