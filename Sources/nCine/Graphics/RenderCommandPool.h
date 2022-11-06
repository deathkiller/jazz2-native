#pragma once

#include "Material.h"

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class RenderCommand;

	/// The class that creates and handles the pool of render commands
	class RenderCommandPool
	{
	public:
		explicit RenderCommandPool(unsigned int poolSize);
		~RenderCommandPool();

		// Adds a new command to the used pool and returns its pointer
		/*! \note It is usually called after checking that the retrieve method fails */
		RenderCommand* add();
		/// Adds a new command with the specified OpenGL shader program to the used pool and returns its pointer
		/*! \note It is usually called after checking that the retrieve method fails */
		RenderCommand* add(GLShaderProgram* shaderProgram);

		/// Retrieves a command with the specified OpenGL shader program
		RenderCommand* retrieve(GLShaderProgram* shaderProgram);

		/// Retrieves (or adds) a command with the specified OpenGL shader program
		RenderCommand* retrieveOrAdd(GLShaderProgram* shaderProgram, bool& commandAdded);

		/// Releases all used commands and returnsi them in the free array
		void reset();

	private:
		SmallVector<std::unique_ptr<RenderCommand>, 0> freeCommandsPool_;
		SmallVector<std::unique_ptr<RenderCommand>, 0> usedCommandsPool_;
	};

}
