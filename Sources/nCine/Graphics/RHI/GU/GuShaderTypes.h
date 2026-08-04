#pragma once

#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nCine::RHI::GU
{
	class GuShaderProgram;

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
	}

	/**
		@brief Reflected metadata of a single active uniform (aliased as `RHI::Uniform`)

		Holds everything the GU backend needs about one uniform imported from the offline
		reflection: its name, scalar/vector/matrix type, array size, a synthetic sequential location and,
		for uniforms living inside a block, the owning block index and std140 byte offset. It carries no
		value storage — a @ref GuUniformCache holds the value.
	*/
	class GuUniform
	{
		friend class GuShaderProgram;
		friend class GuUniformBlock;

	public:
		/** @brief Maximum length of a uniform name, including the terminating null character */
		static constexpr std::uint32_t MaxNameLength = 48;

		GuUniform()
			: _type(ShaderCompiler::UniformType::Float), _size(0), _location(-1), _blockIndex(-1), _offset(0), _owner(nullptr)
		{
			_name[0] = '\0';
		}
		GuUniform(GuShaderProgram* owner, const char* name, ShaderCompiler::UniformType type, std::int32_t arraySize, std::int32_t location)
			: _type(type), _size(arraySize > 0 ? arraySize : 1), _location(location), _blockIndex(-1), _offset(0), _owner(owner)
		{
			SetName(name);
		}

		inline std::int32_t GetLocation() const {
			return _location;
		}
		inline std::int32_t GetBlockIndex() const {
			return _blockIndex;
		}
		inline std::int32_t GetSize() const {
			return _size;
		}
		/** @brief Returns the reflected (backend-neutral) type of the uniform */
		inline ShaderCompiler::UniformType GetType() const {
			return _type;
		}
		inline std::int32_t GetOffset() const {
			return _offset;
		}
		inline const char* GetName() const {
			return _name;
		}
		inline GuShaderProgram* GetOwner() const {
			return _owner;
		}
		inline std::uint32_t GetComponentCount() const {
			return UniformTypeInfo::ComponentCount(_type);
		}
		inline bool IsFloat() const {
			return UniformTypeInfo::IsFloat(_type);
		}
		inline std::uint32_t GetMemorySize() const {
			return std::uint32_t(GetSize()) * GetComponentCount() * 4u;
		}

	private:
		char _name[MaxNameLength];
		ShaderCompiler::UniformType _type;
		std::int32_t _size;
		std::int32_t _location;
		std::int32_t _blockIndex;
		std::int32_t _offset;
		GuShaderProgram* _owner;

		void SetName(const char* name) {
			std::size_t length = std::strlen(name);
			if (length >= MaxNameLength) {
				length = MaxNameLength - 1;
			}
			std::memcpy(_name, name, length);
			_name[length] = '\0';
		}
	};

	/**
		@brief Reflected metadata of a single active uniform block (aliased as `RHI::UniformBlock`)

		Holds the block name, a synthetic sequential index and binding index, its std140 byte size and the
		metadata of its member uniforms (each with a std140 offset). The GU backend does not pad the
		size to any device alignment, so @ref GetAlignAmount() is always 0.
	*/
	class GuUniformBlock
	{
		friend class GuShaderProgram;
		friend class GuUniformBlockCache;

	public:
		static constexpr std::uint32_t MaxNameLength = 48;

		enum class DiscoverUniforms
		{
			ENABLED,
			DISABLED
		};

		GuUniformBlock()
			: _index(0), _size(0), _alignAmount(0), _bindingIndex(-1)
		{
			_name[0] = '\0';
		}
		GuUniformBlock(std::uint32_t index, const char* name, std::int32_t dataSize)
			: _index(index), _size(dataSize), _alignAmount(0), _bindingIndex(-1)
		{
			SetName(name);
		}

		inline std::uint32_t GetIndex() const {
			return _index;
		}
		inline std::int32_t GetBindingIndex() const {
			return _bindingIndex;
		}
		inline std::int32_t GetSize() const {
			return _size;
		}
		inline std::uint8_t GetAlignAmount() const {
			return _alignAmount;
		}
		inline const char* GetName() const {
			return _name;
		}

		/** @brief Returns the member uniform with the specified name, or `nullptr` if not found */
		GuUniform* GetUniform(const char* name) {
			for (GuUniform& u : _members) {
				if (std::strcmp(u.GetName(), name) == 0) {
					return &u;
				}
			}
			return nullptr;
		}
		void SetBlockBinding(std::int32_t blockBinding) {
			_bindingIndex = blockBinding;
		}

	private:
		char _name[MaxNameLength];
		std::uint32_t _index;
		std::int32_t _size;
		std::uint8_t _alignAmount;
		std::int32_t _bindingIndex;
		std::vector<GuUniform> _members;

		void SetName(const char* name) {
			std::size_t length = std::strlen(name);
			if (length >= MaxNameLength) {
				length = MaxNameLength - 1;
			}
			std::memcpy(_name, name, length);
			_name[length] = '\0';
		}
	};

	/**
		@brief Reflected metadata of a single active vertex attribute (aliased as `RHI::Attribute`)
	*/
	class GuAttribute
	{
	public:
		static constexpr std::uint32_t MaxNameLength = 32;

		GuAttribute()
			: _type(ShaderCompiler::UniformType::Float), _location(-1)
		{
			_name[0] = '\0';
		}
		GuAttribute(const char* name, ShaderCompiler::UniformType type, std::int32_t location)
			: _type(type), _location(location)
		{
			std::size_t length = std::strlen(name);
			if (length >= MaxNameLength) {
				length = MaxNameLength - 1;
			}
			std::memcpy(_name, name, length);
			_name[length] = '\0';
		}

		inline std::int32_t GetLocation() const {
			return _location;
		}
		inline ShaderCompiler::UniformType GetType() const {
			return _type;
		}
		inline const char* GetName() const {
			return _name;
		}
		inline std::int32_t GetComponentCount() const {
			return std::int32_t(UniformTypeInfo::ComponentCount(_type));
		}

	private:
		char _name[MaxNameLength];
		ShaderCompiler::UniformType _type;
		std::int32_t _location;
	};
}
