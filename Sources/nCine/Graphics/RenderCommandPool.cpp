#include "RenderCommandPool.h"
#include "RenderCommand.h"
#include "RenderStatistics.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	RenderCommandPool::RenderCommandPool(unsigned int poolSize)
	{
		freeCommandsPool_.reserve(poolSize);
		usedCommandsPool_.reserve(poolSize);
	}

	RenderCommandPool::~RenderCommandPool() = default;

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	RenderCommand* RenderCommandPool::add()
	{
		std::unique_ptr<RenderCommand>& player = usedCommandsPool_.emplace_back(std::make_unique<RenderCommand>());
		return player.get();
	}

	RenderCommand* RenderCommandPool::add(GLShaderProgram* shaderProgram)
	{
		RenderCommand* newCommand = add();
		newCommand->material().setShaderProgram(shaderProgram);

		return newCommand;
	}

	RenderCommand* RenderCommandPool::retrieve(GLShaderProgram* shaderProgram)
	{
		RenderCommand* retrievedCommand = nullptr;

		for (unsigned int i = 0; i < freeCommandsPool_.size(); i++) {
			const unsigned int poolSize = (unsigned int)freeCommandsPool_.size();
			std::unique_ptr<RenderCommand>& command = freeCommandsPool_[i];
			if (command && command->material().shaderProgram() == shaderProgram) {
				retrievedCommand = command.get();
				usedCommandsPool_.push_back(std::move(command));
				command = std::move(freeCommandsPool_[poolSize - 1]);
				freeCommandsPool_.pop_back();
				break;
			}
		}

		if (retrievedCommand)
			RenderStatistics::addCommandPoolRetrieval();

		return retrievedCommand;
	}

	RenderCommand* RenderCommandPool::retrieveOrAdd(GLShaderProgram* shaderProgram, bool& commandAdded)
	{
		RenderCommand* retrievedCommand = retrieve(shaderProgram);

		commandAdded = false;
		if (retrievedCommand == nullptr) {
			retrievedCommand = add(shaderProgram);
			commandAdded = true;
		}

		return retrievedCommand;
	}

	void RenderCommandPool::reset()
	{
		RenderStatistics::gatherCommandPoolStatistics((unsigned int)usedCommandsPool_.size(), (unsigned int)freeCommandsPool_.size());

		for (std::unique_ptr<RenderCommand>& command : usedCommandsPool_)
			freeCommandsPool_.push_back(std::move(command));
		usedCommandsPool_.clear();
	}

}
