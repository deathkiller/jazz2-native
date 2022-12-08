#include "RenderVaoPool.h"
#include "RenderStatistics.h"
#include "GL/GLBufferObject.h"
#include "GL/GLVertexArrayObject.h"
#include "GL/GLDebug.h"
#include "../Base/Algorithms.h"
#include "../../Common.h"

namespace nCine
{
#if defined(NCINE_DEBUG)
	namespace
	{
		/// The string used to output OpenGL debug group information
		static char debugString[128];
	}
#endif

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	RenderVaoPool::RenderVaoPool(unsigned int vaoPoolSize)
	{
		vaoPool_.reserve(vaoPoolSize);

		// Start with a VAO bound to the OpenGL context
		GLVertexFormat format;
		bindVao(format);
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void RenderVaoPool::bindVao(const GLVertexFormat& vertexFormat)
	{
		bool vaoFound = false;
		for (VaoBinding& binding : vaoPool_) {
			if (binding.format == vertexFormat) {
				vaoFound = true;
				const bool bindChanged = binding.object->bind();
				const GLuint iboHandle = vertexFormat.ibo() ? vertexFormat.ibo()->glHandle() : 0;
				if (bindChanged) {
					if (GLDebug::isAvailable()) {
						insertGLDebugMessage(binding);
					}
					// Binding a VAO changes the current bound element array buffer
					GLBufferObject::setBoundHandle(GL_ELEMENT_ARRAY_BUFFER, iboHandle);
				} else {
					// The VAO was already bound but it is not known if the bound element array buffer changed in the meantime
					GLBufferObject::bindHandle(GL_ELEMENT_ARRAY_BUFFER, iboHandle);
				}
				binding.lastBindTime = TimeStamp::now();
#if defined(NCINE_PROFILING)
				RenderStatistics::addVaoPoolBinding();
#endif
				break;
			}
		}

		if (!vaoFound) {
			unsigned int index = 0;
			if (vaoPool_.size() < vaoPool_.capacity()) {
				auto& item = vaoPool_.emplace_back();
				item.object = std::make_unique<GLVertexArrayObject>();
				index = (unsigned int)vaoPool_.size() - 1;
#if defined(NCINE_DEBUG)
				/*if (GLDebug::isAvailable()) {
					debugString.format("Created and defined VAO 0x%lx (%u)", uintptr_t(vaoPool_[index].object.get()), index);
					GLDebug::messageInsert(debugString.data());

					debugString.format("VAO_#%d", index);
					vaoPool_.back().object->setObjectLabel(debugString.data());
				}*/
#endif
			} else {
				// Find the least recently used VAO
				TimeStamp time = vaoPool_[0].lastBindTime;
				for (unsigned int i = 1; i < vaoPool_.size(); i++) {
					if (vaoPool_[i].lastBindTime < time) {
						index = i;
						time = vaoPool_[i].lastBindTime;
					}
				}

#if defined(NCINE_DEBUG)
				formatString(debugString, sizeof(debugString), "Reuse and define VAO 0x%lx (%u)", uintptr_t(vaoPool_[index].object.get()), index);
				GLDebug::messageInsert(debugString);
#endif
#if defined(NCINE_PROFILING)
				RenderStatistics::addVaoPoolReuse();
#endif
			}

			const bool bindChanged = vaoPool_[index].object->bind();
			ASSERT(bindChanged || vaoPool_.size() == 1);
			// Binding a VAO changes the current bound element array buffer
			const GLuint oldIboHandle = vaoPool_[index].format.ibo() ? vaoPool_[index].format.ibo()->glHandle() : 0;
			GLBufferObject::setBoundHandle(GL_ELEMENT_ARRAY_BUFFER, oldIboHandle);
			vaoPool_[index].format = vertexFormat;
			vaoPool_[index].format.define();
			vaoPool_[index].lastBindTime = TimeStamp::now();
#if defined(NCINE_PROFILING)
			RenderStatistics::addVaoPoolBinding();
#endif
		}

#if defined(NCINE_PROFILING)
		RenderStatistics::gatherVaoPoolStatistics((unsigned int)vaoPool_.size(), (unsigned int)vaoPool_.capacity());
#endif
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void RenderVaoPool::insertGLDebugMessage(const VaoBinding& binding)
	{
#if defined(NCINE_DEBUG)
		formatString(debugString, sizeof(debugString), "Bind VAO 0x%lx (", uintptr_t(binding.object.get()));

		// TODO: GLDebug
		/*bool firstVbo = true;
		for (unsigned int i = 0; i < binding.format.numAttributes(); i++)
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
			debugString.formatAppend(", ibo: 0x%lx", uintptr_t(binding.format.ibo()));
		debugString.formatAppend(")");*/

		GLDebug::messageInsert(debugString);
#endif
	}
}
