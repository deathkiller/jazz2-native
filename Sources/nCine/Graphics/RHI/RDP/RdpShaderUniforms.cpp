#include "RdpShaderUniforms.h"
#include "RdpShaderProgram.h"
#include "RdpBuffer.h"
#include "RdpDevice.h"

#include <cstring>

namespace nCine::RHI::RDP
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

	RdpShaderUniforms::RdpShaderUniforms()
		: _shaderProgram(nullptr), _maybeDirty(true)
	{
	}

	RdpShaderUniforms::RdpShaderUniforms(RdpShaderProgram* shaderProgram)
		: RdpShaderUniforms()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	RdpShaderUniforms::RdpShaderUniforms(RdpShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: RdpShaderUniforms()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void RdpShaderUniforms::SetProgram(RdpShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformCaches.clear();
		_uniformNameHashes.clear();
		_maybeDirty = true;

		if (_shaderProgram->GetStatus() == RdpShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void RdpShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != RdpShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_maybeDirty = true;
		std::uint32_t offset = 0;
		for (RdpUniformCache& cache : _uniformCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void RdpShaderUniforms::SetDirty(bool isDirty)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != RdpShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		_maybeDirty = isDirty;
		for (RdpUniformCache& cache : _uniformCaches) {
			cache.SetDirty(isDirty);
		}
	}

	bool RdpShaderUniforms::HasUniform(const char* name) const
	{
		// Fingerprints first, the name itself only where one matches (see RHI::HashUniformName)
		const std::uint32_t hash = HashUniformName(name);
		for (std::size_t i = 0; i < _uniformNameHashes.size(); i++) {
			if (_uniformNameHashes[i] == hash && std::strcmp(_uniformCaches[i].GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	RdpUniformCache* RdpShaderUniforms::GetUniform(const char* name)
	{
		const std::uint32_t hash = HashUniformName(name);
		for (std::size_t i = 0; i < _uniformNameHashes.size(); i++) {
			if (_uniformNameHashes[i] == hash && std::strcmp(_uniformCaches[i].GetUniform()->GetName(), name) == 0) {
				_maybeDirty = true;
				return &_uniformCaches[i];
			}
		}
		return nullptr;
	}

	void RdpShaderUniforms::CommitUniforms()
	{
		if (_shaderProgram == nullptr) {
			return;
		}
		if (_maybeDirty && _shaderProgram->GetStatus() == RdpShaderProgram::Status::LinkedWithIntrospection) {
			_shaderProgram->Use();
			for (RdpUniformCache& cache : _uniformCaches) {
				cache.CommitValue();
			}
			_maybeDirty = false;
		}
	}

	void RdpShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const RdpUniform& uniform : _shaderProgram->_uniforms) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				_uniformCaches.push_back(RdpUniformCache(&uniform));
				_uniformNameHashes.push_back(HashUniformName(uniform.GetName()));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	RdpShaderUniformBlocks::UniformRangeAllocator RdpShaderUniformBlocks::_uniformRangeAllocator = nullptr;

	void RdpShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		_uniformRangeAllocator = allocator;
	}

	RdpShaderUniformBlocks::RdpShaderUniformBlocks()
		: _shaderProgram(nullptr), _dataPointer(nullptr)
	{
	}

	RdpShaderUniformBlocks::RdpShaderUniformBlocks(RdpShaderProgram* shaderProgram)
		: RdpShaderUniformBlocks()
	{
		SetProgram(shaderProgram, nullptr, nullptr);
	}

	RdpShaderUniformBlocks::RdpShaderUniformBlocks(RdpShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: RdpShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void RdpShaderUniformBlocks::SetProgram(RdpShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		_shaderProgram = shaderProgram;
		_uniformBlockCaches.clear();

		if (_shaderProgram->GetStatus() == RdpShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void RdpShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != RdpShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		_dataPointer = dataPointer;
		std::int32_t offset = 0;
		for (RdpUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool RdpShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const RdpUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	RdpUniformBlockCache* RdpShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (RdpUniformBlockCache& cache : _uniformBlockCaches) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void RdpShaderUniformBlocks::CommitUniformBlocks()
	{
		// Nothing to commit. The other backends copy the block contents into a streaming uniform buffer
		// because only the GPU reads them from there; on this tier the "GPU" is the draw dispatch running
		// on the same CPU, and it reads the bytes through the plain pointer @ref Bind() forwards. Staging
		// them through a uniform range was therefore two full copies of the same data every frame - the
		// range allocator's, and RenderBuffersManager::FlushUnmap()'s host copy behind it - of the largest
		// per-frame payload the engine has (a batch's whole instance array). On the main menu that was
		// ~20 KB copied twice per frame, 3.7 ms of a 42 ms frame, for bytes that never moved anywhere.
	}

	void RdpShaderUniformBlocks::Bind()
	{
		if (_shaderProgram == nullptr || _shaderProgram->GetStatus() != RdpShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		// Each cache owns its own contiguous storage, so a block is forwarded where it already lives -
		// which also means a non-contiguous set of blocks needs no gap handling at all
		for (RdpUniformBlockCache& cache : _uniformBlockCaches) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::uint8_t* data = cache.GetDataPointer();
			if (data != nullptr) {
				RdpDevice::BindUniformRange(std::uint32_t(cache.GetBindingIndex()), data, std::uint32_t(cache.usedSize()));
			}
		}
	}

	void RdpShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (RdpUniformBlock& block : _shaderProgram->_uniformBlocks) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				_uniformBlockCaches.push_back(RdpUniformBlockCache(&block));
			}
		}
	}
}
