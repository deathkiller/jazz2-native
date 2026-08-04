#pragma once

#if defined(NCINE_PROFILING) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "RenderCommand.h"

namespace nCine
{
	/**
		@brief Gathers statistics about the rendering subsystem
		
		Accumulates per-frame counters for render commands, buffers, textures, custom VBOs/IBOs,
		culled nodes, the VAO pool and the command pool. The various rendering classes are friends
		so they can feed it data; only compiled when profiling is enabled.
	*/
	class RenderStatistics
	{
		friend class ScreenViewport;
		friend class RenderQueue;
		friend class RenderBuffersManager;
		friend class Texture;
		friend class Geometry;
		friend class DrawableNode;
		friend class RenderVaoPool;
		friend class RenderCommandPool;

	public:
		/** @brief Counters for render commands of a single type or aggregated across all types */
		class Commands
		{
		public:
			std::uint32_t vertices;
			std::uint32_t commands;
			std::uint32_t transparents;
			std::uint32_t instances;
			std::uint32_t batchSize;

			Commands()
				: vertices(0), commands(0), transparents(0), instances(0), batchSize(0) {}

		private:
			void reset()
			{
				vertices = 0;
				commands = 0;
				transparents = 0;
				instances = 0;
				batchSize = 0;
			}

			friend RenderStatistics;
		};

		/** @brief Counters for the managed buffers of a single type */
		class Buffers
		{
		public:
			std::uint32_t count;
			std::uint32_t size;
			std::uint32_t usedSpace;

			Buffers()
				: count(0), size(0), usedSpace(0) {}

		private:
			void reset()
			{
				count = 0;
				size = 0;
				usedSpace = 0;
			}

			friend RenderStatistics;
		};

		/** @brief Counters for textures and their total video memory usage */
		class Textures
		{
		public:
			std::uint32_t count;
			std::uint32_t dataSize;

			Textures()
				: count(0), dataSize(0) {}

		private:
			void reset()
			{
				count = 0;
				dataSize = 0;
			}

			friend RenderStatistics;
		};

		/** @brief Counters for custom VBOs or IBOs and their total memory usage */
		class CustomBuffers
		{
		public:
			std::uint32_t count;
			std::uint32_t dataSize;

			CustomBuffers()
				: count(0), dataSize(0) {}

		private:
			void reset()
			{
				count = 0;
				dataSize = 0;
			}

			friend RenderStatistics;
		};

		/** @brief Counters for the VAO pool size, capacity, reuses and bindings */
		class VaoPool
		{
		public:
			std::uint32_t size;
			std::uint32_t capacity;
			std::uint32_t reuses;
			std::uint32_t bindings;

			VaoPool()
				: size(0), capacity(0), reuses(0), bindings(0) {}

		private:
			void reset()
			{
				size = 0;
				capacity = 0;
				reuses = 0;
				bindings = 0;
			}

			friend RenderStatistics;
		};

		/** @brief Counters for the render command pool used/free sizes and retrievals */
		class CommandPool
		{
		public:
			std::uint32_t usedSize;
			std::uint32_t freeSize;
			std::uint32_t retrievals;

			CommandPool()
				: usedSize(0), freeSize(0), retrievals(0) {}

		private:
			void reset()
			{
				usedSize = 0;
				freeSize = 0;
				retrievals = 0;
			}

			friend RenderStatistics;
		};

		/** @brief Returns the command statistics aggregated across all types */
		static inline const Commands& GetAllCommands() {
			return _allCommands;
		}
		/** @brief Returns the command statistics for the specified type */
		static inline const Commands& GetCommands(RenderCommand::Type type) {
			return _typedCommands[(std::int32_t)type];
		}

		/** @brief Returns the buffer statistics for the specified type */
		static inline const Buffers& GetBuffers(RenderBuffersManager::BufferTypes type) {
			return _typedBuffers[(std::int32_t)type];
		}

		/** @brief Returns the aggregated texture statistics */
		static inline const Textures& GetTextures() {
			return _textures;
		}

		/** @brief Returns the aggregated custom VBOs statistics */
		static inline const CustomBuffers& GetCustomVBOs() {
			return _customVbos;
		}

		/** @brief Returns the aggregated custom IBOs statistics */
		static inline const CustomBuffers& GetCustomIBOs() {
			return _customIbos;
		}

		/** @brief Returns the number of `DrawableNode` objects culled for being off-screen */
		static inline std::uint32_t GetCulled() {
			return _culledNodes[(_index + 1) % 2];
		}

		/** @brief Returns the statistics about the VAO pool */
		static inline const VaoPool& GetVaoPool() {
			return _vaoPool;
		}

		/** @brief Returns the statistics about the render command pool */
		static inline const CommandPool& GetCommandPool() {
			return _commandPool;
		}

	private:
		static Commands _allCommands;
		static Commands _typedCommands[(std::int32_t)RenderCommand::Type::Count];
		static Buffers _typedBuffers[(std::int32_t)RenderBuffersManager::BufferTypes::Count];
		static Textures _textures;
		static CustomBuffers _customVbos;
		static CustomBuffers _customIbos;
		static std::uint32_t _index;
		static std::uint32_t _culledNodes[2];
		static VaoPool _vaoPool;
		static CommandPool _commandPool;

		static void Reset();
		static void GatherStatistics(const RenderCommand& command);
		static void GatherStatistics(const RenderBuffersManager::ManagedBuffer& buffer);

		static inline void GatherVaoPoolStatistics(std::uint32_t poolSize, std::uint32_t poolCapacity)
		{
			_vaoPool.size = poolSize;
			_vaoPool.capacity = poolCapacity;
		}
		static inline void GatherCommandPoolStatistics(std::uint32_t usedSize, std::uint32_t freeSize)
		{
			_commandPool.usedSize = usedSize;
			_commandPool.freeSize = freeSize;
		}
		static inline void AddTexture(std::uint32_t datasize)
		{
			_textures.count++;
			_textures.dataSize += datasize;
		}
		static inline void RemoveTexture(std::uint32_t datasize)
		{
			_textures.count--;
			_textures.dataSize -= datasize;
		}
		static inline void AddCustomVbo(std::uint32_t datasize)
		{
			_customVbos.count++;
			_customVbos.dataSize += datasize;
		}
		static inline void RemoveCustomVbo(std::uint32_t datasize)
		{
			_customVbos.count--;
			_customVbos.dataSize -= datasize;
		}
		static inline void AddCustomIbo(std::uint32_t datasize)
		{
			_customIbos.count++;
			_customIbos.dataSize += datasize;
		}
		static inline void RemoveCustomIbo(std::uint32_t datasize)
		{
			_customIbos.count--;
			_customIbos.dataSize -= datasize;
		}
		static inline void AddCulledNode()
		{
			_culledNodes[_index]++;
		}
		static inline void AddVaoPoolReuse()
		{
			_vaoPool.reuses++;
		}
		static inline void AddVaoPoolBinding()
		{
			_vaoPool.bindings++;
		}
		static inline void AddCommandPoolRetrieval()
		{
			_commandPool.retrievals++;
		}
	};
}

#endif