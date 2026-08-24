#include "GuShaderUniforms.h"
#include "GuShaderProgram.h"
#include "GuBuffer.h"

#include <cstring>

namespace nCine::RHI::GU
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

	GuShaderUniforms::GuShaderUniforms()
		: _shaderProgram(nullptr), _maybeDirty(true)
	{
	}

	GuShaderUniforms::GuShaderUniforms(GuShaderProgram* shaderProgram)
		: GuShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	GuShaderUniforms::GuShaderUniforms(GuShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: GuShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void GuShaderUniforms::SetProgram(GuShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformCaches.clear();
		_uniformNameHashes.clear();
		_maybeDirty = true;

		if (_shaderProgram->GetStatus() == GuShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void GuShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GuShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_maybeDirty = true;
		std::uint32_t offset = 0;
		for (GuUniformCache& cache : _uniformCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void GuShaderUniforms::SetDirty(bool isDirty)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GuShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		_maybeDirty = isDirty;
		for (GuUniformCache& cache : _uniformCaches) {
			cache.SetDirty(isDirty);
		}
	}

	bool GuShaderUniforms::HasUniform(const char* name) const
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

	GuUniformCache* GuShaderUniforms::GetUniform(const char* name)
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

	void GuShaderUniforms::CommitUniforms()
	{
		if (_shaderProgram == nullptr) {
			return;
		}
		if (_maybeDirty && _shaderProgram->GetStatus() == GuShaderProgram::Status::LinkedWithIntrospection) {
			_shaderProgram->Use();
			for (GuUniformCache& cache : _uniformCaches) {
				cache.CommitValue();
			}
			_maybeDirty = false;
		}
	}

	void GuShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const GuUniform& uniform : _shaderProgram->_uniforms) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				_uniformCaches.push_back(GuUniformCache(&uniform));
				_uniformNameHashes.push_back(HashUniformName(uniform.GetName()));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	GuShaderUniformBlocks::UniformRangeAllocator GuShaderUniformBlocks::_uniformRangeAllocator = nullptr;

	void GuShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		_uniformRangeAllocator = allocator;
	}

	GuShaderUniformBlocks::GuShaderUniformBlocks()
		: _shaderProgram(nullptr), _dataPointer(nullptr)
	{
	}

	GuShaderUniformBlocks::GuShaderUniformBlocks(GuShaderProgram* shaderProgram)
		: GuShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	GuShaderUniformBlocks::GuShaderUniformBlocks(GuShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: GuShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void GuShaderUniformBlocks::SetProgram(GuShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformBlockCaches.clear();

		if (_shaderProgram->GetStatus() == GuShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void GuShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GuShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_dataPointer = dataPointer;
		std::int32_t offset = 0;
		for (GuUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool GuShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const GuUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	GuUniformBlockCache* GuShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (GuUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void GuShaderUniformBlocks::CommitUniformBlocks()
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GuShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (GuUniformBlockCache& cache : _uniformBlockCaches) {
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
					for (GuUniformBlockCache& cache : _uniformBlockCaches) {
						std::memcpy(_uboParams.mapBase + _uboParams.offset + offset, cache.GetDataPointer(), cache.usedSize());
						offset += cache.usedSize();
					}
				} else {
					std::memcpy(_uboParams.mapBase + _uboParams.offset, _dataPointer, totalUsedSize);
				}
			}
		}
	}

	void GuShaderUniformBlocks::Bind()
	{
		if (_uboParams.object == nullptr) {
			return;
		}

		_uboParams.object->Bind();
		std::size_t moreOffset = 0;
		for (GuUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(_uboParams.offset) + moreOffset;
			_uboParams.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.usedSize()));
			moreOffset += std::size_t(cache.usedSize());
		}
	}

	void GuShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (GuUniformBlock& block : _shaderProgram->_uniformBlocks) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				_uniformBlockCaches.push_back(GuUniformBlockCache(&block));
			}
		}
	}
}
