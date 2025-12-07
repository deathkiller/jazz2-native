#include "GLShaderUniformBlocks.h"
#include "GLShaderProgram.h"
#include "GLShaderUniforms.h"
#include "../RenderResources.h"
#include "../Material.h"
#include "../../ServiceLocator.h"
#include "../../../Main.h"

#include <cstring> // for memcpy()

#include "GLShaderUniformBlocks.h"
#include "GLShaderProgram.h"
#include "GLShaderUniforms.h"
#include "GLUniform.h"
#include "../RenderResources.h"
#include "../Material.h"
#include "../../ServiceLocator.h"
#include "../../../Main.h"

#include <cstring> // for memcpy()

namespace nCine
{
#if defined(WITH_OPENGL2)
	void GLShaderUniformBlocks::InitializeFallbackCache()
	{
		if (gl2ShaderUniforms_ == nullptr) {
			return;
		}
		
		gl2FallbackCache_.dataPointer_ = gl2FallbackBackingStore_;
		std::memset(gl2FallbackBackingStore_, 0, sizeof(gl2FallbackBackingStore_));
		
		// Dynamically discover all uniforms from the shader and create caches for them
		// This ensures we handle all uniforms regardless of naming conventions
		std::uint32_t offset = 0;
		for (const GLUniformCache& shaderUniformCache : gl2ShaderUniforms_->GetAllUniforms()) {
			const GLUniform* uniform = shaderUniformCache.GetUniform();
			if (uniform == nullptr) {
				continue;
			}
			
			// Skip uniforms that are likely not part of instance block
			// (projection, view, model matrices, and texture samplers)
			const char* name = uniform->GetName();
			if (strstr(name, "Projection") != nullptr || 
			    strstr(name, "View") != nullptr ||
			    strstr(name, "Texture") != nullptr ||
			    strcmp(name, "uDepth") == 0) {
				continue;
			}
			
			// Calculate required size
			std::uint32_t uniformSize = uniform->GetMemorySize();
			if (offset + uniformSize > sizeof(gl2FallbackBackingStore_)) {
				LOGW("Fallback backing store too small for uniform \"%s\"", name);
				break;
			}
			
			// Create a copy that points to our backing store
			GLUniformCache ourCache(shaderUniformCache);
			ourCache.SetDataPointer(&gl2FallbackBackingStore_[offset]);
			
			// Store in the fallback cache's map
			gl2FallbackCache_.uniformCaches_[name] = Death::move(ourCache);
			
			offset += uniformSize;
		}
		
		gl2FallbackCache_.usedSize_ = offset;
	}

	void GLShaderUniformBlocks::CommitFallbackCache()
	{
		if (gl2ShaderUniforms_ == nullptr) {
			return;
		}
		
		// Copy cached uniform values from per-object backing store to shared shader uniforms
		// This is called right before drawing (from CommitUniforms)
		for (GLUniformCache& ourCache : gl2FallbackCache_.uniformCaches_) {
			const GLUniform* uniform = ourCache.GetUniform();
			if (uniform == nullptr) {
				continue;
			}
			
			// Get the shader uniform cache
			GLUniformCache* shaderCache = gl2ShaderUniforms_->GetUniform(uniform->GetName());
			if (shaderCache != nullptr) {
				// Copy data from our per-object backing store to shader uniform's backing store
				std::memcpy(const_cast<GLubyte*>(shaderCache->GetDataPointer()), ourCache.GetDataPointer(), uniform->GetMemorySize());
				// Mark shader uniform as dirty so CommitUniforms() will upload it
				shaderCache->SetDirty(true);
			}
		}
	}
#endif

	GLShaderUniformBlocks::GLShaderUniformBlocks()
		: shaderProgram_(nullptr), dataPointer_(nullptr)
#if defined(WITH_OPENGL2)
			, gl2ShaderUniforms_(nullptr)
#endif
	{
	}

	GLShaderUniformBlocks::GLShaderUniformBlocks(GLShaderProgram* shaderProgram)
		: GLShaderUniformBlocks(shaderProgram, nullptr, nullptr)
	{
	}

