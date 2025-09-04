#pragma once

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
		/// Adds a new command with the specified OpenGL shader program to the used pool and returns its pointer
		/*! \note It is usually called after checking that the retrieve method fails */
		RenderCommand* Add(GLShaderProgram* shaderProgram);

		/// Retrieves a command with the specified OpenGL shader program
		RenderCommand* Retrieve(GLShaderProgram* shaderProgram);

		/// Retrieves (or adds) a command with the specified OpenGL shader program
		RenderCommand* RetrieveOrAdd(GLShaderProgram* shaderProgram, bool& commandAdded);

		/// Releases all used commands and returnsi them in the free array
		void Reset();

	private:
		SmallVector<std::unique_ptr<RenderCommand>, 0> freeCommandsPool_;
		SmallVector<std::unique_ptr<RenderCommand>, 0> usedCommandsPool_;
	};

}
