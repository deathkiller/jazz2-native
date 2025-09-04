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

	RenderCommand* RenderCommandPool::Add()
	{
		return usedCommandsPool_.emplace_back(std::make_unique<RenderCommand>()).get();
	}

	RenderCommand* RenderCommandPool::Add(GLShaderProgram* shaderProgram)
	{
		RenderCommand* newCommand = Add();
		newCommand->GetMaterial().SetShaderProgram(shaderProgram);
		return newCommand;
	}

	RenderCommand* RenderCommandPool::Retrieve(GLShaderProgram* shaderProgram)
	{
		RenderCommand* retrievedCommand = nullptr;

		for (std::uint32_t i = 0; i < freeCommandsPool_.size(); i++) {
			std::uint32_t poolSize = std::uint32_t(freeCommandsPool_.size());
			std::unique_ptr<RenderCommand>& command = freeCommandsPool_[i];
			if (command && command->GetMaterial().GetShaderProgram() == shaderProgram) {
				retrievedCommand = command.get();
				usedCommandsPool_.push_back(std::move(command));
				command = std::move(freeCommandsPool_[poolSize - 1]);
				freeCommandsPool_.pop_back();
				break;
			}
		}

#if defined(NCINE_PROFILING)
		if (retrievedCommand) {
			RenderStatistics::AddCommandPoolRetrieval();
		}
#endif
		return retrievedCommand;
	}

	RenderCommand* RenderCommandPool::RetrieveOrAdd(GLShaderProgram* shaderProgram, bool& commandAdded)
	{
		RenderCommand* retrievedCommand = Retrieve(shaderProgram);

		commandAdded = false;
		if (retrievedCommand == nullptr) {
			retrievedCommand = Add(shaderProgram);
			commandAdded = true;
		}

		return retrievedCommand;
	}

	void RenderCommandPool::Reset()
	{
#if defined(NCINE_PROFILING)
		RenderStatistics::GatherCommandPoolStatistics(std::uint32_t(usedCommandsPool_.size()), std::uint32_t(freeCommandsPool_.size()));
#endif
		for (std::unique_ptr<RenderCommand>& command : usedCommandsPool_) {
			freeCommandsPool_.push_back(std::move(command));
		}
		usedCommandsPool_.clear();
	}
}
