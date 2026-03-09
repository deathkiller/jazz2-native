#pragma once

#include "RHI/RHI.h"
#include "Material.h"

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class RenderCommand;

	/// Creates and handles the pool of render commands
	class RenderCommandPool
	{
	public:
		explicit RenderCommandPool(std::uint32_t poolSize);
		~RenderCommandPool();

		// Adds a new command to the used pool and returns its pointer
		/*! \note It is usually called after checking that the retrieve method fails */
		RenderCommand* Add();
		/// Adds a new command with the specified shader program to the used pool and returns its pointer
		/*! \note It is usually called after checking that the retrieve method fails */
		RenderCommand* Add(RHI::ShaderProgram* shaderProgram);

		/// Retrieves a command with the specified shader program
		RenderCommand* Retrieve(RHI::ShaderProgram* shaderProgram);

		/// Retrieves (or adds) a command with the specified shader program
		RenderCommand* RetrieveOrAdd(RHI::ShaderProgram* shaderProgram, bool& commandAdded);

		/// Releases all used commands and returnsi them in the free array
		void Reset();

	private:
		SmallVector<std::unique_ptr<RenderCommand>, 0> freeCommandsPool_;
		SmallVector<std::unique_ptr<RenderCommand>, 0> usedCommandsPool_;
	};

}
