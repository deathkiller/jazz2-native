#pragma once

namespace nCine
{
	/**
		@brief Thread pool command interface
		
		Abstract unit of work submitted to an @ref IThreadPool. Derive from this interface and
		implement @ref Execute() to run code on a worker thread.
	*/
	class IThreadCommand
	{
	public:
		virtual ~IThreadCommand() {}

		/** @brief Executes the command on a worker thread */
		virtual void Execute() = 0;
	};
}