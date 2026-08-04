#include "VulkanShaderUniforms.h"
#include "VulkanShaderProgram.h"
#include "VulkanBufferObject.h"

#include <cstring>

namespace nCine::RHI::Vulkan
{
	namespace
	{
		// Matches a name against the null-separated, double-null-terminated include/exclude lists exactly
		// like the OpenGL backend's uniform importers do
		bool ShouldImport(const char* name, const char* includeOnly, const char* exclude)
		{
			bool shouldImport = true;
			if (includeOnly != nullptr) {
				shouldImport = false;
				const char* current = includeOnly;
				while (current != nullptr && current[0] != '\0') {
					if (std::strcmp(current, name) == 0) {
						shouldImport = true;
						break;
					}
					current += std::strlen(current) + 1;
				}
			}
			if (exclude != nullptr) {
				const char* current = exclude;
				while (current != nullptr && current[0] != '\0') {
					if (std::strcmp(current, name) == 0) {
						shouldImport = false;
						break;
					}
					current += std::strlen(current) + 1;
				}
			}
			return shouldImport;
		}
	}

	VulkanShaderUniforms::VulkanShaderUniforms()
		: _shaderProgram(nullptr), _maybeDirty(true)
	{
	}

	VulkanShaderUniforms::VulkanShaderUniforms(VulkanShaderProgram* shaderProgram)
		: VulkanShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	VulkanShaderUniforms::VulkanShaderUniforms(VulkanShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: VulkanShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void VulkanShaderUniforms::SetProgram(VulkanShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformCaches.clear();
		_maybeDirty = true;

		if (_shaderProgram->GetStatus() == VulkanShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void VulkanShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != VulkanShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_maybeDirty = true;
		std::uint32_t offset = 0;
		for (VulkanUniformCache& cache : _uniformCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void VulkanShaderUniforms::SetDirty(bool isDirty)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != VulkanShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		_maybeDirty = isDirty;
		for (VulkanUniformCache& cache : _uniformCaches) {
			cache.SetDirty(isDirty);
		}
	}

	bool VulkanShaderUniforms::HasUniform(const char* name) const
	{
		for (const VulkanUniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	VulkanUniformCache* VulkanShaderUniforms::GetUniform(const char* name)
	{
		for (VulkanUniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				_maybeDirty = true;
				return &cache;
			}
		}
		return nullptr;
	}

	void VulkanShaderUniforms::CommitUniforms()
	{
		if (_shaderProgram == nullptr) {
			return;
		}
		if (_maybeDirty && _shaderProgram->GetStatus() == VulkanShaderProgram::Status::LinkedWithIntrospection) {
			_shaderProgram->Use();
			for (VulkanUniformCache& cache : _uniformCaches) {
				cache.CommitValue();
			}
			_maybeDirty = false;
		}
	}

	void VulkanShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const VulkanUniform& uniform : _shaderProgram->_uniforms) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				_uniformCaches.push_back(VulkanUniformCache(&uniform));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	VulkanShaderUniformBlocks::UniformRangeAllocator VulkanShaderUniformBlocks::_uniformRangeAllocator = nullptr;

	void VulkanShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		_uniformRangeAllocator = allocator;
	}

	VulkanShaderUniformBlocks::VulkanShaderUniformBlocks()
		: _shaderProgram(nullptr), _dataPointer(nullptr)
	{
	}

	VulkanShaderUniformBlocks::VulkanShaderUniformBlocks(VulkanShaderProgram* shaderProgram)
		: VulkanShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	VulkanShaderUniformBlocks::VulkanShaderUniformBlocks(VulkanShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: VulkanShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void VulkanShaderUniformBlocks::SetProgram(VulkanShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformBlockCaches.clear();

		if (_shaderProgram->GetStatus() == VulkanShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void VulkanShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != VulkanShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_dataPointer = dataPointer;
		std::int32_t offset = 0;
		for (VulkanUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool VulkanShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const VulkanUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	VulkanUniformBlockCache* VulkanShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (VulkanUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void VulkanShaderUniformBlocks::CommitUniformBlocks()
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != VulkanShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (VulkanUniformBlockCache& cache : _uniformBlockCaches) {
			if (cache.GetDataPointer() != _dataPointer + totalUsedSize) {
				hasMemoryGaps = true;
			}
			totalUsedSize += cache.usedSize();
		}

		if (totalUsedSize > 0 && _uniformRangeAllocator != nullptr) {
			_uboParams = _uniformRangeAllocator(std::uint32_t(totalUsedSize));
			if (_uboParams.mapBase != nullptr) {
				if (hasMemoryGaps) {
					std::int32_t offset = 0;
					for (VulkanUniformBlockCache& cache : _uniformBlockCaches) {
						std::memcpy(_uboParams.mapBase + _uboParams.offset + offset, cache.GetDataPointer(), cache.usedSize());
						offset += cache.usedSize();
					}
				} else {
					std::memcpy(_uboParams.mapBase + _uboParams.offset, _dataPointer, totalUsedSize);
				}
			}
		}
	}

	void VulkanShaderUniformBlocks::Bind()
	{
		if (_uboParams.object == nullptr) {
			return;
		}

		_uboParams.object->Bind();
		std::size_t moreOffset = 0;
		for (VulkanUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(_uboParams.offset) + moreOffset;
			_uboParams.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.usedSize()));
			moreOffset += std::size_t(cache.usedSize());
		}
	}

	void VulkanShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (VulkanUniformBlock& block : _shaderProgram->_uniformBlocks) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				_uniformBlockCaches.push_back(VulkanUniformBlockCache(&block));
			}
		}
	}
}
