#include "PvrShaderUniforms.h"
#include "PvrShaderProgram.h"
#include "PvrBuffer.h"

#include <cstring>

namespace nCine::RHI::PVR
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

	PvrShaderUniforms::PvrShaderUniforms()
		: _shaderProgram(nullptr), _maybeDirty(true)
	{
	}

	PvrShaderUniforms::PvrShaderUniforms(PvrShaderProgram* shaderProgram)
		: PvrShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	PvrShaderUniforms::PvrShaderUniforms(PvrShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: PvrShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void PvrShaderUniforms::SetProgram(PvrShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformCaches.clear();
		_maybeDirty = true;

		if (_shaderProgram->GetStatus() == PvrShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void PvrShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != PvrShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_maybeDirty = true;
		std::uint32_t offset = 0;
		for (PvrUniformCache& cache : _uniformCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void PvrShaderUniforms::SetDirty(bool isDirty)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != PvrShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		_maybeDirty = isDirty;
		for (PvrUniformCache& cache : _uniformCaches) {
			cache.SetDirty(isDirty);
		}
	}

	bool PvrShaderUniforms::HasUniform(const char* name) const
	{
		for (const PvrUniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	PvrUniformCache* PvrShaderUniforms::GetUniform(const char* name)
	{
		for (PvrUniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				_maybeDirty = true;
				return &cache;
			}
		}
		return nullptr;
	}

	void PvrShaderUniforms::CommitUniforms()
	{
		if (_shaderProgram == nullptr) {
			return;
		}
		if (_maybeDirty && _shaderProgram->GetStatus() == PvrShaderProgram::Status::LinkedWithIntrospection) {
			_shaderProgram->Use();
			for (PvrUniformCache& cache : _uniformCaches) {
				cache.CommitValue();
			}
			_maybeDirty = false;
		}
	}

	void PvrShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const PvrUniform& uniform : _shaderProgram->_uniforms) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				_uniformCaches.push_back(PvrUniformCache(&uniform));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	PvrShaderUniformBlocks::UniformRangeAllocator PvrShaderUniformBlocks::_uniformRangeAllocator = nullptr;

	void PvrShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		_uniformRangeAllocator = allocator;
	}

	PvrShaderUniformBlocks::PvrShaderUniformBlocks()
		: _shaderProgram(nullptr), _dataPointer(nullptr)
	{
	}

	PvrShaderUniformBlocks::PvrShaderUniformBlocks(PvrShaderProgram* shaderProgram)
		: PvrShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	PvrShaderUniformBlocks::PvrShaderUniformBlocks(PvrShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: PvrShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void PvrShaderUniformBlocks::SetProgram(PvrShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformBlockCaches.clear();

		if (_shaderProgram->GetStatus() == PvrShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void PvrShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != PvrShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_dataPointer = dataPointer;
		std::int32_t offset = 0;
		for (PvrUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool PvrShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const PvrUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	PvrUniformBlockCache* PvrShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (PvrUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void PvrShaderUniformBlocks::CommitUniformBlocks()
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != PvrShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (PvrUniformBlockCache& cache : _uniformBlockCaches) {
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
					for (PvrUniformBlockCache& cache : _uniformBlockCaches) {
						std::memcpy(_uboParams.mapBase + _uboParams.offset + offset, cache.GetDataPointer(), cache.usedSize());
						offset += cache.usedSize();
					}
				} else {
					std::memcpy(_uboParams.mapBase + _uboParams.offset, _dataPointer, totalUsedSize);
				}
			}
		}
	}

	void PvrShaderUniformBlocks::Bind()
	{
		if (_uboParams.object == nullptr) {
			return;
		}

		_uboParams.object->Bind();
		std::size_t moreOffset = 0;
		for (PvrUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(_uboParams.offset) + moreOffset;
			_uboParams.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.usedSize()));
			moreOffset += std::size_t(cache.usedSize());
		}
	}

	void PvrShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (PvrUniformBlock& block : _shaderProgram->_uniformBlocks) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				_uniformBlockCaches.push_back(PvrUniformBlockCache(&block));
			}
		}
	}
}
