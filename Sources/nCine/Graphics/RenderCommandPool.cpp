#include "RenderCommandPool.h"
#include "RenderCommand.h"
#include "RenderStatistics.h"

namespace nCine
{
	RenderCommandPool::RenderCommandPool(std::uint32_t poolSize)
	{
		freeCommandsPool_.reserve(poolSize);
		usedCommandsPool_.reserve(poolSize);
	}

	RenderCommandPool::~RenderCommandPool() = default;

	RenderCommand* RenderCommandPool::add()
	{
		return usedCommandsPool_.emplace_back(std::make_unique<RenderCommand>()).get();
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

		for (std::uint32_t i = 0; i < freeCommandsPool_.size(); i++) {
			std::uint32_t poolSize = std::uint32_t(freeCommandsPool_.size());
			std::unique_ptr<RenderCommand>& command = freeCommandsPool_[i];
			if (command && command->material().shaderProgram() == shaderProgram) {
				retrievedCommand = command.get();
				usedCommandsPool_.push_back(std::move(command));
				command = std::move(freeCommandsPool_[poolSize - 1]);
				freeCommandsPool_.pop_back();
				break;
			}
		}

#if defined(NCINE_PROFILING)
		if (retrievedCommand) {
			RenderStatistics::addCommandPoolRetrieval();
		}
#endif
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
#if defined(NCINE_PROFILING)
		RenderStatistics::gatherCommandPoolStatistics(std::uint32_t(usedCommandsPool_.size()), std::uint32_t(freeCommandsPool_.size()));
#endif
		for (std::unique_ptr<RenderCommand>& command : usedCommandsPool_) {
			freeCommandsPool_.push_back(std::move(command));
		}
		usedCommandsPool_.clear();
	}
}
