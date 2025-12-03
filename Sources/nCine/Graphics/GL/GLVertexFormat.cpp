#include "GLVertexFormat.h"
#include "GLBufferObject.h"
#include "../IGfxCapabilities.h"
#include "../../ServiceLocator.h"
#include "../../../Main.h"

namespace nCine
{
	GLVertexFormat::Attribute::Attribute()
		: enabled_(false), vbo_(nullptr), index_(0), size_(-1), type_(GL_FLOAT), stride_(0), pointer_(nullptr), baseOffset_(0)
	{
	}

	GLVertexFormat::GLVertexFormat()
		: ibo_(nullptr), attributes_(MaxAttributes)
	{
	}

	bool GLVertexFormat::Attribute::operator==(const Attribute& other) const
	{
		return ((other.enabled_ == false && enabled_ == false) ||
				((other.enabled_ == true && enabled_ == true) &&
					(other.vbo_ && vbo_ && other.vbo_->GetGLHandle() == vbo_->GetGLHandle()) &&
					other.index_ == index_ &&
					other.size_ == size_ &&
					other.type_ == type_ &&
					other.normalized_ == normalized_ &&
					other.stride_ == stride_ &&
					other.pointer_ == pointer_ &&
					other.baseOffset_ == baseOffset_));
	}

	bool GLVertexFormat::Attribute::operator!=(const Attribute& other) const
	{
		return !operator==(other);
	}

	void GLVertexFormat::Attribute::Init(std::uint32_t index, GLint size, GLenum type)
	{
		enabled_ = true;
		vbo_ = nullptr;
		index_ = index;
		size_ = size;
		type_ = type;
		normalized_ = GL_FALSE;
		stride_ = 0;
		pointer_ = nullptr;
		baseOffset_ = 0;
	}

	void GLVertexFormat::Attribute::SetVboParameters(GLsizei stride, const GLvoid* pointer)
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM)) && (!defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_1))
		static const std::int32_t MaxVertexAttribStride = theServiceLocator().GetGfxCapabilities().GetValue(IGfxCapabilities::GLIntValues::MAX_VERTEX_ATTRIB_STRIDE);

		if (stride > MaxVertexAttribStride) {
			stride_ = MaxVertexAttribStride;
			LOGW("Vertex attribute stride ({}) is bigger than the maximum value supported ({})", stride, MaxVertexAttribStride);
		} else
#endif
		{
			stride_ = stride;
		}

		pointer_ = pointer;
	}

	void GLVertexFormat::Define()
	{
#if defined(WITH_OPENGL2)
		// On OpenGL 2.x, we need to disable previously enabled attributes since there's no VAO support
		static std::uint32_t previouslyEnabledMask = 0;
		std::uint32_t currentEnabledMask = 0;
		
		// Build mask of currently enabled attributes
		for (std::uint32_t i = 0; i < MaxAttributes; i++) {
			if (attributes_[i].enabled_) {
				currentEnabledMask |= (1 << i);
			}
		}
		
		// Disable attributes that were previously enabled but are not in current format
		std::uint32_t toDisable = previouslyEnabledMask & ~currentEnabledMask;
		for (std::uint32_t i = 0; i < MaxAttributes; i++) {
			if (toDisable & (1 << i)) {
				glDisableVertexAttribArray(i);
			}
		}
		
		previouslyEnabledMask = currentEnabledMask;
#endif

		for (std::uint32_t i = 0; i < MaxAttributes; i++) {
			if (attributes_[i].enabled_) {
				attributes_[i].vbo_->Bind();
				glEnableVertexAttribArray(attributes_[i].index_);

#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
				const GLubyte* initialPointer = reinterpret_cast<const GLubyte*>(attributes_[i].pointer_);
				const GLvoid* pointer = reinterpret_cast<const GLvoid*>(initialPointer + attributes_[i].baseOffset_);
#else
				const GLvoid* pointer = attributes_[i].pointer_;
#endif

				switch (attributes_[i].type_) {
					case GL_BYTE:
					case GL_UNSIGNED_BYTE:
					case GL_SHORT:
					case GL_UNSIGNED_SHORT:
					case GL_INT:
					case GL_UNSIGNED_INT:
#if defined(WITH_OPENGL2)
						// glVertexAttribIPointer is not available in OpenGL 2.x, fallback to regular pointer
						glVertexAttribPointer(attributes_[i].index_, attributes_[i].size_, attributes_[i].type_, attributes_[i].normalized_, attributes_[i].stride_, pointer);
#else
						if (attributes_[i].normalized_) {
							glVertexAttribPointer(attributes_[i].index_, attributes_[i].size_, attributes_[i].type_, GL_TRUE, attributes_[i].stride_, pointer);
						} else {
							glVertexAttribIPointer(attributes_[i].index_, attributes_[i].size_, attributes_[i].type_, attributes_[i].stride_, pointer);
						}
#endif
						break;
					default:
						glVertexAttribPointer(attributes_[i].index_, attributes_[i].size_, attributes_[i].type_, attributes_[i].normalized_, attributes_[i].stride_, pointer);
						break;
				}
			}
		}

		if (ibo_) {
			ibo_->Bind();
		}
	}

	void GLVertexFormat::Reset()
	{
		for (std::uint32_t i = 0; i < MaxAttributes; i++) {
			attributes_[i].enabled_ = false;
		}
		ibo_ = nullptr;
	}

	bool GLVertexFormat::operator==(const GLVertexFormat& other) const
	{
		bool areEqual = (other.ibo_ == ibo_ && other.attributes_.size() == attributes_.size());

		// If indices are the same then check attributes too
		if (areEqual) {
			for (std::uint32_t i = 0; i < attributes_.size(); i++) {
				if (other.attributes_[i] != attributes_[i]) {
					areEqual = false;
					break;
				}
			}
		}

		return areEqual;
	}

	bool GLVertexFormat::operator!=(const GLVertexFormat& other) const
	{
		return !operator==(other);
	}
}
