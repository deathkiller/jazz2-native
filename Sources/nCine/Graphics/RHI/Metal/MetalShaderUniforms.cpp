#include "MetalShaderUniforms.h"
#include "MetalShaderProgram.h"
#include "MetalBufferObject.h"

#include <cstring>

namespace nCine::RHI::Metal
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

	MetalShaderUniforms::MetalShaderUniforms()
		: _shaderProgram(nullptr), _maybeDirty(true)
	{
	}

	MetalShaderUniforms::MetalShaderUniforms(MetalShaderProgram* shaderProgram)
		: MetalShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	MetalShaderUniforms::MetalShaderUniforms(MetalShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: MetalShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void MetalShaderUniforms::SetProgram(MetalShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformCaches.clear();
		_maybeDirty = true;

		if (_shaderProgram->GetStatus() == MetalShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void MetalShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != MetalShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_maybeDirty = true;
		std::uint32_t offset = 0;
		for (MetalUniformCache& cache : _uniformCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void MetalShaderUniforms::SetDirty(bool isDirty)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != MetalShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		_maybeDirty = isDirty;
		for (MetalUniformCache& cache : _uniformCaches) {
			cache.SetDirty(isDirty);
		}
	}

	bool MetalShaderUniforms::HasUniform(const char* name) const
	{
		for (const MetalUniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	MetalUniformCache* MetalShaderUniforms::GetUniform(const char* name)
	{
		for (MetalUniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				_maybeDirty = true;
				return &cache;
			}
		}
		return nullptr;
	}

	void MetalShaderUniforms::CommitUniforms()
	{
		if (_shaderProgram == nullptr) {
			return;
		}
		if (_maybeDirty && _shaderProgram->GetStatus() == MetalShaderProgram::Status::LinkedWithIntrospection) {
			_shaderProgram->Use();
			for (MetalUniformCache& cache : _uniformCaches) {
				cache.CommitValue();
			}
			_maybeDirty = false;
		}
	}

	void MetalShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const MetalUniform& uniform : _shaderProgram->_uniforms) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				_uniformCaches.push_back(MetalUniformCache(&uniform));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	MetalShaderUniformBlocks::UniformRangeAllocator MetalShaderUniformBlocks::_uniformRangeAllocator = nullptr;

	void MetalShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		_uniformRangeAllocator = allocator;
	}

	MetalShaderUniformBlocks::MetalShaderUniformBlocks()
		: _shaderProgram(nullptr), _dataPointer(nullptr)
	{
	}

	MetalShaderUniformBlocks::MetalShaderUniformBlocks(MetalShaderProgram* shaderProgram)
		: MetalShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	MetalShaderUniformBlocks::MetalShaderUniformBlocks(MetalShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: MetalShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void MetalShaderUniformBlocks::SetProgram(MetalShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformBlockCaches.clear();

		if (_shaderProgram->GetStatus() == MetalShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void MetalShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != MetalShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_dataPointer = dataPointer;
		std::int32_t offset = 0;
		for (MetalUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniformBlock()->GetSize() - cache.GetUniformBlock()->GetAlignAmount();
		}
	}

	bool MetalShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const MetalUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.GetUniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	MetalUniformBlockCache* MetalShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (MetalUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.GetUniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void MetalShaderUniformBlocks::CommitUniformBlocks()
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != MetalShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (MetalUniformBlockCache& cache : _uniformBlockCaches) {
			if (cache.GetDataPointer() != _dataPointer + totalUsedSize) {
				hasMemoryGaps = true;
			}
			totalUsedSize += cache.GetUsedSize();
		}

		if (totalUsedSize > 0 && _uniformRangeAllocator != nullptr) {
			_uboParams = _uniformRangeAllocator(std::uint32_t(totalUsedSize));
			if (_uboParams.mapBase != nullptr) {
				if (hasMemoryGaps) {
					std::int32_t offset = 0;
					for (MetalUniformBlockCache& cache : _uniformBlockCaches) {
						std::memcpy(_uboParams.mapBase + _uboParams.offset + offset, cache.GetDataPointer(), cache.GetUsedSize());
						offset += cache.GetUsedSize();
					}
				} else {
					std::memcpy(_uboParams.mapBase + _uboParams.offset, _dataPointer, totalUsedSize);
				}
			}
		}
	}

	void MetalShaderUniformBlocks::Bind()
	{
		if (_uboParams.object == nullptr) {
			return;
		}

		_uboParams.object->Bind();
		std::size_t moreOffset = 0;
		for (MetalUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(_uboParams.offset) + moreOffset;
			_uboParams.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.GetUsedSize()));
			moreOffset += std::size_t(cache.GetUsedSize());
		}
	}

	void MetalShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (MetalUniformBlock& block : _shaderProgram->_uniformBlocks) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				_uniformBlockCaches.push_back(MetalUniformBlockCache(&block));
			}
		}
	}
}
