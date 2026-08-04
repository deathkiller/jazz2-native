#include "GxShaderUniforms.h"
#include "GxShaderProgram.h"
#include "GxBuffer.h"

#include <cstring>

namespace nCine::RHI::GX
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

	GxShaderUniforms::GxShaderUniforms()
		: _shaderProgram(nullptr), _maybeDirty(true)
	{
	}

	GxShaderUniforms::GxShaderUniforms(GxShaderProgram* shaderProgram)
		: GxShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	GxShaderUniforms::GxShaderUniforms(GxShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: GxShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void GxShaderUniforms::SetProgram(GxShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformCaches.clear();
		_maybeDirty = true;

		if (_shaderProgram->GetStatus() == GxShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void GxShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GxShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_maybeDirty = true;
		std::uint32_t offset = 0;
		for (GxUniformCache& cache : _uniformCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void GxShaderUniforms::SetDirty(bool isDirty)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GxShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		_maybeDirty = isDirty;
		for (GxUniformCache& cache : _uniformCaches) {
			cache.SetDirty(isDirty);
		}
	}

	bool GxShaderUniforms::HasUniform(const char* name) const
	{
		for (const GxUniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	GxUniformCache* GxShaderUniforms::GetUniform(const char* name)
	{
		for (GxUniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				_maybeDirty = true;
				return &cache;
			}
		}
		return nullptr;
	}

	void GxShaderUniforms::CommitUniforms()
	{
		if (_shaderProgram == nullptr) {
			return;
		}
		if (_maybeDirty && _shaderProgram->GetStatus() == GxShaderProgram::Status::LinkedWithIntrospection) {
			_shaderProgram->Use();
			for (GxUniformCache& cache : _uniformCaches) {
				cache.CommitValue();
			}
			_maybeDirty = false;
		}
	}

	void GxShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const GxUniform& uniform : _shaderProgram->_uniforms) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				_uniformCaches.push_back(GxUniformCache(&uniform));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	GxShaderUniformBlocks::UniformRangeAllocator GxShaderUniformBlocks::_uniformRangeAllocator = nullptr;

	void GxShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		_uniformRangeAllocator = allocator;
	}

	GxShaderUniformBlocks::GxShaderUniformBlocks()
		: _shaderProgram(nullptr), _dataPointer(nullptr)
	{
	}

	GxShaderUniformBlocks::GxShaderUniformBlocks(GxShaderProgram* shaderProgram)
		: GxShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	GxShaderUniformBlocks::GxShaderUniformBlocks(GxShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: GxShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void GxShaderUniformBlocks::SetProgram(GxShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformBlockCaches.clear();

		if (_shaderProgram->GetStatus() == GxShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void GxShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GxShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_dataPointer = dataPointer;
		std::int32_t offset = 0;
		for (GxUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool GxShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const GxUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	GxUniformBlockCache* GxShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (GxUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void GxShaderUniformBlocks::CommitUniformBlocks()
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GxShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (GxUniformBlockCache& cache : _uniformBlockCaches) {
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
					for (GxUniformBlockCache& cache : _uniformBlockCaches) {
						std::memcpy(_uboParams.mapBase + _uboParams.offset + offset, cache.GetDataPointer(), cache.usedSize());
						offset += cache.usedSize();
					}
				} else {
					std::memcpy(_uboParams.mapBase + _uboParams.offset, _dataPointer, totalUsedSize);
				}
			}
		}
	}

	void GxShaderUniformBlocks::Bind()
	{
		if (_uboParams.object == nullptr) {
			return;
		}

		_uboParams.object->Bind();
		std::size_t moreOffset = 0;
		for (GxUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(_uboParams.offset) + moreOffset;
			_uboParams.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.usedSize()));
			moreOffset += std::size_t(cache.usedSize());
		}
	}

	void GxShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (GxUniformBlock& block : _shaderProgram->_uniformBlocks) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				_uniformBlockCaches.push_back(GxUniformBlockCache(&block));
			}
		}
	}
}
