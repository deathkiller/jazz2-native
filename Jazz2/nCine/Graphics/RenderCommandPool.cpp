#include "RenderCommandPool.h"
#include "RenderCommand.h"
#include "RenderStatistics.h"

namespace nCine {

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

	RenderCommand* RenderCommandPool::add(Material::ShaderProgramType shaderProgramType)
	{
		RenderCommand* newCommand = add();
		newCommand->material().setShaderProgramType(shaderProgramType);

		return newCommand;
	}

	RenderCommand* RenderCommandPool::retrieve(Material::ShaderProgramType shaderProgramType)
	{
		RenderCommand* retrievedCommand = nullptr;

		for (unsigned int i = 0; i < freeCommandsPool_.size(); i++) {
			const unsigned int poolSize = freeCommandsPool_.size();
			std::unique_ptr<RenderCommand>& command = freeCommandsPool_[i];
			if (command && command->material().shaderProgramType() == shaderProgramType) {
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

	RenderCommand* RenderCommandPool::retrieve(GLShaderProgram* shaderProgram)
	{
		RenderCommand* retrievedCommand = nullptr;

		for (unsigned int i = 0; i < freeCommandsPool_.size(); i++) {
			const unsigned int poolSize = freeCommandsPool_.size();
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

	RenderCommand* RenderCommandPool::retrieveOrAdd(Material::ShaderProgramType shaderProgramType, bool& commandAdded)
	{
		RenderCommand* retrievedCommand = retrieve(shaderProgramType);

		commandAdded = false;
		if (retrievedCommand == nullptr) {
			retrievedCommand = add(shaderProgramType);
			commandAdded = true;
		}

		return retrievedCommand;
	}

	void RenderCommandPool::reset()
	{
		RenderStatistics::gatherCommandPoolStatistics(usedCommandsPool_.size(), freeCommandsPool_.size());

		for (std::unique_ptr<RenderCommand>& command : usedCommandsPool_)
			freeCommandsPool_.push_back(std::move(command));
		usedCommandsPool_.clear();
	}

}
