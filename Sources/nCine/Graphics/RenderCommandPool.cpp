#include "RenderCommandPool.h"
#include "RenderCommand.h"
#include "RenderStatistics.h"

namespace nCine
{
	RenderCommandPool::RenderCommandPool(std::uint32_t poolSize)
	{
		_freeCommandsPool.reserve(poolSize);
		_usedCommandsPool.reserve(poolSize);
	}

	RenderCommandPool::~RenderCommandPool() = default;

	RenderCommand* RenderCommandPool::Add()
	{
		return _usedCommandsPool.emplace_back(std::make_unique<RenderCommand>()).get();
	}

	RenderCommand* RenderCommandPool::Add(RHI::ShaderProgram* shaderProgram)
	{
		RenderCommand* newCommand = Add();
		newCommand->GetMaterial().SetShaderProgram(shaderProgram);
		return newCommand;
	}

	RenderCommand* RenderCommandPool::Retrieve(RHI::ShaderProgram* shaderProgram)
	{
		RenderCommand* retrievedCommand = nullptr;

		for (std::uint32_t i = 0; i < _freeCommandsPool.size(); i++) {
			std::uint32_t poolSize = std::uint32_t(_freeCommandsPool.size());
			std::unique_ptr<RenderCommand>& command = _freeCommandsPool[i];
			if (command && command->GetMaterial().GetShaderProgram() == shaderProgram) {
				retrievedCommand = command.get();
				_usedCommandsPool.push_back(std::move(command));
				command = std::move(_freeCommandsPool[poolSize - 1]);
				_freeCommandsPool.pop_back();
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

	RenderCommand* RenderCommandPool::RetrieveOrAdd(RHI::ShaderProgram* shaderProgram, bool& commandAdded)
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
		RenderStatistics::GatherCommandPoolStatistics(std::uint32_t(_usedCommandsPool.size()), std::uint32_t(_freeCommandsPool.size()));
#endif
		for (std::unique_ptr<RenderCommand>& command : _usedCommandsPool) {
			_freeCommandsPool.push_back(std::move(command));
		}
		_usedCommandsPool.clear();
	}
}
