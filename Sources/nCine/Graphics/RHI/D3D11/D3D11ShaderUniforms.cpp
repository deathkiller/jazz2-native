#include "D3D11ShaderUniforms.h"
#include "D3D11ShaderProgram.h"
#include "D3D11BufferObject.h"

#include <cstring>

namespace nCine::RHI::D3D11
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

	D3D11ShaderUniforms::D3D11ShaderUniforms()
		: _shaderProgram(nullptr), _maybeDirty(true)
	{
	}

	D3D11ShaderUniforms::D3D11ShaderUniforms(D3D11ShaderProgram* shaderProgram)
		: D3D11ShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	D3D11ShaderUniforms::D3D11ShaderUniforms(D3D11ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: D3D11ShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void D3D11ShaderUniforms::SetProgram(D3D11ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformCaches.clear();
		_maybeDirty = true;

		if (_shaderProgram->GetStatus() == D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void D3D11ShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_maybeDirty = true;
		std::uint32_t offset = 0;
		for (D3D11UniformCache& cache : _uniformCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void D3D11ShaderUniforms::SetDirty(bool isDirty)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		_maybeDirty = isDirty;
		for (D3D11UniformCache& cache : _uniformCaches) {
			cache.SetDirty(isDirty);
		}
	}

	bool D3D11ShaderUniforms::HasUniform(const char* name) const
	{
		for (const D3D11UniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	D3D11UniformCache* D3D11ShaderUniforms::GetUniform(const char* name)
	{
		for (D3D11UniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				_maybeDirty = true;
				return &cache;
			}
		}
		return nullptr;
	}

	void D3D11ShaderUniforms::CommitUniforms()
	{
		if (_shaderProgram == nullptr) {
			return;
		}
		if (_maybeDirty && _shaderProgram->GetStatus() == D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			_shaderProgram->Use();
			for (D3D11UniformCache& cache : _uniformCaches) {
				cache.CommitValue();
			}
			_maybeDirty = false;
		}
	}

	void D3D11ShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const D3D11Uniform& uniform : _shaderProgram->_uniforms) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				_uniformCaches.push_back(D3D11UniformCache(&uniform));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	D3D11ShaderUniformBlocks::UniformRangeAllocator D3D11ShaderUniformBlocks::_uniformRangeAllocator = nullptr;

	void D3D11ShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		_uniformRangeAllocator = allocator;
	}

	D3D11ShaderUniformBlocks::D3D11ShaderUniformBlocks()
		: _shaderProgram(nullptr), _dataPointer(nullptr)
	{
	}

	D3D11ShaderUniformBlocks::D3D11ShaderUniformBlocks(D3D11ShaderProgram* shaderProgram)
		: D3D11ShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	D3D11ShaderUniformBlocks::D3D11ShaderUniformBlocks(D3D11ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: D3D11ShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void D3D11ShaderUniformBlocks::SetProgram(D3D11ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformBlockCaches.clear();

		if (_shaderProgram->GetStatus() == D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void D3D11ShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_dataPointer = dataPointer;
		std::int32_t offset = 0;
		for (D3D11UniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool D3D11ShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const D3D11UniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	D3D11UniformBlockCache* D3D11ShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (D3D11UniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void D3D11ShaderUniformBlocks::CommitUniformBlocks()
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (D3D11UniformBlockCache& cache : _uniformBlockCaches) {
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
					for (D3D11UniformBlockCache& cache : _uniformBlockCaches) {
						std::memcpy(_uboParams.mapBase + _uboParams.offset + offset, cache.GetDataPointer(), cache.usedSize());
						offset += cache.usedSize();
					}
				} else {
					std::memcpy(_uboParams.mapBase + _uboParams.offset, _dataPointer, totalUsedSize);
				}
			}
		}
	}

	void D3D11ShaderUniformBlocks::Bind()
	{
		if (_uboParams.object == nullptr) {
			return;
		}

		_uboParams.object->Bind();
		std::size_t moreOffset = 0;
		for (D3D11UniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(_uboParams.offset) + moreOffset;
			_uboParams.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.usedSize()));
			moreOffset += std::size_t(cache.usedSize());
		}
	}

	void D3D11ShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (D3D11UniformBlock& block : _shaderProgram->_uniformBlocks) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				_uniformBlockCaches.push_back(D3D11UniformBlockCache(&block));
			}
		}
	}
}
