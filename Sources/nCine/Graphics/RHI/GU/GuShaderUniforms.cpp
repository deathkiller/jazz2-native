#include "GuShaderUniforms.h"
#include "GuShaderProgram.h"
#include "GuDevice.h"

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
		// Nothing to commit, for the RDP's reason (see RdpShaderUniformBlocks): the reader of the blocks is the
		// draw dispatch on this same CPU, which takes the bytes through the plain pointers Bind() forwards. Staging
		// them in a range of the streaming uniform buffer first was a copy of every block of every command, the
		// whole instance array of each batch included, for bytes that were already where the dispatch reads them.
	}

	void GuShaderUniformBlocks::Bind()
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != GuShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		// Each cache owns contiguous storage, so a block is forwarded where it already lives - which also means
		// a non-contiguous set of blocks needs no gap handling
		for (GuUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::uint8_t* data = cache.GetDataPointer();
			if (data != nullptr) {
				GuDevice::BindUniformRange(std::uint32_t(cache.GetBindingIndex()), data, std::uint32_t(cache.usedSize()));
			}
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