	GLShaderUniformBlocks::GLShaderUniformBlocks(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
		: GLShaderUniformBlocks()
	{
		SetProgram(shaderProgram, includeOnly, exclude);
	}

	void GLShaderUniformBlocks::Bind()
	{
#if !defined(WITH_OPENGL2)
		// Use uniform buffer objects on OpenGL 3.3+
#	if defined(DEATH_DEBUG)
		static const std::int32_t offsetAlignment = theServiceLocator().GetGfxCapabilities().GetValue(IGfxCapabilities::GLIntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT);
#	endif
		if (uboParams_.object) {
			uboParams_.object->Bind();

			GLintptr moreOffset = 0;
			for (GLUniformBlockCache& uniformBlockCache : uniformBlockCaches_) {
				uniformBlockCache.SetBlockBinding(uniformBlockCache.GetIndex());
				const GLintptr offset = static_cast<GLintptr>(uboParams_.offset) + moreOffset;
#	if defined(DEATH_DEBUG)
				DEATH_DEBUG_ASSERT(offset % offsetAlignment == 0);
#	endif
				uboParams_.object->BindBufferRange(uniformBlockCache.GetBindingIndex(), offset, uniformBlockCache.usedSize());
				moreOffset += uniformBlockCache.usedSize();
			}
		}
#endif
	}

	void GLShaderUniformBlocks::SetProgram(GLShaderProgram* shaderProgram, const char* includeOnly, const char* exclude)
	{
		DEATH_ASSERT(shaderProgram);

		shaderProgram_ = shaderProgram;
		shaderProgram_->ProcessDeferredQueries();
		uniformBlockCaches_.clear();

#if defined(WITH_OPENGL2)
		// Clear fallback cache when shader changes, it contains uniforms from old shader
		gl2FallbackCache_.uniformCaches_.clear();
#endif

		if (shaderProgram->GetStatus() == GLShaderProgram::Status::LinkedWithIntrospection) {
			ImportUniformBlocks(includeOnly, exclude);
		}
	}

	void GLShaderUniformBlocks::SetUniformsDataPointer(GLubyte* dataPointer)
	{
		DEATH_ASSERT(dataPointer);

		if (shaderProgram_->GetStatus() != GLShaderProgram::Status::LinkedWithIntrospection) {
			return;
		}

		dataPointer_ = dataPointer;
		std::int32_t offset = 0;
		for (GLUniformBlockCache& uniformBlockCache : uniformBlockCaches_) {
			uniformBlockCache.SetDataPointer(dataPointer + offset);
			offset += uniformBlockCache.uniformBlock()->GetSize() - uniformBlockCache.uniformBlock()->GetAlignAmount();
		}
	}

	GLUniformBlockCache* GLShaderUniformBlocks::GetUniformBlock(const char* name)
	{
		DEATH_ASSERT(name);
		GLUniformBlockCache* uniformBlockCache = nullptr;

		if (shaderProgram_ != nullptr) {
			uniformBlockCache = uniformBlockCaches_.find(String::nullTerminatedView(name));
			
#if defined(WITH_OPENGL2)
			// Return fallback cache for instance block on OpenGL 2.x
			if (uniformBlockCache == nullptr && strcmp(name, Material::InstanceBlockName) == 0) {
				if (gl2FallbackCache_.uniformCaches_.size() == 0) {
					InitializeFallbackCache();
				}
				return &gl2FallbackCache_;
			}
#endif
		} else {
			LOGE("Cannot find uniform block \"{}\", no shader program associated", name);
		}
		return uniformBlockCache;
	}

	void GLShaderUniformBlocks::CommitUniformBlocks()
	{
		if (shaderProgram_ == nullptr) {
			LOGE("No shader program associated");
			return;
		}

		if (shaderProgram_->GetStatus() == GLShaderProgram::Status::LinkedWithIntrospection) {
			// OpenGL 2.x: Fallback cache is handled in CommitUniforms(), not here

			std::int32_t totalUsedSize = 0;
			bool hasMemoryGaps = false;
			for (GLUniformBlockCache& uniformBlockCache : uniformBlockCaches_) {
				// There is a gap if at least one block cache (not in last position) uses less memory than its size
				if (uniformBlockCache.GetDataPointer() != dataPointer_ + totalUsedSize) {
					hasMemoryGaps = true;
				}
				totalUsedSize += uniformBlockCache.usedSize();
			}

			if (totalUsedSize > 0) {
				const RenderBuffersManager::BufferTypes bufferType = RenderBuffersManager::BufferTypes::Uniform;
				uboParams_ = RenderResources::GetBuffersManager().AcquireMemory(bufferType, totalUsedSize);
				if (uboParams_.mapBase != nullptr) {
					if (hasMemoryGaps) {
						std::int32_t offset = 0;
						for (GLUniformBlockCache& uniformBlockCache : uniformBlockCaches_) {
							std::memcpy(uboParams_.mapBase + uboParams_.offset + offset, uniformBlockCache.GetDataPointer(), uniformBlockCache.usedSize());
							offset += uniformBlockCache.usedSize();
						}
					} else {
						std::memcpy(uboParams_.mapBase + uboParams_.offset, dataPointer_, totalUsedSize);
					}
				}
			}
		}
	}

	void GLShaderUniformBlocks::ImportUniformBlocks(const char* includeOnly, const char* exclude)
	{
		const std::uint32_t MaxUniformBlockName = 128;

		std::uint32_t importedCount = 0;
		for (GLUniformBlock& uniformBlock : shaderProgram_->uniformBlocks_) {
			const char* uniformBlockName = uniformBlock.GetName();
			const char* currentIncludeOnly = includeOnly;
			const char* currentExclude = exclude;
			bool shouldImport = true;

			if (includeOnly != nullptr) {
				shouldImport = false;
				while (currentIncludeOnly != nullptr && currentIncludeOnly[0] != '\0') {
					if (strncmp(currentIncludeOnly, uniformBlockName, MaxUniformBlockName) == 0) {
						shouldImport = true;
						break;
					}
					currentIncludeOnly += strnlen(currentIncludeOnly, MaxUniformBlockName) + 1;
				}
			}

			if (exclude != nullptr) {
				while (currentExclude != nullptr && currentExclude[0] != '\0') {
					if (strncmp(currentExclude, uniformBlockName, MaxUniformBlockName) == 0) {
						shouldImport = false;
						break;
					}
					currentExclude += strnlen(currentExclude, MaxUniformBlockName) + 1;
				}
			}

			if (shouldImport) {
				uniformBlockCaches_.emplace(uniformBlockName, &uniformBlock);
				importedCount++;
			}
		}

		if (importedCount > UniformBlockCachesHashSize) {
			LOGW("More imported uniform blocks ({}) than hashmap buckets ({})", importedCount, UniformBlockCachesHashSize);
		}
	}
}
