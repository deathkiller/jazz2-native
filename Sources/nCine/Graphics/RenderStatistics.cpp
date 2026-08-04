#if defined(NCINE_PROFILING)

#include "RenderStatistics.h"
#include "../tracy.h"

namespace nCine
{
	RenderStatistics::Commands RenderStatistics::_allCommands;
	RenderStatistics::Commands RenderStatistics::_typedCommands[(int)RenderCommand::Type::Count];
	RenderStatistics::Buffers RenderStatistics::_typedBuffers[(int)RenderBuffersManager::BufferTypes::Count];
	RenderStatistics::Textures RenderStatistics::_textures;
	RenderStatistics::CustomBuffers RenderStatistics::_customVbos;
	RenderStatistics::CustomBuffers RenderStatistics::_customIbos;
	std::uint32_t RenderStatistics::_index = 0;
	std::uint32_t RenderStatistics::_culledNodes[2] = { 0, 0 };
	RenderStatistics::VaoPool RenderStatistics::_vaoPool;
	RenderStatistics::CommandPool RenderStatistics::_commandPool;

	void RenderStatistics::Reset()
	{
		TracyPlot("Vertices", static_cast<int64_t>(_allCommands.vertices));
		TracyPlot("Render Commands", static_cast<int64_t>(_allCommands.commands));

		for (unsigned int i = 0; i < (unsigned int)RenderCommand::Type::Count; i++) {
			_typedCommands[i].reset();
		}
		_allCommands.reset();

		for (unsigned int i = 0; i < (unsigned int)RenderBuffersManager::BufferTypes::Count; i++) {
			_typedBuffers[i].reset();
		}

		// Ping pong index for last and current frame
		_index = (_index + 1) % 2;
		_culledNodes[_index] = 0;

		_vaoPool.reset();
		_commandPool.reset();
	}

	void RenderStatistics::GatherStatistics(const RenderCommand& command)
	{
		const std::int32_t numVertices = command.GetGeometry().GetVertexCount();
		const unsigned int numIndices = command.GetGeometry().GetIndexCount();

		if (numVertices == 0 && numIndices == 0) {
			return;
		}

		unsigned int verticesToCount = 0;
		if (numIndices > 0) {
			verticesToCount = (command.GetInstanceCount() > 0) ? numIndices * command.GetInstanceCount() : numIndices;
		} else {
			verticesToCount = (command.GetInstanceCount() > 0) ? numVertices * command.GetInstanceCount() : numVertices;
		}

		const unsigned int typeIndex = (unsigned int)command.GetType();
		_typedCommands[typeIndex].vertices += verticesToCount;
		_typedCommands[typeIndex].commands++;
		_typedCommands[typeIndex].transparents += (command.GetMaterial().IsBlendingEnabled()) ? 1 : 0;
		_typedCommands[typeIndex].instances += command.GetInstanceCount();
		_typedCommands[typeIndex].batchSize += command.GetBatchSize();

		_allCommands.vertices += verticesToCount;
		_allCommands.commands++;
		_allCommands.transparents += (command.GetMaterial().IsBlendingEnabled()) ? 1 : 0;
		_allCommands.instances += command.GetInstanceCount();
		_allCommands.batchSize += command.GetBatchSize();
	}

	void RenderStatistics::GatherStatistics(const RenderBuffersManager::ManagedBuffer& buffer)
	{
		const unsigned int typeIndex = (unsigned int)buffer.type;
		_typedBuffers[typeIndex].count++;
		_typedBuffers[typeIndex].size += buffer.size;
		_typedBuffers[typeIndex].usedSpace += buffer.size - buffer.freeSpace;
	}
}

#endif