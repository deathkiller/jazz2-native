#include "GLUniformBlockCache.h"
#include "GLUniformBlock.h"
#include "../../../Common.h"

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
		usedSize_ = uniformBlock->size();

		static_assert(UniformHashSize >= GLUniformBlock::BlockUniformHashSize, "Uniform cache is smaller than the number of uniforms");

		for (const GLUniform& uniform : uniformBlock->blockUniforms_) {
			GLUniformCache uniformCache(&uniform);
			uniformCaches_[uniform.name()] = uniformCache;
		}
	}

	GLuint GLUniformBlockCache::index() const
	{
		GLuint index = 0;
		if (uniformBlock_ != nullptr) {
			index = uniformBlock_->index();
		}
		return index;
	}

	GLuint GLUniformBlockCache::bindingIndex() const
	{
		GLuint bindingIndex = 0;
		if (uniformBlock_ != nullptr) {
			bindingIndex = uniformBlock_->bindingIndex();
		}
		return bindingIndex;
	}

	GLint GLUniformBlockCache::size() const
	{
		GLint size = 0;
		if (uniformBlock_ != nullptr) {
			size = uniformBlock_->size();
		}
		return size;
	}

	unsigned char GLUniformBlockCache::alignAmount() const
	{
		unsigned char alignAmount = 0;
		if (uniformBlock_ != nullptr) {
			alignAmount = uniformBlock_->alignAmount();
		}
		return alignAmount;
	}

	void GLUniformBlockCache::setDataPointer(GLubyte* dataPointer)
	{
		dataPointer_ = dataPointer;

		for (GLUniformCache& uniformCache : uniformCaches_) {
			uniformCache.setDataPointer(dataPointer_ + uniformCache.uniform()->offset());
		}
	}

	void GLUniformBlockCache::setUsedSize(GLint usedSize)
	{
		if (usedSize >= 0) {
			usedSize_ = usedSize;
		}
	}

	bool GLUniformBlockCache::copyData(unsigned int destIndex, const GLubyte* src, unsigned int numBytes)
	{
		if (destIndex + numBytes > (unsigned int)size() || numBytes == 0 || src == nullptr || dataPointer_ == nullptr) {
			return false;
		}
		memcpy(&dataPointer_[destIndex], src, numBytes);
		return true;
	}

	GLUniformCache* GLUniformBlockCache::uniform(const StringView& name)
	{
		return uniformCaches_.find(String::nullTerminatedView(name));
	}

	void GLUniformBlockCache::setBlockBinding(GLuint blockBinding)
	{
		if (uniformBlock_) {
			uniformBlock_->setBlockBinding(blockBinding);
		}
	}
}
