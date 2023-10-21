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
		virtual void EnqueueCommand(std::unique_ptr<IThreadCommand>&& threadCommand) = 0;
	};

	inline IThreadPool::~IThreadPool() { }

	/// A fake thread pool which doesn't execute any commands
	class NullThreadPool : public IThreadPool
	{
	public:
		void EnqueueCommand(std::unique_ptr<IThreadCommand>&& threadCommand) override { }
	};
}