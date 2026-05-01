#include "RenderVaoPool.h"

#if defined(RHI_CAP_VAO)

#include "RenderStatistics.h"
#include "RHI/GL/Buffer.h"
#include "RHI/GL/Debug.h"
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

		bool vaoFound = false;
		for (VaoBinding& binding : vaoPool_) {
			if (binding.format == vertexFormat) {
				vaoFound = true;
				const bool bindChanged = binding.object->Bind();
				const GLuint iboHandle = vertexFormat.GetIbo() ? vertexFormat.GetIbo()->GetGLHandle() : 0;
				if (bindChanged) {
					if (RHI::Debug::IsAvailable()) {
						InsertGLDebugMessage(binding);
					}
					// Binding a VAO changes the current bound element array buffer
					RHI::Buffer::SetBoundHandle(GL_ELEMENT_ARRAY_BUFFER, iboHandle);
				} else {
					// The VAO was already bound but it is not known if the bound element array buffer changed in the meantime
					RHI::Buffer::BindHandle(GL_ELEMENT_ARRAY_BUFFER, iboHandle);
				}
				binding.lastBindTime = TimeStamp::now();
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
				item.object = std::make_unique<RHI::VertexArrayObject>();
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
				TimeStamp time = vaoPool_[0].lastBindTime;
				for (std::uint32_t i = 1; i < vaoPool_.size(); i++) {
					if (vaoPool_[i].lastBindTime < time) {
						index = i;
						time = vaoPool_[i].lastBindTime;
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
			const GLuint oldIboHandle = vaoPool_[index].format.GetIbo() ? vaoPool_[index].format.GetIbo()->GetGLHandle() : 0;
			RHI::Buffer::SetBoundHandle(GL_ELEMENT_ARRAY_BUFFER, oldIboHandle);
			vaoPool_[index].format = vertexFormat;
			vaoPool_[index].format.Define();
			vaoPool_[index].lastBindTime = TimeStamp::now();
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

		// TODO: GLDebug
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

#endif