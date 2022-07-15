#pragma once

#include "IThreadCommand.h"

#include <memory>

namespace nCine
{
	/// Thread pool interface class
	class IThreadPool
	{
	public:
		virtual ~IThreadPool() = 0;

		/// Enqueues a command request for a worker thread
		virtual void enqueueCommand(std::unique_ptr<IThreadCommand> threadCommand) = 0;
	};

	inline IThreadPool::~IThreadPool() {}

	/// A fake thread pool which doesn't create any thread
	class NullThreadPool : public IThreadPool
	{
	public:
		void enqueueCommand(std::unique_ptr<IThreadCommand> threadCommand) override {}
	};

}
