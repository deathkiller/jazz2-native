#include "PicaShaderUniforms.h"
#include "PicaShaderProgram.h"
#include "PicaBuffer.h"

#include <cstring>

namespace nCine::RHI::PICA
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

	PicaShaderUniforms::PicaShaderUniforms()
		: _shaderProgram(nullptr), _maybeDirty(true)
	{
	}

	PicaShaderUniforms::PicaShaderUniforms(PicaShaderProgram* shaderProgram)
		: PicaShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	PicaShaderUniforms::PicaShaderUniforms(PicaShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: PicaShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void PicaShaderUniforms::SetProgram(PicaShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformCaches.clear();
		_uniformNameHashes.clear();
		_maybeDirty = true;

		if (_shaderProgram->GetStatus() == PicaShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void PicaShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != PicaShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_maybeDirty = true;
		std::uint32_t offset = 0;
		for (PicaUniformCache& cache : _uniformCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void PicaShaderUniforms::SetDirty(bool isDirty)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != PicaShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		_maybeDirty = isDirty;
		for (PicaUniformCache& cache : _uniformCaches) {
			cache.SetDirty(isDirty);
		}
	}

	bool PicaShaderUniforms::HasUniform(const char* name) const
	{
		// Fingerprints first, the name itself only on the entry that matched (see @ref HashUniformName)
		const std::uint32_t hash = HashUniformName(name);
		const std::size_t count = _uniformNameHashes.size();
		for (std::size_t i = 0; i < count; i++) {
			if (_uniformNameHashes[i] == hash && std::strcmp(_uniformCaches[i].GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	PicaUniformCache* PicaShaderUniforms::GetUniform(const char* name)
	{
		const std::uint32_t hash = HashUniformName(name);
		const std::size_t count = _uniformNameHashes.size();
		for (std::size_t i = 0; i < count; i++) {
			if (_uniformNameHashes[i] == hash && std::strcmp(_uniformCaches[i].GetUniform()->GetName(), name) == 0) {
				_maybeDirty = true;
				return &_uniformCaches[i];
			}
		}
		return nullptr;
	}

	void PicaShaderUniforms::CommitUniforms()
	{
		if (_shaderProgram == nullptr) {
			return;
		}
		if (_maybeDirty && _shaderProgram->GetStatus() == PicaShaderProgram::Status::LinkedWithIntrospection) {
			_shaderProgram->Use();
			for (PicaUniformCache& cache : _uniformCaches) {
				cache.CommitValue();
			}
			_maybeDirty = false;
		}
	}

	void PicaShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const PicaUniform& uniform : _shaderProgram->_uniforms) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				_uniformCaches.push_back(PicaUniformCache(&uniform));
				_uniformNameHashes.push_back(HashUniformName(uniform.GetName()));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	PicaShaderUniformBlocks::UniformRangeAllocator PicaShaderUniformBlocks::_uniformRangeAllocator = nullptr;

	void PicaShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		_uniformRangeAllocator = allocator;
	}

	PicaShaderUniformBlocks::PicaShaderUniformBlocks()
		: _shaderProgram(nullptr), _dataPointer(nullptr)
	{
	}

	PicaShaderUniformBlocks::PicaShaderUniformBlocks(PicaShaderProgram* shaderProgram)
		: PicaShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	PicaShaderUniformBlocks::PicaShaderUniformBlocks(PicaShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: PicaShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void PicaShaderUniformBlocks::SetProgram(PicaShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformBlockCaches.clear();

		if (_shaderProgram->GetStatus() == PicaShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void PicaShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != PicaShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_dataPointer = dataPointer;
		std::int32_t offset = 0;
		for (PicaUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool PicaShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const PicaUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	PicaUniformBlockCache* PicaShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (PicaUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void PicaShaderUniformBlocks::CommitUniformBlocks()
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != PicaShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (PicaUniformBlockCache& cache : _uniformBlockCaches) {
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
					for (PicaUniformBlockCache& cache : _uniformBlockCaches) {
						std::memcpy(_uboParams.mapBase + _uboParams.offset + offset, cache.GetDataPointer(), cache.usedSize());
						offset += cache.usedSize();
					}
				} else {
					std::memcpy(_uboParams.mapBase + _uboParams.offset, _dataPointer, totalUsedSize);
				}
			}
		}
	}

	void PicaShaderUniformBlocks::Bind()
	{
		if (_uboParams.object == nullptr) {
			return;
		}

		_uboParams.object->Bind();
		std::size_t moreOffset = 0;
		for (PicaUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(_uboParams.offset) + moreOffset;
			_uboParams.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.usedSize()));
			moreOffset += std::size_t(cache.usedSize());
		}
	}

	void PicaShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (PicaUniformBlock& block : _shaderProgram->_uniformBlocks) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				_uniformBlockCaches.push_back(PicaUniformBlockCache(&block));
			}
		}
	}
}
