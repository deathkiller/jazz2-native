#pragma once

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::GXM
{
	class GxmBufferObject;

	/**
		@brief Vertex layout description of the sceGxm backend (aliased as `RHI::VertexFormat`)

		Records the set of vertex attributes (component count, type, stride, offset and source buffer) the
		way the OpenGL backend does, so the pipeline's attribute setup code compiles unchanged. The backend
		translates the recorded layout into the `SceGxmVertexAttribute` / `SceGxmVertexStream` arrays that a
		patched `SceGxmVertexProgram` is created from (see @ref GxmShaderProgram::GetVertexProgram()).
	*/
	class GxmVertexFormat
	{
	public:
		/** @brief The maximum number of vertex attributes */
		static constexpr std::uint32_t MaxAttributes = 16;

		/** @brief A single vertex attribute within a vertex format */
		class Attribute
		{
			friend class GxmVertexFormat;

		public:
			Attribute()
				: vbo_(nullptr), pointer_(nullptr), index_(0), size_(0), type_(0), stride_(0), baseOffset_(0), enabled_(false), normalized_(false) {}

			void Init(std::uint32_t index, std::int32_t size, std::uint32_t type) {
				index_ = index;
				size_ = size;
				type_ = type;
				enabled_ = true;
			}

			// Mirrors the OpenGL backend's `Attribute::operator==` (only enabled attributes are compared in
			// depth); the source buffer is compared by pointer identity, which is equivalent for this backend
			// and avoids needing the complete `GxmBufferObject` type in this header
			bool operator==(const Attribute& other) const {
				return ((other.enabled_ == false && enabled_ == false) ||
						((other.enabled_ == true && enabled_ == true) &&
							other.vbo_ == vbo_ &&
							other.index_ == index_ &&
							other.size_ == size_ &&
							other.type_ == type_ &&
							other.normalized_ == normalized_ &&
							other.stride_ == stride_ &&
							other.pointer_ == pointer_ &&
							other.baseOffset_ == baseOffset_));
			}
			bool operator!=(const Attribute& other) const {
				return !operator==(other);
			}

			inline bool IsEnabled() const {
				return enabled_;
			}
			inline const GxmBufferObject* GetVbo() const {
				return vbo_;
			}
			inline std::uint32_t GetIndex() const {
				return index_;
			}
			inline std::int32_t GetSize() const {
				return size_;
			}
			inline std::uint32_t GetType() const {
				return type_;
			}
			inline bool IsNormalized() const {
				return normalized_;
			}
			inline std::int32_t GetStride() const {
				return stride_;
			}
			inline const void* GetPointer() const {
				return pointer_;
			}
			inline std::uint32_t GetBaseOffset() const {
				return baseOffset_;
			}

			void SetVboParameters(std::int32_t stride, const void* pointer) {
				stride_ = stride;
				pointer_ = pointer;
			}
			inline void setVbo(const GxmBufferObject* vbo) {
				vbo_ = vbo;
			}
			inline void SetBaseOffset(std::uint32_t baseOffset) {
				baseOffset_ = baseOffset;
			}
			inline void SetSize(std::int32_t size) {
				size_ = size;
			}
			inline void SetType(std::uint32_t type) {
				type_ = type;
			}
			inline void SetNormalized(bool normalized) {
				normalized_ = normalized;
			}

		private:
			const GxmBufferObject* vbo_;
			const void* pointer_;
			std::uint32_t index_;
			std::int32_t size_;
			std::uint32_t type_;
			std::int32_t stride_;
			std::uint32_t baseOffset_;
			bool enabled_;
			bool normalized_;
		};

		GxmVertexFormat()
			: ibo_(nullptr) {}

		inline std::uint32_t GetAttributeCount() const {
			return std::uint32_t(attributes_.size());
		}

		inline const GxmBufferObject* GetIbo() const {
			return ibo_;
		}
		inline void SetIbo(const GxmBufferObject* ibo) {
			ibo_ = ibo;
		}
		/** @brief Applies the vertex format (no-op; the recorded layout is consumed at input-layout creation) */
		void Define() {}
		/** @brief Disables all attributes and clears the index buffer */
		void Reset() {
			attributes_.clear();
			ibo_ = nullptr;
		}

		/**
			@brief Returns a value identifying this layout, used as the key of the patched-vertex-program cache

			A patched `SceGxmVertexProgram` bakes the layout in, so it may only be reused for an identical one.
			The digest covers everything that ends up in the `SceGxmVertexAttribute` / `SceGxmVertexStream`
			arrays - which attribute slots are enabled, and each one's component count, component type,
			normalization, within-vertex offset and stream stride. The source buffer and its base offset are
			deliberately left out: they are passed to the draw call as an address, not compiled into the
			program, so two draws out of different ring-buffer regions share one patched program.
		*/
		std::uint64_t CalculateFingerprint() const {
			std::uint64_t hash = 0xcbf29ce484222325ull;		// FNV-1a, 64-bit
			const auto mix = [&hash](std::uint64_t value) {
				hash = (hash ^ value) * 0x100000001b3ull;
			};
			mix(attributes_.size());
			for (std::uint32_t i = 0; i < attributes_.size(); i++) {
				const Attribute& attribute = attributes_[i];
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
			if (index >= attributes_.size()) {
				attributes_.resize(index + 1);
			}
			return attributes_[index];
		}
		inline const Attribute& operator[](std::uint32_t index) const {
			return attributes_[index];
		}

		// Mirrors the OpenGL backend's `GLVertexFormat::operator==`: equal index buffers and attribute sets
		bool operator==(const GxmVertexFormat& other) const {
			if (other.ibo_ != ibo_ || other.attributes_.size() != attributes_.size()) {
				return false;
			}
			for (std::uint32_t i = 0; i < attributes_.size(); i++) {
				if (other.attributes_[i] != attributes_[i]) {
					return false;
				}
			}
			return true;
		}
		bool operator!=(const GxmVertexFormat& other) const {
			return !operator==(other);
		}

	private:
		SmallVector<Attribute, MaxAttributes> attributes_;
		const GxmBufferObject* ibo_;
	};

	/**
		@brief Vertex array object stub of the sceGxm backend (aliased as `RHI::VertexArray`)

		sceGxm has no vertex-array object; the vertex layout is baked into the patched vertex program and the
		stream base addresses are set per draw. This only satisfies the contract alias with inert bind/label
		operations.
	*/
	class GxmVertexArray
	{
	public:
		GxmVertexArray() = default;
		~GxmVertexArray() = default;

		GxmVertexArray(const GxmVertexArray&) = delete;
		GxmVertexArray& operator=(const GxmVertexArray&) = delete;

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
