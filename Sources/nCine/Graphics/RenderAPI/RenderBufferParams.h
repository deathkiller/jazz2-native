#pragma once

#include <cstdint>

// Forward-declare the buffer object type.
// For the GL backend a GLBufferObject is used; the circular-include chain
//   RenderBuffersManager.h -> RHI.h -> RHI_GL.h -> GLShaderUniformBlocks.h -> RenderBuffersManager.h
// means we cannot include RHI.h here for GL — we use a forward declaration instead.
// For all other backends Rhi::Buffer is a concrete class; RHI.h is guaranteed to be
// already included before this header in that case (via RenderBuffersManager.h -> RHI.h).
#if defined(RHI_BACKEND_GL)
namespace nCine { class GLBufferObject; }
#else
namespace Rhi { class Buffer; }
#endif

namespace nCine
{
	/// Lightweight buffer allocation record returned by RenderBuffersManager::AcquireMemory().
	struct BufferParams
	{
		BufferParams()
			: object(nullptr), size(0), offset(0), mapBase(nullptr) {}

#if defined(RHI_BACKEND_GL)
		GLBufferObject* object;
#else
		Rhi::Buffer*    object;
#endif
		std::uint32_t   size;
		std::uint32_t   offset;
		std::uint8_t*   mapBase;
	};
}
