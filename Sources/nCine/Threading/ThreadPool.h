#pragma once

#include "IThreadPool.h"
#include "ThreadSync.h"
#include "Thread.h"

#include <list>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Thread pool
		
		Maintains a fixed set of worker threads that pull queued @ref IThreadCommand instances off a
		shared queue and execute them. Implements the @ref IThreadPool interface and is non-copyable.
	*/
	class ThreadPool : public IThreadPool
	{
	public:
		/** @brief Creates a thread pool with as many worker threads as available processors */
		ThreadPool();
		/** @brief Creates a thread pool with the given number of worker threads */
		explicit ThreadPool(std::size_t numThreads);
		~ThreadPool() override;

		void EnqueueCommand(std::unique_ptr<IThreadCommand>&& threadCommand) override;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ThreadStruct
		{
			std::list<std::unique_ptr<IThreadCommand>>* queue;
			Mutex* queueMutex;
			CondVariable* queueCV;
			bool shouldQuit;
		};
#endif

		std::list<std::unique_ptr<IThreadCommand>> queue_;
		SmallVector<Thread, 0> threads_;
		Mutex queueMutex_;
		CondVariable queueCV_;
		Mutex quitMutex_;
		std::size_t numThreads_;

		ThreadStruct threadStruct_;
		static void WorkerFunction(void* arg);

		/** @brief Deleted copy constructor */
		ThreadPool(const ThreadPool&) = delete;
		/** @brief Deleted assignment operator */
		ThreadPool& operator=(const ThreadPool&) = delete;
	};
}