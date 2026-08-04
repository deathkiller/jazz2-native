#pragma once

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::PVR
{
	class PvrBuffer;

	/**
		@brief Vertex layout description of the PVR backend (aliased as `RHI::VertexFormat`)

		Records the set of vertex attributes (component count, type, stride, offset and source buffer) the
		way the OpenGL backend does, so the pipeline's attribute setup code compiles unchanged. The GX draw
		dispatch synthesizes sprite corners from the per-draw uniform block and feeds general vertex data
		through the CPU write-gather FIFO with its own layout, so the recorded layout is currently
		informational - @ref Define() and @ref Reset() have nothing to apply.
	*/
	class PvrVertexFormat
	{
	public:
		/** @brief The maximum number of vertex attributes */
		static constexpr std::uint32_t MaxAttributes = 16;

		/** @brief A single vertex attribute within a vertex format */
		class Attribute
		{
			friend class PvrVertexFormat;

		public:
			Attribute()
				: _vbo(nullptr), _pointer(nullptr), _index(0), _size(0), _type(0), _stride(0), _baseOffset(0), _enabled(false), _normalized(false) {}

			void Init(std::uint32_t index, std::int32_t size, std::uint32_t type) {
				_index = index;
				_size = size;
				_type = type;
				_enabled = true;
			}

			// Mirrors the OpenGL backend's `Attribute::operator==` (only enabled attributes are compared in
			// depth); the source buffer is compared by pointer identity, which is equivalent here and avoids
			// needing the complete `PvrBuffer` type in this header
			bool operator==(const Attribute& other) const {
				return ((other._enabled == false && _enabled == false) ||
						((other._enabled == true && _enabled == true) &&
							other._vbo == _vbo &&
							other._index == _index &&
							other._size == _size &&
							other._type == _type &&
							other._normalized == _normalized &&
							other._stride == _stride &&
							other._pointer == _pointer &&
							other._baseOffset == _baseOffset));
			}
			bool operator!=(const Attribute& other) const {
				return !operator==(other);
			}

			inline bool IsEnabled() const {
				return _enabled;
			}
			inline const PvrBuffer* GetVbo() const {
				return _vbo;
			}
			inline std::uint32_t GetIndex() const {
				return _index;
			}
			inline std::int32_t GetSize() const {
				return _size;
			}
			inline std::uint32_t GetType() const {
				return _type;
			}
			inline bool IsNormalized() const {
				return _normalized;
			}
			inline std::int32_t GetStride() const {
				return _stride;
			}
			inline const void* GetPointer() const {
				return _pointer;
			}
			inline std::uint32_t GetBaseOffset() const {
				return _baseOffset;
			}

			void SetVboParameters(std::int32_t stride, const void* pointer) {
				_stride = stride;
				_pointer = pointer;
			}
			inline void setVbo(const PvrBuffer* vbo) {
				_vbo = vbo;
			}
			inline void SetBaseOffset(std::uint32_t baseOffset) {
				_baseOffset = baseOffset;
			}
			inline void SetSize(std::int32_t size) {
				_size = size;
			}
			inline void SetType(std::uint32_t type) {
				_type = type;
			}
			inline void SetNormalized(bool normalized) {
				_normalized = normalized;
			}

		private:
			const PvrBuffer* _vbo;
			const void* _pointer;
			std::uint32_t _index;
			std::int32_t _size;
			std::uint32_t _type;
			std::int32_t _stride;
			std::uint32_t _baseOffset;
			bool _enabled;
			bool _normalized;
		};

		PvrVertexFormat()
			: _ibo(nullptr) {}

		inline std::uint32_t GetAttributeCount() const {
			return std::uint32_t(_attributes.size());
		}

		inline const PvrBuffer* GetIbo() const {
			return _ibo;
		}
		inline void SetIbo(const PvrBuffer* ibo) {
			_ibo = ibo;
		}
		/** @brief Applies the vertex format (no-op for the PVR backend) */
		void Define() {}
		/** @brief Disables all attributes and clears the index buffer */
		void Reset() {
			_attributes.clear();
			_ibo = nullptr;
		}

		std::uint64_t CalculateFingerprint() const {
			return 0;
		}

		inline Attribute& operator[](std::uint32_t index) {
			if (index >= _attributes.size()) {
				_attributes.resize(index + 1);
			}
			return _attributes[index];
		}
		inline const Attribute& operator[](std::uint32_t index) const {
			return _attributes[index];
		}

		// Mirrors the OpenGL backend's `GLVertexFormat::operator==`: equal index buffers and attribute sets
		bool operator==(const PvrVertexFormat& other) const {
			if (other._ibo != _ibo || other._attributes.size() != _attributes.size()) {
				return false;
			}
			for (std::uint32_t i = 0; i < _attributes.size(); i++) {
				if (other._attributes[i] != _attributes[i]) {
					return false;
				}
			}
			return true;
		}
		bool operator!=(const PvrVertexFormat& other) const {
			return !operator==(other);
		}

	private:
		SmallVector<Attribute, MaxAttributes> _attributes;
		const PvrBuffer* _ibo;
	};

	/**
		@brief Vertex array object stub of the PVR backend (aliased as `RHI::VertexArray`)

		GX has no vertex-array state object to capture, so this only satisfies the contract alias with
		inert bind/label operations.
	*/
	class PvrVertexArray
	{
	public:
		PvrVertexArray() = default;
		~PvrVertexArray() = default;

		PvrVertexArray(const PvrVertexArray&) = delete;
		PvrVertexArray& operator=(const PvrVertexArray&) = delete;

		inline std::uint32_t GetGLHandle() const {
			return 0;
		}
		bool Bind() const {
			return true;
		}
		static bool Unbind() {
			return true;
		}
		void SetObjectLabel(StringView label) {
			static_cast<void>(label);
		}
	};
}
