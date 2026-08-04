#include "GxmShaderUniforms.h"
#include "GxmShaderProgram.h"
#include "GxmBufferObject.h"

#include <cstring>

namespace nCine::RHI::GXM
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

	GxmShaderUniforms::GxmShaderUniforms()
		: _shaderProgram(nullptr), _maybeDirty(true)
	{
	}

	GxmShaderUniforms::GxmShaderUniforms(GxmShaderProgram* shaderProgram)
		: GxmShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	GxmShaderUniforms::GxmShaderUniforms(GxmShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: GxmShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void GxmShaderUniforms::SetProgram(GxmShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformCaches.clear();
		_maybeDirty = true;

		if (_shaderProgram->GetStatus() == GxmShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void GxmShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GxmShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_maybeDirty = true;
		std::uint32_t offset = 0;
		for (GxmUniformCache& cache : _uniformCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void GxmShaderUniforms::SetDirty(bool isDirty)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GxmShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		_maybeDirty = isDirty;
		for (GxmUniformCache& cache : _uniformCaches) {
			cache.SetDirty(isDirty);
		}
	}

	bool GxmShaderUniforms::HasUniform(const char* name) const
	{
		for (const GxmUniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	GxmUniformCache* GxmShaderUniforms::GetUniform(const char* name)
	{
		for (GxmUniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				_maybeDirty = true;
				return &cache;
			}
		}
		return nullptr;
	}

	void GxmShaderUniforms::CommitUniforms()
	{
		if (_shaderProgram == nullptr) {
			return;
		}
		if (_maybeDirty && _shaderProgram->GetStatus() == GxmShaderProgram::Status::LinkedWithIntrospection) {
			_shaderProgram->Use();
			for (GxmUniformCache& cache : _uniformCaches) {
				cache.CommitValue();
			}
			_maybeDirty = false;
		}
	}

	void GxmShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const GxmUniform& uniform : _shaderProgram->_uniforms) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				_uniformCaches.push_back(GxmUniformCache(&uniform));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	GxmShaderUniformBlocks::UniformRangeAllocator GxmShaderUniformBlocks::_uniformRangeAllocator = nullptr;

	void GxmShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		_uniformRangeAllocator = allocator;
	}

	GxmShaderUniformBlocks::GxmShaderUniformBlocks()
		: _shaderProgram(nullptr), _dataPointer(nullptr)
	{
	}

	GxmShaderUniformBlocks::GxmShaderUniformBlocks(GxmShaderProgram* shaderProgram)
		: GxmShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	GxmShaderUniformBlocks::GxmShaderUniformBlocks(GxmShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: GxmShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void GxmShaderUniformBlocks::SetProgram(GxmShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformBlockCaches.clear();

		if (_shaderProgram->GetStatus() == GxmShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void GxmShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GxmShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_dataPointer = dataPointer;
		std::int32_t offset = 0;
		for (GxmUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool GxmShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const GxmUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	GxmUniformBlockCache* GxmShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (GxmUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void GxmShaderUniformBlocks::CommitUniformBlocks()
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GxmShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (GxmUniformBlockCache& cache : _uniformBlockCaches) {
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
					for (GxmUniformBlockCache& cache : _uniformBlockCaches) {
						std::memcpy(_uboParams.mapBase + _uboParams.offset + offset, cache.GetDataPointer(), cache.usedSize());
						offset += cache.usedSize();
					}
				} else {
					std::memcpy(_uboParams.mapBase + _uboParams.offset, _dataPointer, totalUsedSize);
				}
			}
		}
	}

	void GxmShaderUniformBlocks::Bind()
	{
		if (_uboParams.object == nullptr) {
			return;
		}

		_uboParams.object->Bind();
		std::size_t moreOffset = 0;
		for (GxmUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(_uboParams.offset) + moreOffset;
			_uboParams.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.usedSize()));
			moreOffset += std::size_t(cache.usedSize());
		}
	}

	void GxmShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (GxmUniformBlock& block : _shaderProgram->_uniformBlocks) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				_uniformBlockCaches.push_back(GxmUniformBlockCache(&block));
			}
		}
	}
}
