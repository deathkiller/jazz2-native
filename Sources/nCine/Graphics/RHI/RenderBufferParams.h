#pragma once

#include <cstdint>

// Forward-declare the buffer object type
namespace nCine::RHI
{
	class Buffer;
}

namespace nCine
{
	/// Lightweight buffer allocation record returned by RenderBuffersManager::AcquireMemory().
	struct BufferParams
	{
		BufferParams()
			: object(nullptr), size(0), offset(0), mapBase(nullptr) {}

		RHI::Buffer* object;
		std::uint32_t size;
		std::uint32_t offset;
		std::uint8_t* mapBase;
	};
}
