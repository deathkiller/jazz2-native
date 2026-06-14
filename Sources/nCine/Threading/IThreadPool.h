#pragma once

#include "IThreadCommand.h"

#include <memory>

namespace nCine
{
	/**
		@brief Thread pool interface
		
		Abstract interface for a pool of worker threads that asynchronously execute queued
		@ref IThreadCommand instances. Implemented by @ref ThreadPool.
	*/
	class IThreadPool
	{
	public:
		virtual ~IThreadPool() = 0;

		/** @brief Enqueues a command to be executed by a worker thread */
		virtual void EnqueueCommand(std::unique_ptr<IThreadCommand>&& threadCommand) = 0;
	};

	inline IThreadPool::~IThreadPool() { }

#ifndef DOXYGEN_GENERATING_OUTPUT
	/** @brief Null thread pool that silently discards every enqueued command */
	class NullThreadPool : public IThreadPool
	{
	public:
		void EnqueueCommand(std::unique_ptr<IThreadCommand>&& threadCommand) override { }
	};
#endif
}