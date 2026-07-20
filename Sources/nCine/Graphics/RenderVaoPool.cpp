#include "RenderVaoPool.h"
#include "RenderStatistics.h"
#include "RHI/Rhi.h"
#include "../Base/Algorithms.h"
#include "../../Main.h"

namespace nCine
{
	RenderVaoPool::RenderVaoPool(std::uint32_t vaoPoolSize)
	{
		vaoPool_.reserve(vaoPoolSize);

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
		for (VaoBinding& binding : vaoPool_) {
			if (binding.fingerprint == fingerprint && binding.format == vertexFormat) {
				vaoFound = true;
				const bool bindChanged = binding.object->Bind();
				const std::uint32_t iboHandle = vertexFormat.GetIbo() ? vertexFormat.GetIbo()->GetGLHandle() : 0;
				if (bindChanged) {
					if (RHI::Debug::IsAvailable()) {
						InsertGLDebugMessage(binding);
					}
					// Binding a VAO changes the current bound element array buffer
					RHI::Buffer::SetBoundHandle(std::uint32_t(BufferTarget::Index), iboHandle);
				} else {
					// The VAO was already bound but it is not known if the bound element array buffer changed in the meantime
					RHI::Buffer::BindHandle(std::uint32_t(BufferTarget::Index), iboHandle);
				}
				binding.lastBindIndex = ++bindIndex_;
#if defined(NCINE_PROFILING)
				RenderStatistics::AddVaoPoolBinding();
#endif
				break;
			}
		}

		if (!vaoFound) {
			std::uint32_t index = 0;
			if (vaoPool_.size() < vaoPool_.capacity()) {
				auto& item = vaoPool_.emplace_back();
				item.object = std::make_unique<RHI::VertexArray>();
				index = std::uint32_t(vaoPool_.size() - 1);
#if defined(DEATH_DEBUG)
				if (RHI::Debug::IsAvailable()) {
					std::size_t length = formatInto(debugString, "Created and defined VAO 0x{:x} ({})", std::uintptr_t(vaoPool_[index].object.get()), index);
					RHI::Debug::MessageInsert({ debugString, length });

					length = formatInto(debugString, "VAO_#{}", index);
					vaoPool_.back().object->SetObjectLabel({ debugString, length });
				}
#endif
			} else {
				// Find the least recently used VAO
				std::uint64_t lruBindIndex = vaoPool_[0].lastBindIndex;
				for (std::uint32_t i = 1; i < vaoPool_.size(); i++) {
					if (vaoPool_[i].lastBindIndex < lruBindIndex) {
						index = i;
						lruBindIndex = vaoPool_[i].lastBindIndex;
					}
				}

#if defined(DEATH_DEBUG)
				std::size_t length = formatInto(debugString, "Reuse and define VAO 0x{:x} ({})", std::uintptr_t(vaoPool_[index].object.get()), index);
				RHI::Debug::MessageInsert({ debugString, length });
#endif
#if defined(NCINE_PROFILING)
				RenderStatistics::AddVaoPoolReuse();
#endif
			}

			const bool bindChanged = vaoPool_[index].object->Bind();
			DEATH_ASSERT(bindChanged || vaoPool_.size() == 1);
			// Binding a VAO changes the current bound element array buffer
			const std::uint32_t oldIboHandle = vaoPool_[index].format.GetIbo() ? vaoPool_[index].format.GetIbo()->GetGLHandle() : 0;
			RHI::Buffer::SetBoundHandle(std::uint32_t(BufferTarget::Index), oldIboHandle);
			vaoPool_[index].format = vertexFormat;
			vaoPool_[index].fingerprint = fingerprint;
			vaoPool_[index].format.Define();
			vaoPool_[index].lastBindIndex = ++bindIndex_;
#if defined(NCINE_PROFILING)
			RenderStatistics::AddVaoPoolBinding();
#endif
		}

#if defined(NCINE_PROFILING)
		RenderStatistics::GatherVaoPoolStatistics(std::uint32_t(vaoPool_.size()), std::uint32_t(vaoPool_.capacity()));
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
