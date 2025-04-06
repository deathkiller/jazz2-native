#include "GLUniformBlockCache.h"
#include "GLUniformBlock.h"
#include "../../../Main.h"

namespace nCine
{
	GLUniformBlockCache::GLUniformBlockCache()
		: uniformBlock_(nullptr), dataPointer_(nullptr), usedSize_(0)
	{
	}

	GLUniformBlockCache::GLUniformBlockCache(GLUniformBlock* uniformBlock)
		: uniformBlock_(uniformBlock), dataPointer_(nullptr), usedSize_(0)
	{
		ASSERT(uniformBlock);
		usedSize_ = uniformBlock->GetSize();

		static_assert(UniformHashSize >= GLUniformBlock::BlockUniformHashSize, "Uniform cache is smaller than the number of uniforms");

		for (const GLUniform& uniform : uniformBlock->blockUniforms_) {
			GLUniformCache uniformCache(&uniform);
			uniformCaches_[uniform.GetName()] = uniformCache;
		}
	}

	GLuint GLUniformBlockCache::GetIndex() const
	{
		GLuint index = 0;
		if (uniformBlock_ != nullptr) {
			index = uniformBlock_->GetIndex();
		}
		return index;
	}

	GLuint GLUniformBlockCache::GetBindingIndex() const
	{
		GLuint bindingIndex = 0;
		if (uniformBlock_ != nullptr) {
			bindingIndex = uniformBlock_->GetBindingIndex();
		}
		return bindingIndex;
	}

	GLint GLUniformBlockCache::GetSize() const
	{
		GLint size = 0;
		if (uniformBlock_ != nullptr) {
			size = uniformBlock_->GetSize();
		}
		return size;
	}

	std::uint8_t GLUniformBlockCache::GetAlignAmount() const
	{
		std::uint8_t alignAmount = 0;
		if (uniformBlock_ != nullptr) {
			alignAmount = uniformBlock_->GetAlignAmount();
		}
		return alignAmount;
	}

	void GLUniformBlockCache::SetDataPointer(GLubyte* dataPointer)
	{
		dataPointer_ = dataPointer;

		for (GLUniformCache& uniformCache : uniformCaches_) {
			uniformCache.SetDataPointer(dataPointer_ + uniformCache.GetUniform()->GetOffset());
		}
	}

	void GLUniformBlockCache::SetUsedSize(GLint usedSize)
	{
		if (usedSize >= 0) {
			usedSize_ = usedSize;
		}
	}

	bool GLUniformBlockCache::CopyData(std::uint32_t destIndex, const GLubyte* src, std::uint32_t numBytes)
	{
		if (destIndex + numBytes > std::uint32_t(GetSize()) || numBytes == 0 || src == nullptr || dataPointer_ == nullptr) {
			return false;
		}
		std::memcpy(&dataPointer_[destIndex], src, numBytes);
		return true;
	}

	GLUniformCache* GLUniformBlockCache::GetUniform(StringView name)
	{
		return uniformCaches_.find(String::nullTerminatedView(name));
	}

	void GLUniformBlockCache::SetBlockBinding(GLuint blockBinding)
	{
		if (uniformBlock_) {
			uniformBlock_->SetBlockBinding(blockBinding);
		}
	}
}
