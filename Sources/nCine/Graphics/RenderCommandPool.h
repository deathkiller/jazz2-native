#pragma once

#include "Material.h"

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class RenderCommand;

	/**
		@brief Pool of reusable render commands
		
		Keeps allocated @ref RenderCommand instances alive between frames so they can be recycled
		instead of reallocated. Commands move from the free pool to the used pool when handed out,
		and @ref Reset() returns them all to the free pool at the end of a frame.
	*/
	class RenderCommandPool
	{
	public:
		explicit RenderCommandPool(std::uint32_t poolSize);
		~RenderCommandPool();

		/**
		 * @brief Adds a new command to the used pool and returns it
		 *
		 * @note Usually called only after @ref Retrieve() fails to find a matching command.
		 */
		RenderCommand* Add();
		/**
		 * @brief Adds a new command bound to the specified shader program and returns it
		 *
		 * @note Usually called only after @ref Retrieve() fails to find a matching command.
		 */
		RenderCommand* Add(GLShaderProgram* shaderProgram);

		/** @brief Returns an unused command bound to the specified shader program, or `nullptr` if none is available */
		RenderCommand* Retrieve(GLShaderProgram* shaderProgram);

		/** @brief Returns a command bound to the specified shader program, adding a new one if none can be reused */
		RenderCommand* RetrieveOrAdd(GLShaderProgram* shaderProgram, bool& commandAdded);

		/** @brief Releases all used commands back to the free pool */
		void Reset();

	private:
		SmallVector<std::unique_ptr<RenderCommand>, 0> freeCommandsPool_;
		SmallVector<std::unique_ptr<RenderCommand>, 0> usedCommandsPool_;
	};

}
