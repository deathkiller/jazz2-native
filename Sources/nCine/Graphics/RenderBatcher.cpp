#include "RenderBatcher.h"
#include "RenderCommand.h"
#include "RenderCommandPool.h"
#include "RenderResources.h"
#include "GL/GLShaderProgram.h"
#include "../Application.h"
#include "../ServiceLocator.h"
#include "../Base/StaticHashMapIterator.h"

#include <cstring> // for memcpy()

namespace nCine
{
	std::uint32_t RenderBatcher::UboMaxSize = 0;

	RenderBatcher::RenderBatcher()
	{
		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
		UboMaxSize = std::uint32_t(gfxCaps.GetValue(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED));

		// Create the first buffer right away
		createBuffer(UboMaxSize);
	}

	void RenderBatcher::createBatches(const SmallVectorImpl<RenderCommand*>& srcQueue, SmallVectorImpl<RenderCommand*>& destQueue)
	{
		std::uint32_t minBatchSize, maxBatchSize;
		std::uint32_t fixedBatchSize = theApplication().GetAppConfiguration().fixedBatchSize;
		if (fixedBatchSize > 0) {
			minBatchSize = fixedBatchSize;
			maxBatchSize = fixedBatchSize;
		} else {
			auto& renderingSettings = theApplication().GetRenderingSettings();
			minBatchSize = renderingSettings.minBatchSize;
			maxBatchSize = renderingSettings.maxBatchSize;
		}

		ASSERT(minBatchSize > 1);
		ASSERT(maxBatchSize >= minBatchSize);

		std::uint32_t lastSplit = 0;

		for (std::uint32_t i = 1; i < srcQueue.size(); i++) {
			const RenderCommand* command = srcQueue[i];
			const GLenum primitive = command->geometry().primitiveType();

			const RenderCommand* prevCommand = srcQueue[i - 1];
			const GLenum prevPrimitive = prevCommand->geometry().primitiveType();

			// Should split if material sort key (that takes into account shader program, textures and blending) or primitive type differs
			// GL_LINE_STRIP is split always, because it cannot be batched
			const bool shouldSplit = (command->lowerMaterialSortKey() != prevCommand->lowerMaterialSortKey() || prevPrimitive != primitive || primitive == GL_LINE_STRIP);

			// Also collect the very last command if it can be batched with the previous one
			std::uint32_t endSplit = (i == srcQueue.size() - 1 && !shouldSplit ? i + 1 : i);

			// Split point if last command or split condition
			if (i == srcQueue.size() - 1 || shouldSplit) {
				const GLShaderProgram* batchedShader = RenderResources::batchedShader(prevCommand->material().shaderProgram());
				if (batchedShader && (endSplit - lastSplit) >= minBatchSize) {
					// Split point for the maximum batch size
					while (lastSplit < endSplit) {
						std::uint32_t currentMaxBatchSize = maxBatchSize;
						const std::uint32_t shaderBatchSize = batchedShader->GetBatchSize();
						if (shaderBatchSize > 0 && currentMaxBatchSize > shaderBatchSize) {
							currentMaxBatchSize = shaderBatchSize;
						}

						const std::uint32_t batchSize = endSplit - lastSplit;
						std::uint32_t nextSplit = endSplit;
						if (batchSize > currentMaxBatchSize) {
							nextSplit = lastSplit + currentMaxBatchSize;
						} else if (batchSize < minBatchSize) {
							break;
						}
						
						SmallVectorImpl<RenderCommand*>::const_iterator start = srcQueue.begin() + lastSplit;
						SmallVectorImpl<RenderCommand*>::const_iterator end = srcQueue.begin() + nextSplit;

						// Handling early splits while collecting (not enough UBO free space)
						RenderCommand* batchCommand = collectCommands(start, end, start);
						destQueue.push_back(batchCommand);
						lastSplit = std::uint32_t(start - srcQueue.begin());
					}
				}

				// Also collect the very last command
				endSplit = (i == srcQueue.size() - 1 ? i + 1 : i);

				// Passthrough for unsupported command types and for the last few commands that are less than the minimum batch size
				for (std::uint32_t j = lastSplit; j < endSplit; j++) {
					destQueue.push_back(srcQueue[j]);
				}

				lastSplit = endSplit;
			}
		}

		// If the queue has only one command the for loop didn't execute, the command has to passthrough
		if (srcQueue.size() == 1) {
			destQueue.push_back(srcQueue[0]);
		}
	}

