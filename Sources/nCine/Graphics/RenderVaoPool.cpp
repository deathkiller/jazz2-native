#include "RenderVaoPool.h"
#include "RenderStatistics.h"
#include "RHI/Rhi.h"
#include "../Base/Algorithms.h"
#include "../../Main.h"

namespace nCine
{
	namespace
	{
		// Stands for "the element array buffer currently bound is not known", matching the value the GL
		// backend writes into its own cache in InvalidateCachedBindings(). No buffer handle can equal it, so
		// the next bind through the cache always reaches the driver.
		constexpr std::uint32_t UnknownBufferHandle = ~std::uint32_t(0);
	}

	RenderVaoPool::RenderVaoPool(std::uint32_t vaoPoolSize)
	{
		_vaoPool.reserve(vaoPoolSize);

		// Start with a VAO bound to the OpenGL context
		RHI::VertexFormat format;
		BindVao(format);
	}

	void RenderVaoPool::BindVao(const RHI::VertexFormat& vertexFormat)
	{
#if defined(DEATH_DEBUG)
		char debugString[128];
#endif

		// The fingerprint rejects mismatches with a single comparison, the deep format
		// comparison only runs on a fingerprint match to rule out hash collisions
		const std::uint64_t fingerprint = vertexFormat.CalculateFingerprint();

		bool vaoFound = false;
		for (VaoBinding& binding : _vaoPool) {
			if (binding.fingerprint == fingerprint && binding.format == vertexFormat) {
				vaoFound = true;
				const bool bindChanged = binding.object->Bind();
				const std::uint32_t iboHandle = vertexFormat.GetIbo() ? vertexFormat.GetIbo()->GetGLHandle() : 0;
				if (bindChanged && RHI::Debug::IsAvailable()) {
					InsertGLDebugMessage(binding);
				}
				// Binding a VAO restores the element array buffer recorded IN THAT VAO, and that is not
				// necessarily still this format's index buffer: the element array binding is VAO state, so any
				// bind of one issued while this VAO was current rewrote what it records - which is exactly what
				// the buffers manager does whenever it creates, maps or flushes an index buffer, and it does
				// that between draws as soon as a frame streams more indices than the buffers in hand can hold.
				// Recording the format's handle as bound would then leave the draw pulling its indices out of
				// whichever buffer was touched last; dropping the cached handle first makes the bind below
				// reach the driver, which also repairs the VAO that the stale handle was recorded in.
				if (bindChanged) {
					RHI::Buffer::SetBoundHandle(std::uint32_t(BufferTarget::Index), UnknownBufferHandle);
				}
				RHI::Buffer::BindHandle(std::uint32_t(BufferTarget::Index), iboHandle);
				binding.lastBindIndex = ++_bindIndex;
#if defined(NCINE_PROFILING)
				RenderStatistics::AddVaoPoolBinding();
#endif
				break;
			}
		}

		if (!vaoFound) {
			std::uint32_t index = 0;
			if (_vaoPool.size() < _vaoPool.capacity()) {
				auto& item = _vaoPool.emplace_back();
				item.object = std::make_unique<RHI::VertexArray>();
				index = std::uint32_t(_vaoPool.size() - 1);
#if defined(DEATH_DEBUG)
				if (RHI::Debug::IsAvailable()) {
					std::size_t length = formatInto(debugString, "Created and defined VAO 0x{:x} ({})", std::uintptr_t(_vaoPool[index].object.get()), index);
					RHI::Debug::MessageInsert({ debugString, length });

					length = formatInto(debugString, "VAO_#{}", index);
					_vaoPool.back().object->SetObjectLabel({ debugString, length });
				}
#endif
			} else {
				// Find the least recently used VAO
				std::uint64_t lruBindIndex = _vaoPool[0].lastBindIndex;
				for (std::uint32_t i = 1; i < _vaoPool.size(); i++) {
					if (_vaoPool[i].lastBindIndex < lruBindIndex) {
						index = i;
						lruBindIndex = _vaoPool[i].lastBindIndex;
					}
				}

#if defined(DEATH_DEBUG)
				std::size_t length = formatInto(debugString, "Reuse and define VAO 0x{:x} ({})", std::uintptr_t(_vaoPool[index].object.get()), index);
				RHI::Debug::MessageInsert({ debugString, length });
#endif
#if defined(NCINE_PROFILING)
				RenderStatistics::AddVaoPoolReuse();
#endif
			}

			const bool bindChanged = _vaoPool[index].object->Bind();
			DEATH_ASSERT(bindChanged || _vaoPool.size() == 1);
			// Binding a VAO restores the element array buffer recorded in it, which is not known here for the
			// reason given above - and the format being replaced cannot be asked for it either, as the buffer
			// it names may be gone by now. Dropping the cached handle makes Define() below bind the new index
			// buffer for real; a format that has none unbinds instead, so the VAO ends up recording none.
			RHI::Buffer::SetBoundHandle(std::uint32_t(BufferTarget::Index), UnknownBufferHandle);
			_vaoPool[index].format = vertexFormat;
			_vaoPool[index].fingerprint = fingerprint;
			_vaoPool[index].format.Define();
			if (vertexFormat.GetIbo() == nullptr) {
				RHI::Buffer::BindHandle(std::uint32_t(BufferTarget::Index), 0);
			}
			_vaoPool[index].lastBindIndex = ++_bindIndex;
#if defined(NCINE_PROFILING)
			RenderStatistics::AddVaoPoolBinding();
#endif
		}

#if defined(NCINE_PROFILING)
		RenderStatistics::GatherVaoPoolStatistics(std::uint32_t(_vaoPool.size()), std::uint32_t(_vaoPool.capacity()));
#endif
	}

	void RenderVaoPool::InsertGLDebugMessage(const VaoBinding& binding)
	{
#if defined(DEATH_DEBUG)
		static char debugString[128];
		std::size_t length = formatInto(debugString, "Bind VAO 0x{:x}", std::uintptr_t(binding.object.get()));

		// TODO: RHI::Debug
		/*bool firstVbo = true;
		for (std::uint32_t i = 0; i < binding.format.numAttributes(); i++)
		{
			if (binding.format[i].isEnabled() && binding.format[i].vbo() != nullptr)
			{
				if (firstVbo == false)
					debugString.formatAppend(", ");
				debugString.formatAppend("vbo #%u: 0x%lx", i, uintptr_t(binding.format[i].vbo()));
				firstVbo = false;
			}
		}
		if (binding.format.ibo() != nullptr)
			debugString.formatAppend(", ibo: 0x%lx", std::uintptr_t(binding.format.ibo()));
		debugString.formatAppend(")");*/

		RHI::Debug::MessageInsert({ debugString, length });
#endif
	}
}
