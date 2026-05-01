#if defined(WITH_RHI_GL)
#include "UniformBlockCache.h"
#include "UniformBlock.h"
#include "../../../../Main.h"

namespace nCine::RHI
{
	UniformBlockCache::UniformBlockCache()
		: uniformBlock_(nullptr), dataPointer_(nullptr), usedSize_(0)
	{
	}

	UniformBlockCache::UniformBlockCache(UniformBlock* uniformBlock)
		: uniformBlock_(uniformBlock), dataPointer_(nullptr), usedSize_(0)
	{
		DEATH_ASSERT(uniformBlock);
		usedSize_ = uniformBlock->GetSize();

		static_assert(UniformHashSize >= UniformBlock::BlockUniformHashSize, "Uniform cache is smaller than the number of uniforms");

		for (const Uniform& uniform : uniformBlock->blockUniforms_) {
			UniformCache uniformCache(&uniform);
			uniformCaches_[uniform.GetName()] = uniformCache;
		}
	}

	GLuint UniformBlockCache::GetIndex() const
	{
		GLuint index = 0;
		if (uniformBlock_ != nullptr) {
			index = uniformBlock_->GetIndex();
		}
		return index;
	}

	GLuint UniformBlockCache::GetBindingIndex() const
	{
		GLuint bindingIndex = 0;
		if (uniformBlock_ != nullptr) {
			bindingIndex = uniformBlock_->GetBindingIndex();
		}
		return bindingIndex;
	}

	GLint UniformBlockCache::GetSize() const
	{
		GLint size = 0;
		if (uniformBlock_ != nullptr) {
			size = uniformBlock_->GetSize();
		}
		return size;
	}

	std::uint8_t UniformBlockCache::GetAlignAmount() const
	{
		std::uint8_t alignAmount = 0;
		if (uniformBlock_ != nullptr) {
			alignAmount = uniformBlock_->GetAlignAmount();
		}
		return alignAmount;
	}

	void UniformBlockCache::SetDataPointer(GLubyte* dataPointer)
	{
		dataPointer_ = dataPointer;

		for (UniformCache& uniformCache : uniformCaches_) {
			uniformCache.SetDataPointer(dataPointer_ + uniformCache.GetUniform()->GetOffset());
		}
	}

	void UniformBlockCache::SetUsedSize(GLint usedSize)
	{
		if (usedSize >= 0) {
			usedSize_ = usedSize;
		}
	}

	bool UniformBlockCache::CopyData(std::uint32_t destIndex, const GLubyte* src, std::uint32_t numBytes)
	{
		if (destIndex + numBytes > std::uint32_t(GetSize()) || numBytes == 0 || src == nullptr || dataPointer_ == nullptr) {
			return false;
		}
		std::memcpy(&dataPointer_[destIndex], src, numBytes);
		return true;
	}

	UniformCache* UniformBlockCache::GetUniform(StringView name)
	{
		return uniformCaches_.find(String::nullTerminatedView(name));
	}

	void UniformBlockCache::SetBlockBinding(GLuint blockBinding)
	{
		if (uniformBlock_) {
			uniformBlock_->SetBlockBinding(blockBinding);
		}
	}
}

#endif // defined(WITH_RHI_GL)
