#include "PvrShaderUniforms.h"
#include "PvrShaderProgram.h"
#include "PvrBuffer.h"
#include "PvrDevice.h"

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
		_uniformNameHashes.clear();
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

	PvrUniformCache* PvrShaderUniforms::GetUniform(const char* name)
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
				_uniformNameHashes.push_back(HashUniformName(uniform.GetName()));
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
		// Nothing to commit. The other backends copy the block contents into a streaming uniform buffer
		// because only the GPU reads them from there; on this tier the reader is the draw dispatch running
		// on the same CPU, and it takes the bytes through the plain pointer @ref Bind() forwards. Staging
		// them through a uniform range copied the largest per-frame payload the engine has - every batch's
		// whole instance array - once more each frame for bytes that never moved anywhere (the same finding
		// as the RDP backend's).
	}

	void PvrShaderUniformBlocks::Bind()
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != PvrShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		// Each cache owns its own contiguous storage, so a block is forwarded where it already lives -
		// which also means a non-contiguous set of blocks needs no gap handling at all
		for (PvrUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::uint8_t* data = cache.GetDataPointer();
			if (data != nullptr) {
				PvrDevice::BindUniformRange(std::uint32_t(cache.GetBindingIndex()), data, std::uint32_t(cache.usedSize()));
			}
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
