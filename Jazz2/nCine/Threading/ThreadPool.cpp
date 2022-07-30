#include "ThreadPool.h"
#include "../../Common.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	ThreadPool::ThreadPool()
		: ThreadPool(Thread::numProcessors())
	{
	}

	ThreadPool::ThreadPool(unsigned int numThreads)
		: threads_(numThreads), numThreads_(numThreads)
	{
		threadStruct_.queue = &queue_;
		threadStruct_.queueMutex = &queueMutex_;
		threadStruct_.queueCV = &queueCV_;
		threadStruct_.shouldQuit = false;

		quitMutex_.lock();

		//std::string threadName;
		for (unsigned int i = 0; i < numThreads_; i++) {
			threads_.emplace_back(workerFunction, &threadStruct_);
#if !defined(__EMSCRIPTEN__)
#if !defined(__APPLE__)
			// TODO
			//threadName.format("WorkerThread#%02d", i);
			//threads_.back().setName(threadName.data());
#endif
#if !defined(__ANDROID__)
			threads_.back().setAffinityMask(ThreadAffinityMask(i));
#endif
#endif
		}
	}

	ThreadPool::~ThreadPool()
	{
		threadStruct_.shouldQuit = true;
		queueCV_.broadcast();

		for (unsigned int i = 0; i < numThreads_; i++)
			threads_[i].join();
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void ThreadPool::enqueueCommand(std::unique_ptr<IThreadCommand> threadCommand)
	{
		//ASSERT(threadCommand);

		queueMutex_.lock();
		queue_.push_back(std::move(threadCommand));
		queueCV_.broadcast();
		queueMutex_.unlock();
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void ThreadPool::workerFunction(void* arg)
	{
		ThreadStruct* threadStruct = static_cast<ThreadStruct*>(arg);

		LOGD_X("Worker thread %u is starting", Thread::self());

		while (true) {
			threadStruct->queueMutex->lock();
			while (threadStruct->queue->empty() && threadStruct->shouldQuit == false)
				threadStruct->queueCV->wait(*(threadStruct->queueMutex));

			if (threadStruct->shouldQuit) {
				threadStruct->queueMutex->unlock();
				break;
			}

			std::unique_ptr<IThreadCommand> threadCommand = std::move(threadStruct->queue->front());
			threadStruct->queue->pop_front();
			threadStruct->queueMutex->unlock();

			LOGD_X("Worker thread %u is executing its command", Thread::self());
			threadCommand->execute();
		}

		LOGD_X("Worker thread %u is exiting", Thread::self());
	}

}
