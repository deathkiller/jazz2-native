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
		: _threads(numThreads), _numThreads(numThreads)
	{
		_threadStruct.queue = &_queue;
		_threadStruct.queueMutex = &_queueMutex;
		_threadStruct.queueCV = &_queueCV;
		_threadStruct.shouldQuit = false;

		_quitMutex.Lock();

		for (std::size_t i = 0; i < _numThreads; i++) {
			_threads.emplace_back(WorkerFunction, &_threadStruct);
		}
	}

	ThreadPool::~ThreadPool()
	{
		_threadStruct.shouldQuit = true;
		_queueCV.Broadcast();

		for (std::size_t i = 0; i < _numThreads; i++) {
			_threads[i].Join();
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