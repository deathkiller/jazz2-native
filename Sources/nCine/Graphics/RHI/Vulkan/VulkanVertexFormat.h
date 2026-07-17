#pragma once

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RhiVulkan
{
	class VulkanBufferObject;

	/**
		@brief Vertex layout description of the Vulkan backend (aliased as `Rhi::VertexFormat`)

		Records the set of vertex attributes (component count, type, stride, offset and source buffer) the
		way the OpenGL backend does, so the pipeline's attribute setup code compiles unchanged. Slice 2a does
		not issue real draws, so the recorded layout is informational; slice 2b builds the graphics pipeline's
		`VkVertexInputAttributeDescription`/`VkVertexInputBindingDescription` from it (fed by the offline
		shader reflection).
	*/
	class VulkanVertexFormat
	{
	public:
		/** @brief The maximum number of vertex attributes */
		static constexpr std::uint32_t MaxAttributes = 16;

		/** @brief A single vertex attribute within a vertex format */
		class Attribute
		{
			friend class VulkanVertexFormat;

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
			// and avoids needing the complete `VulkanBufferObject` type in this header
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
			inline const VulkanBufferObject* GetVbo() const {
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
			inline void setVbo(const VulkanBufferObject* vbo) {
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
			const VulkanBufferObject* vbo_;
			const void* pointer_;
			std::uint32_t index_;
			std::int32_t size_;
			std::uint32_t type_;
			std::int32_t stride_;
			std::uint32_t baseOffset_;
			bool enabled_;
			bool normalized_;
		};

		VulkanVertexFormat()
			: ibo_(nullptr) {}

		inline std::uint32_t GetAttributeCount() const {
			return std::uint32_t(attributes_.size());
		}

		inline const VulkanBufferObject* GetIbo() const {
			return ibo_;
		}
		inline void SetIbo(const VulkanBufferObject* ibo) {
			ibo_ = ibo;
		}
		/** @brief Applies the vertex format (no-op for slice 2a) */
		void Define() {}
		/** @brief Disables all attributes and clears the index buffer */
		void Reset() {
			attributes_.clear();
			ibo_ = nullptr;
		}

		std::uint64_t CalculateFingerprint() const {
			return 0;
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
		bool operator==(const VulkanVertexFormat& other) const {
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
		bool operator!=(const VulkanVertexFormat& other) const {
			return !operator==(other);
		}

	private:
		SmallVector<Attribute, MaxAttributes> attributes_;
		const VulkanBufferObject* ibo_;
	};

	/**
		@brief Vertex array object stub of the Vulkan backend (aliased as `Rhi::VertexArray`)

		Vulkan has no vertex-array object; the vertex input state lives in the graphics pipeline and the
		buffer bindings are set per draw with `vkCmdBindVertexBuffers`. This only satisfies the contract alias
		with inert bind/label operations.
	*/
	class VulkanVertexArray
	{
	public:
		VulkanVertexArray() = default;
		~VulkanVertexArray() = default;

		VulkanVertexArray(const VulkanVertexArray&) = delete;
		VulkanVertexArray& operator=(const VulkanVertexArray&) = delete;

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
