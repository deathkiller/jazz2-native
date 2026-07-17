#pragma once

#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nCine::RhiVulkan
{
	class VulkanShaderProgram;

	namespace UniformTypeInfo
	{
		/** @brief Number of scalar components of a reflected uniform type (0 for aggregates) */
		inline std::uint32_t ComponentCount(ShaderCompiler::UniformType type)
		{
			switch (type) {
				case ShaderCompiler::UniformType::Float:
				case ShaderCompiler::UniformType::Int:
				case ShaderCompiler::UniformType::UInt:
				case ShaderCompiler::UniformType::Bool:
				case ShaderCompiler::UniformType::Sampler2D:
				case ShaderCompiler::UniformType::Sampler3D:
				case ShaderCompiler::UniformType::SamplerCube:
					return 1;
				case ShaderCompiler::UniformType::Vec2:
				case ShaderCompiler::UniformType::IVec2:
				case ShaderCompiler::UniformType::UVec2:
				case ShaderCompiler::UniformType::BVec2:
					return 2;
				case ShaderCompiler::UniformType::Vec3:
				case ShaderCompiler::UniformType::IVec3:
				case ShaderCompiler::UniformType::UVec3:
				case ShaderCompiler::UniformType::BVec3:
					return 3;
				case ShaderCompiler::UniformType::Vec4:
				case ShaderCompiler::UniformType::IVec4:
				case ShaderCompiler::UniformType::UVec4:
				case ShaderCompiler::UniformType::BVec4:
					return 4;
				case ShaderCompiler::UniformType::Mat2: return 4;
				case ShaderCompiler::UniformType::Mat3: return 9;
				case ShaderCompiler::UniformType::Mat4: return 16;
				default: return 0;
			}
		}

		/** @brief Returns `true` if the basic component type of the uniform is floating point */
		inline bool IsFloat(ShaderCompiler::UniformType type)
		{
			switch (type) {
				case ShaderCompiler::UniformType::Float:
				case ShaderCompiler::UniformType::Vec2:
				case ShaderCompiler::UniformType::Vec3:
				case ShaderCompiler::UniformType::Vec4:
				case ShaderCompiler::UniformType::Mat2:
				case ShaderCompiler::UniformType::Mat3:
				case ShaderCompiler::UniformType::Mat4:
					return true;
				default:
					return false;
			}
		}

		/** @brief Maps a reflected uniform type to the equivalent OpenGL type enum, mirroring the GL backend's `UniformTypeToGL` so the render pipeline's `GetType()` comparisons (e.g. `== GL_SAMPLER_2D`) behave identically */
		inline GLenum ToGLType(ShaderCompiler::UniformType type)
		{
			switch (type) {
				case ShaderCompiler::UniformType::Float: return GL_FLOAT;
				case ShaderCompiler::UniformType::Int: return GL_INT;
				case ShaderCompiler::UniformType::UInt: return GL_UNSIGNED_INT;
				case ShaderCompiler::UniformType::Bool: return GL_BOOL;
				case ShaderCompiler::UniformType::Vec2: return GL_FLOAT_VEC2;
				case ShaderCompiler::UniformType::Vec3: return GL_FLOAT_VEC3;
				case ShaderCompiler::UniformType::Vec4: return GL_FLOAT_VEC4;
				case ShaderCompiler::UniformType::IVec2: return GL_INT_VEC2;
				case ShaderCompiler::UniformType::IVec3: return GL_INT_VEC3;
				case ShaderCompiler::UniformType::IVec4: return GL_INT_VEC4;
				case ShaderCompiler::UniformType::UVec2: return GL_UNSIGNED_INT_VEC2;
				case ShaderCompiler::UniformType::UVec3: return GL_UNSIGNED_INT_VEC3;
				case ShaderCompiler::UniformType::UVec4: return GL_UNSIGNED_INT_VEC4;
				case ShaderCompiler::UniformType::BVec2: return GL_BOOL_VEC2;
				case ShaderCompiler::UniformType::BVec3: return GL_BOOL_VEC3;
				case ShaderCompiler::UniformType::BVec4: return GL_BOOL_VEC4;
				case ShaderCompiler::UniformType::Mat2: return GL_FLOAT_MAT2;
				case ShaderCompiler::UniformType::Mat3: return GL_FLOAT_MAT3;
				case ShaderCompiler::UniformType::Mat4: return GL_FLOAT_MAT4;
				case ShaderCompiler::UniformType::Sampler2D: return GL_SAMPLER_2D;
				case ShaderCompiler::UniformType::Sampler3D: return GL_SAMPLER_3D;
				case ShaderCompiler::UniformType::SamplerCube: return GL_SAMPLER_CUBE;
				default: return GL_FLOAT;
			}
		}
	}

	/**
		@brief Reflected metadata of a single active uniform (aliased as `Rhi::Uniform`)

		Holds everything the backend needs about one uniform imported from the offline reflection: its name,
		scalar/vector/matrix type, array size, a synthetic sequential location and, for uniforms living inside
		a block, the owning block index and std140 byte offset. It carries no value storage — a @ref
		VulkanUniformCache holds the value.
	*/
	class VulkanUniform
	{
		friend class VulkanShaderProgram;
		friend class VulkanUniformBlock;

	public:
		/** @brief Maximum length of a uniform name, including the terminating null character */
		static constexpr std::uint32_t MaxNameLength = 48;

		VulkanUniform()
			: type_(ShaderCompiler::UniformType::Float), size_(0), location_(-1), blockIndex_(-1), offset_(0), owner_(nullptr)
		{
			name_[0] = '\0';
		}
		VulkanUniform(VulkanShaderProgram* owner, const char* name, ShaderCompiler::UniformType type, std::int32_t arraySize, std::int32_t location)
			: type_(type), size_(arraySize > 0 ? arraySize : 1), location_(location), blockIndex_(-1), offset_(0), owner_(owner)
		{
			SetName(name);
		}

		inline std::int32_t GetLocation() const {
			return location_;
		}
		inline std::int32_t GetBlockIndex() const {
			return blockIndex_;
		}
		inline std::int32_t GetSize() const {
			return size_;
		}
		/** @brief Returns the OpenGL type enum of the uniform (e.g. `GL_FLOAT_VEC4`), matching the GL backend so pipeline comparisons like `== GL_SAMPLER_2D` work */
		inline GLenum GetType() const {
			return UniformTypeInfo::ToGLType(type_);
		}
		/** @brief Returns the reflected (backend-neutral) type of the uniform */
		inline ShaderCompiler::UniformType GetReflectedType() const {
			return type_;
		}
		inline std::int32_t GetOffset() const {
			return offset_;
		}
		inline const char* GetName() const {
			return name_;
		}
		inline VulkanShaderProgram* GetOwner() const {
			return owner_;
		}
		inline std::uint32_t GetComponentCount() const {
			return UniformTypeInfo::ComponentCount(type_);
		}
		inline bool IsFloat() const {
			return UniformTypeInfo::IsFloat(type_);
		}
		inline std::uint32_t GetMemorySize() const {
			return std::uint32_t(GetSize()) * GetComponentCount() * 4u;
		}

	private:
		char name_[MaxNameLength];
		ShaderCompiler::UniformType type_;
		std::int32_t size_;
		std::int32_t location_;
		std::int32_t blockIndex_;
		std::int32_t offset_;
		VulkanShaderProgram* owner_;

		void SetName(const char* name) {
			std::size_t length = std::strlen(name);
			if (length >= MaxNameLength) {
				length = MaxNameLength - 1;
			}
			std::memcpy(name_, name, length);
			name_[length] = '\0';
		}
	};

	/**
		@brief Reflected metadata of a single active uniform block (aliased as `Rhi::UniformBlock`)

		Holds the block name, a synthetic sequential index and binding index, its std140 byte size and the
		metadata of its member uniforms (each with a std140 offset). The backend does not pad the size to any
		device alignment, so @ref GetAlignAmount() is always 0.
	*/
	class VulkanUniformBlock
	{
		friend class VulkanShaderProgram;
		friend class VulkanUniformBlockCache;

	public:
		static constexpr std::uint32_t MaxNameLength = 48;

		enum class DiscoverUniforms
		{
			ENABLED,
			DISABLED
		};

		VulkanUniformBlock()
			: index_(0), size_(0), alignAmount_(0), bindingIndex_(-1)
		{
			name_[0] = '\0';
		}
		VulkanUniformBlock(std::uint32_t index, const char* name, std::int32_t dataSize)
			: index_(index), size_(dataSize), alignAmount_(0), bindingIndex_(-1)
		{
			SetName(name);
		}

		inline std::uint32_t GetIndex() const {
			return index_;
		}
		inline std::int32_t GetBindingIndex() const {
			return bindingIndex_;
		}
		inline std::int32_t GetSize() const {
			return size_;
		}
		inline std::uint8_t GetAlignAmount() const {
			return alignAmount_;
		}
		inline const char* GetName() const {
			return name_;
		}

		/** @brief Returns the member uniform with the specified name, or `nullptr` if not found */
		VulkanUniform* GetUniform(const char* name) {
			for (VulkanUniform& u : members_) {
				if (std::strcmp(u.GetName(), name) == 0) {
					return &u;
				}
			}
			return nullptr;
		}
		void SetBlockBinding(std::int32_t blockBinding) {
			bindingIndex_ = blockBinding;
		}

	private:
		char name_[MaxNameLength];
		std::uint32_t index_;
		std::int32_t size_;
		std::uint8_t alignAmount_;
		std::int32_t bindingIndex_;
		std::vector<VulkanUniform> members_;

		void SetName(const char* name) {
			std::size_t length = std::strlen(name);
			if (length >= MaxNameLength) {
				length = MaxNameLength - 1;
			}
			std::memcpy(name_, name, length);
			name_[length] = '\0';
		}
	};

	/**
		@brief Reflected metadata of a single active vertex attribute (aliased as `Rhi::Attribute`)
	*/
	class VulkanAttribute
	{
	public:
		static constexpr std::uint32_t MaxNameLength = 32;

		VulkanAttribute()
			: type_(ShaderCompiler::UniformType::Float), location_(-1)
		{
			name_[0] = '\0';
		}
		VulkanAttribute(const char* name, ShaderCompiler::UniformType type, std::int32_t location)
			: type_(type), location_(location)
		{
			std::size_t length = std::strlen(name);
			if (length >= MaxNameLength) {
				length = MaxNameLength - 1;
			}
			std::memcpy(name_, name, length);
			name_[length] = '\0';
		}

		inline std::int32_t GetLocation() const {
			return location_;
		}
		inline ShaderCompiler::UniformType GetType() const {
			return type_;
		}
		inline const char* GetName() const {
			return name_;
		}
		inline std::int32_t GetComponentCount() const {
			return std::int32_t(UniformTypeInfo::ComponentCount(type_));
		}

	private:
		char name_[MaxNameLength];
		ShaderCompiler::UniformType type_;
		std::int32_t location_;
	};
}
