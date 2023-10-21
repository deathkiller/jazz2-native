#pragma once

#include "IThreadPool.h"
#include "ThreadSync.h"
#include "Thread.h"

#include <list>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/// Thread pool class
	class ThreadPool : public IThreadPool
	{
	public:
		/// Creates a thread pool with as many threads as available processors
		ThreadPool();
		/// Creates a thread pool with a specified number of threads
		explicit ThreadPool(std::size_t numThreads);
		~ThreadPool() override;

		/// Enqueues a command request for a worker thread
		void EnqueueCommand(std::unique_ptr<IThreadCommand>&& threadCommand) override;

	private:
		struct ThreadStruct
		{
			std::list<std::unique_ptr<IThreadCommand>>* queue;
			Mutex* queueMutex;
			CondVariable* queueCV;
			bool shouldQuit;
		};

		std::list<std::unique_ptr<IThreadCommand>> queue_;
		SmallVector<Thread, 0> threads_;
		Mutex queueMutex_;
		CondVariable queueCV_;
		Mutex quitMutex_;
		std::size_t numThreads_;

		ThreadStruct threadStruct_;
		static void WorkerFunction(void* arg);

		/// Deleted copy constructor
		ThreadPool(const ThreadPool&) = delete;
		/// Deleted assignment operator
		ThreadPool& operator=(const ThreadPool&) = delete;
	};
}