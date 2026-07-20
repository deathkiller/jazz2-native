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
		: shaderProgram_(nullptr), maybeDirty_(true)
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
		shaderProgram_ = shaderProgram;
		uniformCaches_.clear();
		maybeDirty_ = true;

		if (shaderProgram_->GetStatus() == D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniforms(includeOnly, exclude);
		}
	}

	void D3D11ShaderUniforms::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		maybeDirty_ = true;
		std::uint32_t offset = 0;
		for (D3D11UniformCache& cache : uniformCaches_) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.GetUniform()->GetMemorySize();
		}
	}

	void D3D11ShaderUniforms::SetDirty(bool isDirty)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}
		maybeDirty_ = isDirty;
		for (D3D11UniformCache& cache : uniformCaches_) {
			cache.SetDirty(isDirty);
		}
	}

	bool D3D11ShaderUniforms::HasUniform(const char* name) const
	{
		for (const D3D11UniformCache& cache : uniformCaches_) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	D3D11UniformCache* D3D11ShaderUniforms::GetUniform(const char* name)
	{
		for (D3D11UniformCache& cache : uniformCaches_) {
			if (std::strcmp(cache.GetUniform()->GetName(), name) == 0) {
				maybeDirty_ = true;
				return &cache;
			}
		}
		return nullptr;
	}

	void D3D11ShaderUniforms::CommitUniforms()
	{
		if (shaderProgram_ == nullptr) {
			return;
		}
		if (maybeDirty_ && shaderProgram_->GetStatus() == D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			shaderProgram_->Use();
			for (D3D11UniformCache& cache : uniformCaches_) {
				cache.CommitValue();
			}
			maybeDirty_ = false;
		}
	}

	void D3D11ShaderUniforms::ImportUniforms(const char* includeOnly, const char* exclude)
	{
		for (const D3D11Uniform& uniform : shaderProgram_->uniforms_) {
			if (ShouldImport(uniform.GetName(), includeOnly, exclude)) {
				uniformCaches_.push_back(D3D11UniformCache(&uniform));
			}
		}
	}

	// -------------------------------------------------------------------------------------------------

	D3D11ShaderUniformBlocks::UniformRangeAllocator D3D11ShaderUniformBlocks::uniformRangeAllocator_ = nullptr;

	void D3D11ShaderUniformBlocks::SetUniformRangeAllocator(UniformRangeAllocator allocator)
	{
		uniformRangeAllocator_ = allocator;
	}

	D3D11ShaderUniformBlocks::D3D11ShaderUniformBlocks()
		: shaderProgram_(nullptr), dataPointer_(nullptr)
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
		shaderProgram_ = shaderProgram;
		uniformBlockCaches_.clear();

		if (shaderProgram_->GetStatus() == D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void D3D11ShaderUniformBlocks::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		dataPointer_ = dataPointer;
		std::int32_t offset = 0;
		for (D3D11UniformBlockCache& cache : uniformBlockCaches_) {
			cache.SetDataPointer(dataPointer + offset);
			offset += cache.uniformBlock()->GetSize() - cache.uniformBlock()->GetAlignAmount();
		}
	}

	bool D3D11ShaderUniformBlocks::HasUniformBlock(const char* name) const
	{
		for (const D3D11UniformBlockCache& cache : uniformBlockCaches_) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	D3D11UniformBlockCache* D3D11ShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		for (D3D11UniformBlockCache& cache : uniformBlockCaches_) {
			if (std::strcmp(cache.uniformBlock()->GetName(), name) == 0) {
				return &cache;
			}
		}
		return nullptr;
	}

	void D3D11ShaderUniformBlocks::CommitUniformBlocks()
	{
		if (shaderProgram_ == nullptr || shaderProgram_->GetStatus() != D3D11ShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		std::int32_t totalUsedSize = 0;
		bool hasMemoryGaps = false;
		for (D3D11UniformBlockCache& cache : uniformBlockCaches_) {
			if (cache.GetDataPointer() != dataPointer_ + totalUsedSize) {
				hasMemoryGaps = true;
			}
			totalUsedSize += cache.usedSize();
		}

		if (totalUsedSize > 0 && uniformRangeAllocator_ != nullptr) {
			uboParams_ = uniformRangeAllocator_(std::uint32_t(totalUsedSize));
			if (uboParams_.mapBase != nullptr) {
				if (hasMemoryGaps) {
					std::int32_t offset = 0;
					for (D3D11UniformBlockCache& cache : uniformBlockCaches_) {
						std::memcpy(uboParams_.mapBase + uboParams_.offset + offset, cache.GetDataPointer(), cache.usedSize());
						offset += cache.usedSize();
					}
				} else {
					std::memcpy(uboParams_.mapBase + uboParams_.offset, dataPointer_, totalUsedSize);
				}
			}
		}
	}

	void D3D11ShaderUniformBlocks::Bind()
	{
		if (uboParams_.object == nullptr) {
			return;
		}

		uboParams_.object->Bind();
		std::size_t moreOffset = 0;
		for (D3D11UniformBlockCache& cache : uniformBlockCaches_) {
			cache.SetBlockBinding(std::int32_t(cache.GetIndex()));
			const std::size_t offset = std::size_t(uboParams_.offset) + moreOffset;
			uboParams_.object->BindBufferRange(std::uint32_t(cache.GetBindingIndex()), offset, std::size_t(cache.usedSize()));
			moreOffset += std::size_t(cache.usedSize());
		}
	}

	void D3D11ShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		for (D3D11UniformBlock& block : shaderProgram_->uniformBlocks_) {
			if (ShouldImport(block.GetName(), includeOnly, exclude)) {
				uniformBlockCaches_.push_back(D3D11UniformBlockCache(&block));
			}
		}
	}
}
