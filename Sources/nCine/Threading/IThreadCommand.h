#pragma once

namespace nCine
{
	/// Thread pool command interface
	class IThreadCommand
	{
	public:
		virtual ~IThreadCommand() {}

		/// Executes the command on a worker thread
		virtual void Execute() = 0;
	};

}