	void RenderBatcher::reset()
	{
		// Reset managed buffers
		for (ManagedBuffer& buffer : buffers_) {
			buffer.freeSpace = buffer.size;
		}
	}

	RenderCommand* RenderBatcher::collectCommands(
		SmallVectorImpl<RenderCommand*>::const_iterator start,
		SmallVectorImpl<RenderCommand*>::const_iterator end,
		SmallVectorImpl<RenderCommand*>::const_iterator& nextStart)
	{
		ASSERT(end > start);

		const RenderCommand* refCommand = *start;
		RenderCommand* batchCommand = nullptr;
		GLUniformBlockCache* instancesBlock = nullptr;

		// Tracking the amount of memory required by uniform blocks, vertices and indices of all instances
		std::uint32_t instancesBlockSize = 0;
		std::uint32_t instancesVertexDataSize = 0;
		std::uint32_t instancesIndicesAmount = 0;

		const GLShaderProgram* refShader = refCommand->material().shaderProgram();
		GLShaderProgram* batchedShader = RenderResources::batchedShader(refShader);
		// The following check should never fail as it is already checked by the calling function
		FATAL_ASSERT_MSG(batchedShader != nullptr, "Unsupported shader for batch element");
		bool commandAdded = false;
		batchCommand = RenderResources::renderCommandPool().retrieveOrAdd(batchedShader, commandAdded);

		// Retrieving the original block instance size without the uniform buffer offset alignment
		const GLUniformBlockCache* singleInstanceBlock = (*start)->material().uniformBlock(Material::InstanceBlockName);
		const std::uint32_t singleInstanceBlockSizePacked = singleInstanceBlock->GetSize() - singleInstanceBlock->GetAlignAmount(); // remove the uniform buffer offset alignment
		const std::uint32_t singleInstanceBlockSize = singleInstanceBlockSizePacked + (16 - singleInstanceBlockSizePacked % 16) % 16; // but add the std140 vec4 layout alignment

#if defined(NCINE_PROFILING)
		batchCommand->setType(refCommand->type());
#endif
		instancesBlock = batchCommand->material().uniformBlock(Material::InstancesBlockName);
		FATAL_ASSERT_MSG(instancesBlock != nullptr, "Batched shader does not have an \"{}\" uniform block", Material::InstancesBlockName);

		const std::uint32_t nonBlockUniformsSize = batchCommand->material().shaderProgram()->GetUniformsSize();
		// Determine how much memory is needed by uniform blocks that are not for instances
		std::uint32_t nonInstancesBlocksSize = 0;
		const GLShaderUniformBlocks::UniformHashMapType allUniformBlocks = refCommand->material().allUniformBlocks();
		for (const GLUniformBlockCache& uniformBlockCache : allUniformBlocks) {
			const char* uniformBlockName = uniformBlockCache.uniformBlock()->GetName();
			if (strcmp(uniformBlockName, Material::InstanceBlockName) == 0) {
				continue;
			}

			GLUniformBlockCache* batchBlock = batchCommand->material().uniformBlock(uniformBlockName);
			ASSERT(batchBlock);
			if (batchBlock) {
				nonInstancesBlocksSize += uniformBlockCache.GetSize() - uniformBlockCache.GetAlignAmount();
			}
		}

		// Set to true if at least one command in the batch has indices or forced by a rendering settings
		bool batchingWithIndices = theApplication().GetRenderingSettings().batchingWithIndices;
		// Sum the amount of UBO memory required by the batch and determine if indices are needed
		SmallVectorImpl<RenderCommand*>::const_iterator it = start;
		while (it != end) {
			if ((*it)->geometry().numIndices() > 0) {
				batchingWithIndices = true;
			}

			// Don't request more bytes than an instances block or an UBO can hold (also protects against big `RenderingSettings::maxBatchSize` values)
			const std::uint32_t currentSize = nonBlockUniformsSize + nonInstancesBlocksSize + instancesBlockSize;
			if (instancesBlockSize + singleInstanceBlockSize > instancesBlock->GetSize() || currentSize + singleInstanceBlockSize > UboMaxSize) {
				break;
			}
			
			instancesBlockSize += singleInstanceBlockSize;
			++it;
		}
		nextStart = it;

		batchCommand->material().setUniformsDataPointer(acquireMemory(nonBlockUniformsSize + nonInstancesBlocksSize + instancesBlockSize));
		// Copying data for non-instances uniform blocks from the first command in the batch
		for (const GLUniformBlockCache& uniformBlockCache : allUniformBlocks) {
			const char* uniformBlockName = uniformBlockCache.uniformBlock()->GetName();
			if (strcmp(uniformBlockName, Material::InstanceBlockName) == 0) {
				continue;
			}

			GLUniformBlockCache* batchBlock = batchCommand->material().uniformBlock(uniformBlockName);
			const bool dataCopied = batchBlock->CopyData(uniformBlockCache.GetDataPointer());
			ASSERT(dataCopied);
			batchBlock->SetUsedSize(uniformBlockCache.usedSize());
		}

		// Setting sampler uniforms for GL_TEXTURE* units
		const GLShaderUniforms::UniformHashMapType allUniforms = refCommand->material().allUniforms();
		for (const GLUniformCache& uniformCache : allUniforms) {
			if (uniformCache.GetUniform()->GetType() == GL_SAMPLER_2D) {
				GLUniformCache* batchUniformCache = batchCommand->material().uniform(uniformCache.GetUniform()->GetName());
				const std::int32_t refValue = uniformCache.GetIntValue(0);
				const std::int32_t batchValue = batchUniformCache->GetIntValue(0);
				// Also checking if the command has just been added, as the memory at the
				// uniforms data pointer is not cleared and might contain the reference value
				if (batchValue != refValue || commandAdded) {
					batchUniformCache->SetIntValue(refValue);
				}
			}
		}

		const std::uint32_t maxVertexDataSize = RenderResources::buffersManager().specs(RenderBuffersManager::BufferTypes::Array).maxSize;
		const std::uint32_t maxIndexDataSize = RenderResources::buffersManager().specs(RenderBuffersManager::BufferTypes::ElementArray).maxSize;
		// Sum the amount of VBO and IBO memory required by the batch
		it = start;
		const bool refShaderHasAttributes = (refShader->GetAttributeCount() > 0);
		while (it != nextStart) {
			std::uint32_t vertexDataSize = 0;
			std::uint32_t numIndices = (*it)->geometry().numIndices();

			if (refShaderHasAttributes) {
				std::uint32_t numVertices = (*it)->geometry().numVertices();
				if (!batchingWithIndices) {
					numVertices += 2; // plus two degenerates if indices are not used
				}
				const std::uint32_t numElementsPerVertex = (*it)->geometry().numElementsPerVertex() + 1; // plus the mesh index
				vertexDataSize = numVertices * numElementsPerVertex * sizeof(GLfloat);

				if (batchingWithIndices) {
					numIndices = (numIndices > 0) ? numIndices + 2 : numVertices + 2;
				}
			}

			// Don't request more bytes than a common VBO or IBO can hold
			if (instancesVertexDataSize + vertexDataSize > maxVertexDataSize ||
				(instancesIndicesAmount + numIndices) * sizeof(GLushort) > maxIndexDataSize ||
				instancesIndicesAmount + numIndices > 65535) {
				break;
			}

			instancesVertexDataSize += vertexDataSize;
			instancesIndicesAmount += numIndices;
			++it;
		}
		nextStart = it;

		// Remove the two missing degenerate vertices or indices from first and last elements
		const std::uint32_t twoVerticesDataSize = 2 * (refCommand->geometry().numElementsPerVertex() + 1) * sizeof(GLfloat);
		if (instancesIndicesAmount >= 2) {
			instancesIndicesAmount -= 2;
		} else if (instancesVertexDataSize >= twoVerticesDataSize) {
			instancesVertexDataSize -= twoVerticesDataSize;
		}

		const std::uint32_t NumFloatsVertexFormat = refCommand->geometry().numElementsPerVertex();
		const std::uint32_t NumFloatsVertexFormatAndIndex = NumFloatsVertexFormat + 1; // index is an `int`, same size as a `float`
		const std::uint32_t SizeVertexFormat = NumFloatsVertexFormat * 4;
		const std::uint32_t SizeVertexFormatAndIndex = SizeVertexFormat + sizeof(std::uint32_t);

		float* destVtx = nullptr;
		GLushort* destIdx = nullptr;

		const bool batchedShaderHasAttributes = (batchedShader->GetAttributeCount() > 1);
		if (batchedShaderHasAttributes) {
			const std::uint32_t numFloats = instancesVertexDataSize / sizeof(GLfloat);
			destVtx = batchCommand->geometry().acquireVertexPointer(numFloats, NumFloatsVertexFormat + 1); // aligned to vertex format with index

			if (instancesIndicesAmount > 0) {
				destIdx = batchCommand->geometry().acquireIndexPointer(instancesIndicesAmount);
			}
		}

		it = start;
		std::uint32_t instancesBlockOffset = 0;
		std::uint16_t batchFirstVertexId = 0;
		while (it != nextStart) {
			RenderCommand* command = *it;
			command->commitNodeTransformation();

			const GLUniformBlockCache* singleInstanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
			const bool dataCopied = instancesBlock->CopyData(instancesBlockOffset, singleInstanceBlock->GetDataPointer(), singleInstanceBlockSize);
			ASSERT(dataCopied);
			instancesBlockOffset += singleInstanceBlockSize;

			if (batchedShaderHasAttributes) {
				const std::uint32_t numVertices = command->geometry().numVertices();
				const std::int32_t meshIndex = std::int32_t(it - start);
				const float* srcVtx = command->geometry().hostVertexPointer();
				FATAL_ASSERT(srcVtx != nullptr);

				// Vertex of a degenerate triangle, if not a starting element and there are more than one in the batch
				if (it != start && nextStart - start > 1 && !batchingWithIndices) {
					std::memcpy(destVtx, srcVtx, SizeVertexFormat);
					*reinterpret_cast<std::int32_t*>(static_cast<void*>(&destVtx[NumFloatsVertexFormat])) = meshIndex; // last element is the index
					destVtx += NumFloatsVertexFormatAndIndex;
				}
				for (std::uint32_t i = 0; i < numVertices; i++) {
					std::memcpy(destVtx, srcVtx, SizeVertexFormat);
					*reinterpret_cast<std::int32_t*>(static_cast<void*>(&destVtx[NumFloatsVertexFormat])) = meshIndex; // last element is the index
					destVtx += NumFloatsVertexFormatAndIndex;
					srcVtx += NumFloatsVertexFormat; // source format does not include an index
				}
				// Vertex of a degenerate triangle, if not an ending element and there are more than one in the batch
				if (it != nextStart - 1 && nextStart - start > 1 && !batchingWithIndices) {
					srcVtx -= NumFloatsVertexFormat;
					std::memcpy(destVtx, srcVtx, SizeVertexFormat);
					*reinterpret_cast<std::int32_t*>(static_cast<void*>(&destVtx[NumFloatsVertexFormat])) = meshIndex; // last element is the index
					destVtx += NumFloatsVertexFormatAndIndex;
				}

				if (instancesIndicesAmount > 0) {
					std::uint16_t vertexId = 0;
					const std::uint32_t numIndices = command->geometry().numIndices() ? command->geometry().numIndices() : numVertices;
					const GLushort* srcIdx = command->geometry().hostIndexPointer();

					// Index of a degenerate triangle, if not a starting element and there are more than one in the batch
					if (it != start && nextStart - start > 1) {
						*destIdx = batchFirstVertexId + (srcIdx ? *srcIdx : vertexId);
						destIdx++;
					}
					for (std::uint32_t i = 0; i < numIndices; i++) {
						*destIdx = batchFirstVertexId + (srcIdx ? *srcIdx : vertexId);
						destIdx++;
						vertexId++;
						if (srcIdx != nullptr) {
							srcIdx++;
						}
					}
					// Index of a degenerate triangle, if not an ending element and there are more than one in the batch
					if (it != nextStart - 1 && nextStart - start > 1) {
						if (srcIdx != nullptr) {
							srcIdx--;
						}
						*destIdx = batchFirstVertexId + (srcIdx ? *srcIdx : vertexId - 1);
						destIdx++;
					}

					batchFirstVertexId += srcIdx ? numVertices : vertexId;
				}
			}

			++it;
		}
		instancesBlock->SetUsedSize(instancesBlockOffset);

		if (batchedShaderHasAttributes) {
			batchCommand->geometry().releaseVertexPointer();
			if (destIdx) {
				batchCommand->geometry().releaseIndexPointer();
			}
		}

		for (std::uint32_t i = 0; i < GLTexture::MaxTextureUnits; i++) {
			batchCommand->material().setTexture(i, refCommand->material().texture(i));
		}
		batchCommand->material().setBlendingEnabled(refCommand->material().isBlendingEnabled());
		batchCommand->material().setBlendingFactors(refCommand->material().srcBlendingFactor(), refCommand->material().destBlendingFactor());
		batchCommand->setBatchSize(std::int32_t(nextStart - start));
		batchCommand->setLayer(refCommand->layer());
		batchCommand->setVisitOrder(refCommand->visitOrder());

		if (batchedShaderHasAttributes) {
			const std::uint32_t totalVertices = instancesVertexDataSize / SizeVertexFormatAndIndex;
			batchCommand->geometry().setDrawParameters(refCommand->geometry().primitiveType(), 0, totalVertices);
			batchCommand->geometry().setNumElementsPerVertex(NumFloatsVertexFormatAndIndex);
			batchCommand->geometry().setNumIndices(instancesIndicesAmount);
		} else {
			batchCommand->geometry().setDrawParameters(GL_TRIANGLES, 0, 6 * GLsizei(nextStart - start));
		}

		return batchCommand;
	}

	unsigned char* RenderBatcher::acquireMemory(std::uint32_t bytes)
	{
		FATAL_ASSERT(bytes <= UboMaxSize);

		std::uint8_t* ptr = nullptr;

		for (ManagedBuffer& buffer : buffers_) {
			if (buffer.freeSpace >= bytes) {
				const std::uint32_t offset = buffer.size - buffer.freeSpace;
				ptr = buffer.buffer.get() + offset;
				buffer.freeSpace -= bytes;
				break;
			}
		}

		if (ptr == nullptr) {
			createBuffer(UboMaxSize);
			ptr = buffers_.back().buffer.get();
			buffers_.back().freeSpace -= bytes;
		}

		return ptr;
	}

	void RenderBatcher::createBuffer(std::uint32_t size)
	{
		ManagedBuffer& managedBuffer = buffers_.emplace_back();
		managedBuffer.size = size;
		managedBuffer.freeSpace = size;
		managedBuffer.buffer = std::make_unique<std::uint8_t[]>(size);
	}
}
