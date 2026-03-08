#pragma once

#include <cstdint>

namespace nCine
{
	class GLBufferObject;

	/// Lightweight buffer allocation record returned by RenderBuffersManager::AcquireMemory().
	/// Defined here (outside RenderBuffersManager) so that headers needing only this struct
	/// can avoid pulling in RHI.h and breaking the circular include chain:
	///   RenderBuffersManager.h -> RHI.h -> RHI_GL.h -> GLShaderUniformBlocks.h -> RenderBuffersManager.h
	struct BufferParams
	{
		BufferParams()
			: object(nullptr), size(0), offset(0), mapBase(nullptr) {}

		GLBufferObject* object;
		std::uint32_t   size;
		std::uint32_t   offset;
		std::uint8_t*   mapBase;
	};
}
