#pragma once

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::RSX
{
	class RsxBufferObject;

	/**
		@brief Vertex layout description of the RSX backend (aliased as `RHI::VertexFormat`)

		Records the set of vertex attributes (component count, type, stride, offset and source buffer) the
		way the OpenGL backend does, so the pipeline's attribute setup code compiles unchanged. Unlike the
		sceGxm backend, which has to bake a layout into a patched vertex program, the RSX takes each
		attribute's format and source at draw time through `rsxBindVertexArrayAttrib()` - so the recorded
		layout is simply replayed by @ref RsxDevice::DrawCommon() and nothing is compiled from it.
	*/
	class RsxVertexFormat
	{
	public:
		/** @brief The maximum number of vertex attributes */
		static constexpr std::uint32_t MaxAttributes = 16;

		/** @brief A single vertex attribute within a vertex format */
		class Attribute
		{
			friend class RsxVertexFormat;

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
			// depth); the source buffer is compared by pointer identity, which is equivalent for this backend
			// and avoids needing the complete `RsxBufferObject` type in this header
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
			inline const RsxBufferObject* GetVbo() const {
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
			inline void setVbo(const RsxBufferObject* vbo) {
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
			const RsxBufferObject* _vbo;
			const void* _pointer;
			std::uint32_t _index;
			std::int32_t _size;
			std::uint32_t _type;
			std::int32_t _stride;
			std::uint32_t _baseOffset;
			bool _enabled;
			bool _normalized;
		};

		RsxVertexFormat()
			: _ibo(nullptr) {}

		inline std::uint32_t GetAttributeCount() const {
			return std::uint32_t(_attributes.size());
		}

		inline const RsxBufferObject* GetIbo() const {
			return _ibo;
		}
		inline void SetIbo(const RsxBufferObject* ibo) {
			_ibo = ibo;
		}
		/** @brief Applies the vertex format (no-op; the recorded layout is consumed at input-layout creation) */
		void Define() {}
		/** @brief Disables all attributes and clears the index buffer */
		void Reset() {
			_attributes.clear();
			_ibo = nullptr;
		}

		/**
			@brief Returns a value identifying this layout, used by `RenderVaoPool` to reuse a vertex array

			The RSX compiles nothing from a layout, so this backend needs no cache of its own - but the
			shared `RenderVaoPool` keys on this digest across every backend, so it is provided with the same
			meaning it has elsewhere: which attribute slots are enabled, and each one's component count,
			component type, normalization, within-vertex offset and stream stride. The source buffer and its
			base offset are deliberately left out, so two draws out of different ring-buffer regions match.
		*/
		std::uint64_t CalculateFingerprint() const {
			std::uint64_t hash = 0xcbf29ce484222325ull;		// FNV-1a, 64-bit
			const auto mix = [&hash](std::uint64_t value) {
				hash = (hash ^ value) * 0x100000001b3ull;
			};
			mix(_attributes.size());
			for (std::uint32_t i = 0; i < _attributes.size(); i++) {
				const Attribute& attribute = _attributes[i];
				if (!attribute.IsEnabled()) {
					mix(0);
					continue;
				}
				mix(std::uint64_t(attribute.GetIndex()) | (std::uint64_t(std::uint32_t(attribute.GetSize())) << 8) |
					(std::uint64_t(attribute.GetType()) << 16) | (std::uint64_t(attribute.IsNormalized() ? 1 : 0) << 48));
				mix(std::uint64_t(std::uint32_t(attribute.GetStride())) |
					(std::uint64_t(reinterpret_cast<std::uintptr_t>(attribute.GetPointer())) << 32));
			}
			return hash;
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
		bool operator==(const RsxVertexFormat& other) const {
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
		bool operator!=(const RsxVertexFormat& other) const {
			return !operator==(other);
		}

	private:
		SmallVector<Attribute, MaxAttributes> _attributes;
		const RsxBufferObject* _ibo;
	};

	/**
		@brief Vertex array object stub of the RSX backend (aliased as `RHI::VertexArray`)

		The RSX has no vertex-array object: every attribute's format, stride and source offset is programmed
		per draw. This only satisfies the contract alias with inert bind/label operations.
	*/
	class RsxVertexArray
	{
	public:
		RsxVertexArray() = default;
		~RsxVertexArray() = default;

		RsxVertexArray(const RsxVertexArray&) = delete;
		RsxVertexArray& operator=(const RsxVertexArray&) = delete;

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
