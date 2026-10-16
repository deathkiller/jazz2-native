#include "GLVertexFormat.h"
#include "GLBufferObject.h"
#include "../IRhiCapabilities.h"
#include "../../../ServiceLocator.h"
#include "../../../../Main.h"

namespace nCine::RHI::GL
{
	GLVertexFormat::Attribute::Attribute()
		: _vbo(nullptr), _vboId(0), _pointer(nullptr), _index(0), _size(-1), _type(GL_FLOAT), _stride(0), _baseOffset(0), _enabled(false), _normalized(GL_FALSE)
	{
	}

	GLVertexFormat::GLVertexFormat()
		: _ibo(nullptr), _iboId(0), _attributes(MaxAttributes)
	{
	}

	void GLVertexFormat::Attribute::setVbo(const GLBufferObject* vbo)
	{
		_vbo = vbo;
		_vboId = (vbo != nullptr ? vbo->GetUniqueId() : 0);
	}

	void GLVertexFormat::SetIbo(const GLBufferObject* ibo)
	{
		_ibo = ibo;
		_iboId = (ibo != nullptr ? ibo->GetUniqueId() : 0);
	}

	bool GLVertexFormat::Attribute::operator==(const Attribute& other) const
	{
		return ((other._enabled == false && _enabled == false) ||
				((other._enabled == true && _enabled == true) &&
					// By the identity of the buffer object, the pool keeps formats of objects that may be gone by now, and
					// a new one can get the address or the handle of one that was destroyed
					(other._vboId != 0 && other._vboId == _vboId) &&
					other._index == _index &&
					other._size == _size &&
					other._type == _type &&
					other._normalized == _normalized &&
					other._stride == _stride &&
					other._pointer == _pointer &&
					other._baseOffset == _baseOffset));
	}

	bool GLVertexFormat::Attribute::operator!=(const Attribute& other) const
	{
		return !operator==(other);
	}

	void GLVertexFormat::Attribute::Init(std::uint32_t index, GLint size, GLenum type)
	{
		_enabled = true;
		_vbo = nullptr;
		_vboId = 0;
		_index = index;
		_size = size;
		_type = type;
		_normalized = GL_FALSE;
		_stride = 0;
		_pointer = nullptr;
		_baseOffset = 0;
	}

	void GLVertexFormat::Attribute::SetVboParameters(GLsizei stride, const GLvoid* pointer)
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM)) && (defined(RHI_GL_PROFILE_CORE) || GL_ES_VERSION_3_1)
		static const std::int32_t MaxVertexAttribStride = theServiceLocator().GetRhiCapabilities().GetValue(IRhiCapabilities::IntValues::MaxVertexAttribStride);

		if (stride > MaxVertexAttribStride) {
			_stride = MaxVertexAttribStride;
			LOGW("Vertex attribute stride ({}) is bigger than the maximum value supported ({})", stride, MaxVertexAttribStride);
		} else
#endif
		{
			_stride = stride;
		}

		_pointer = pointer;
	}

	void GLVertexFormat::Define()
	{
		for (std::uint32_t i = 0; i < MaxAttributes; i++) {
			if (_attributes[i]._enabled) {
				_attributes[i]._vbo->Bind();
				glEnableVertexAttribArray(_attributes[i]._index);

#if (defined(RHI_GL_PROFILE_ES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
				const GLubyte* initialPointer = reinterpret_cast<const GLubyte*>(_attributes[i]._pointer);
				const GLvoid* pointer = reinterpret_cast<const GLvoid*>(initialPointer + _attributes[i]._baseOffset);
#else
				const GLvoid* pointer = _attributes[i]._pointer;
#endif

#if defined(RHI_GL_PROFILE_ES2)
				// ES2 has no integer vertex attributes (glVertexAttribIPointer is ES 3.0) and no GL_(UNSIGNED_)INT
				// attribute data type at all; the ESSL 100 emitter already declares every attribute float-typed and
				// the only integer-typed streams belong to the batched-mesh programs this profile never compiles
				glVertexAttribPointer(_attributes[i]._index, _attributes[i]._size, _attributes[i]._type, _attributes[i]._normalized, _attributes[i]._stride, pointer);
#else
				switch (_attributes[i]._type) {
					case GL_BYTE:
					case GL_UNSIGNED_BYTE:
					case GL_SHORT:
					case GL_UNSIGNED_SHORT:
					case GL_INT:
					case GL_UNSIGNED_INT:
						if (_attributes[i]._normalized) {
							glVertexAttribPointer(_attributes[i]._index, _attributes[i]._size, _attributes[i]._type, GL_TRUE, _attributes[i]._stride, pointer);
						} else {
							glVertexAttribIPointer(_attributes[i]._index, _attributes[i]._size, _attributes[i]._type, _attributes[i]._stride, pointer);
						}
						break;
					default:
						glVertexAttribPointer(_attributes[i]._index, _attributes[i]._size, _attributes[i]._type, _attributes[i]._normalized, _attributes[i]._stride, pointer);
						break;
				}
#endif
			}
		}

		if (_ibo) {
			_ibo->Bind();
		}
	}

	void GLVertexFormat::Reset()
	{
		for (std::uint32_t i = 0; i < MaxAttributes; i++) {
			_attributes[i]._enabled = false;
		}
		_ibo = nullptr;
		_iboId = 0;
	}

	namespace
	{
		inline std::uint64_t hashCombine(std::uint64_t h, std::uint64_t v)
		{
			// One splitmix64 step over the previous hash and the new value
			h += 0x9E3779B97F4A7C15ull + v;
			h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
			h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
			return h ^ (h >> 31);
		}
	}

	std::uint64_t GLVertexFormat::CalculateFingerprint() const
	{
		// Mirrors operator==(): only enabled attributes and the IBO identity contribute
		std::uint64_t hash = _iboId;
		for (std::uint32_t i = 0; i < MaxAttributes; i++) {
			const Attribute& attribute = _attributes[i];
			if (!attribute._enabled) {
				continue;
			}
			hash = hashCombine(hash, attribute._index);
			hash = hashCombine(hash, attribute._vboId);
			hash = hashCombine(hash, (std::uint64_t(std::uint32_t(attribute._size)) << 32) | attribute._type);
			hash = hashCombine(hash, (std::uint64_t(std::uint32_t(attribute._stride)) << 33) | (std::uint64_t(attribute._baseOffset) << 1) | (attribute._normalized ? 1u : 0u));
			hash = hashCombine(hash, std::uint64_t(std::uintptr_t(attribute._pointer)));
		}
		return hash;
	}

	bool GLVertexFormat::operator==(const GLVertexFormat& other) const
	{
		bool areEqual = (other._iboId == _iboId && other._attributes.size() == _attributes.size());

		// If indices are the same then check attributes too
		if (areEqual) {
			for (std::uint32_t i = 0; i < _attributes.size(); i++) {
				if (other._attributes[i] != _attributes[i]) {
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
