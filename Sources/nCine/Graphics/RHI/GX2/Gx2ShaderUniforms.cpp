#include "Gx2ShaderUniforms.h"
#include "Gx2ShaderProgram.h"
#include "Gx2BufferObject.h"

#include <cstring>

namespace nCine::RHI::GX2
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

	Gx2ShaderUniforms::Gx2ShaderUniforms()
		: _shaderProgram(nullptr), _maybeDirty(true)
	{
	}

	Gx2ShaderUniforms::Gx2ShaderUniforms(Gx2ShaderProgram* shaderProgram)
		: Gx2ShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	Gx2ShaderUniforms::Gx2ShaderUniforms(Gx2ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: Gx2ShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void Gx2ShaderUniforms::SetProgram(Gx2ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformCaches.clear();
		_maybeDirty = true;

		if (_shaderProgram->GetStatus() == Gx2ShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void Gx2ShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != Gx2ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_maybeDirty = true;
		std::uint32_t offset = 0;
		for (Gx2UniformCache& cache : _uniformCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void Gx2ShaderUniforms::SetDirty(bool isDirty)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != Gx2ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		_maybeDirty = isDirty;
		for (Gx2UniformCache& cache : _uniformCaches) {
			cache.SetDirty(isDirty);
		}
	}

	bool Gx2ShaderUniforms::HasUniform(const char* name) const
	{
		for (const Gx2UniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	Gx2UniformCache* Gx2ShaderUniforms::GetUniform(const char* name)
	{
		for (Gx2UniformCache& cache : _uniformCaches) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				_maybeDirty = true;
				return &cache;
			}
		}
		return nullptr;
	}

	void Gx2ShaderUniforms::CommitUniforms()
	{
		if (_shaderProgram == nullptr) {
			return;
		}
		if (_maybeDirty && _shaderProgram->GetStatus() == Gx2ShaderProgram::Status::LinkedWithIntrospection) {
			_shaderProgram->Use();
			for (Gx2UniformCache& cache : _uniformCaches) {
				cache.CommitValue();
			}
			_maybeDirty = false;
		}
	}

	void Gx2ShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const Gx2Uniform& uniform : _shaderProgram->_uniforms) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				_uniformCaches.push_back(Gx2UniformCache(&uniform));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	Gx2ShaderUniformBlocks::UniformRangeAllocator Gx2ShaderUniformBlocks::_uniformRangeAllocator = nullptr;

	void Gx2ShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		_uniformRangeAllocator = allocator;
	}

	Gx2ShaderUniformBlocks::Gx2ShaderUniformBlocks()
		: _shaderProgram(nullptr), _dataPointer(nullptr)
	{
	}

	Gx2ShaderUniformBlocks::Gx2ShaderUniformBlocks(Gx2ShaderProgram* shaderProgram)
		: Gx2ShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	Gx2ShaderUniformBlocks::Gx2ShaderUniformBlocks(Gx2ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: Gx2ShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void Gx2ShaderUniformBlocks::SetProgram(Gx2ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformBlockCaches.clear();

		if (_shaderProgram->GetStatus() == Gx2ShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void Gx2ShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != Gx2ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_dataPointer = dataPointer;
		std::int32_t offset = 0;
		for (Gx2UniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniformBlock()->GetSize() - cache.GetUniformBlock()->GetAlignAmount();
		}
	}

	bool Gx2ShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const Gx2UniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.GetUniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	Gx2UniformBlockCache* Gx2ShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (Gx2UniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.GetUniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void Gx2ShaderUniformBlocks::CommitUniformBlocks()
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != Gx2ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (Gx2UniformBlockCache& cache : _uniformBlockCaches) {
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
					for (Gx2UniformBlockCache& cache : _uniformBlockCaches) {
						std::memcpy(_uboParams.mapBase + _uboParams.offset + offset, cache.GetDataPointer(), cache.GetUsedSize());
						offset += cache.GetUsedSize();
					}
				} else {
					std::memcpy(_uboParams.mapBase + _uboParams.offset, _dataPointer, totalUsedSize);
				}
			}
		}
	}

	void Gx2ShaderUniformBlocks::Bind()
	{
		if (_uboParams.object == nullptr) {
			return;
		}

		_uboParams.object->Bind();
		std::size_t moreOffset = 0;
		for (Gx2UniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(_uboParams.offset) + moreOffset;
			_uboParams.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.GetUsedSize()));
			moreOffset += std::size_t(cache.GetUsedSize());
		}
	}

	void Gx2ShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (Gx2UniformBlock& block : _shaderProgram->_uniformBlocks) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				_uniformBlockCaches.push_back(Gx2UniformBlockCache(&block));
			}
		}
	}
}
