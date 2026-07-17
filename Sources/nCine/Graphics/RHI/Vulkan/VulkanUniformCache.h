#pragma once

#include "VulkanShaderTypes.h"

#include <cstdint>
#include <vector>

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RhiVulkan
{
	/**
		@brief Caches the host-side value of a single uniform (aliased as `Rhi::UniformCache`)

		Mirrors the OpenGL backend's uniform cache: it holds a pointer into a shared host data buffer and the
		`Set*` methods that write values into it, plus a dirty flag. For a loose uniform, @ref CommitValue()
		publishes the value pointer to the owning shader program. For a uniform living inside a block,
		committing is a no-op — the whole block is published by @ref VulkanShaderUniformBlocks.
	*/
	class VulkanUniformCache
	{
	public:
		VulkanUniformCache()
			: uniform_(nullptr), dataPointer_(nullptr), isDirty_(false) {}
		explicit VulkanUniformCache(const VulkanUniform* uniform)
			: uniform_(uniform), dataPointer_(nullptr), isDirty_(false) {}

		inline const VulkanUniform* GetUniform() const {
			return uniform_;
		}
		inline std::uint8_t* GetDataPointer() const {
			return dataPointer_;
		}
		inline void SetDataPointer(std::uint8_t* dataPointer) {
			dataPointer_ = dataPointer;
		}

		const float* GetFloatVector() const;
		float GetFloatValue(std::uint32_t index) const;
		const std::int32_t* GetIntVector() const;
		std::int32_t GetIntValue(std::uint32_t index) const;

		bool SetFloatVector(const float* vec);
		bool SetFloatValue(float v0);
		bool SetFloatValue(float v0, float v1);
		bool SetFloatValue(float v0, float v1, float v2);
		bool SetFloatValue(float v0, float v1, float v2, float v3);
		bool SetIntVector(const std::int32_t* vec);
		bool SetIntValue(std::int32_t v0);
		bool SetIntValue(std::int32_t v0, std::int32_t v1);
		bool SetIntValue(std::int32_t v0, std::int32_t v1, std::int32_t v2);
		bool SetIntValue(std::int32_t v0, std::int32_t v1, std::int32_t v2, std::int32_t v3);

		inline bool IsDirty() const {
			return isDirty_;
		}
		inline void SetDirty(bool isDirty) {
			isDirty_ = isDirty;
		}
		/** @brief Publishes a dirty loose-uniform value to the owning program; a no-op for block members */
		bool CommitValue();

	private:
		const VulkanUniform* uniform_;
		std::uint8_t* dataPointer_;
		bool isDirty_;

		bool CheckFloat() const;
		bool CheckInt() const;
		bool CheckComponents(std::uint32_t requiredComponents) const;
	};

	/**
		@brief Caches the contents of a uniform block (aliased as `Rhi::UniformBlockCache`)

		Holds a pointer into a shared host data buffer mirroring the block's std140 layout and a @ref
		VulkanUniformCache for each member, each pointing at its member offset within that buffer. The block is
		uploaded as one contiguous range by @ref VulkanShaderUniformBlocks.
	*/
	class VulkanUniformBlockCache
	{
	public:
		VulkanUniformBlockCache()
			: uniformBlock_(nullptr), dataPointer_(nullptr), usedSize_(0) {}
		explicit VulkanUniformBlockCache(VulkanUniformBlock* uniformBlock);

		inline const VulkanUniformBlock* uniformBlock() const {
			return uniformBlock_;
		}
		std::uint32_t GetIndex() const;
		std::int32_t GetBindingIndex() const;
		std::int32_t GetSize() const;
		std::uint8_t GetAlignAmount() const;

		inline std::uint8_t* GetDataPointer() {
			return dataPointer_;
		}
		inline const std::uint8_t* GetDataPointer() const {
			return dataPointer_;
		}
		void SetDataPointer(std::uint8_t* dataPointer);

		inline std::int32_t usedSize() const {
			return usedSize_;
		}
		void SetUsedSize(std::int32_t usedSize);

		bool CopyData(std::uint32_t destIndex, const std::uint8_t* src, std::uint32_t numBytes);
		inline bool CopyData(const std::uint8_t* src) {
			return CopyData(0, src, std::uint32_t(usedSize_));
		}

		/** @brief Returns the member uniform cache with the specified name, or `nullptr` if not found */
		VulkanUniformCache* GetUniform(StringView name);
		void SetBlockBinding(std::int32_t blockBinding);

	private:
		VulkanUniformBlock* uniformBlock_;
		std::uint8_t* dataPointer_;
		std::int32_t usedSize_;

		struct NamedCache
		{
			String Name;
			VulkanUniformCache Cache;
		};
		std::vector<NamedCache> uniformCaches_;
	};
}
