#pragma once

#if defined(NCINE_PROFILING) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "RenderCommand.h"

namespace nCine
{
	/// Gathers statistics about the rendering subsystem
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

		/// Returns the aggregated command statistics for all types
		static inline const Commands& AllCommands() {
			return allCommands_;
		}
		/// Returns the commnad statistics for the specified type
		static inline const Commands& Commands(RenderCommand::Type type) {
			return typedCommands_[(std::int32_t)type];
		}

		/// Returns the buffer statistics for the specified type
		static inline const Buffers& Buffers(RenderBuffersManager::BufferTypes type) {
			return typedBuffers_[(std::int32_t)type];
		}

		/// Returns aggregated texture statistics
		static inline const Textures& Textures() {
			return textures_;
		}

		/// Returns aggregated custom VBOs statistics
		static inline const CustomBuffers& CustomVBOs() {
			return customVbos_;
		}

		/// Returns aggregated custom IBOs statistics
		static inline const CustomBuffers& CustomIBOs() {
			return customIbos_;
		}

		/// Returns the number of `DrawableNodes` culled because outside of the screen
		static inline std::uint32_t Culled() {
			return culledNodes_[(index_ + 1) % 2];
		}

		/// Returns statistics about the VAO pool
		static inline const VaoPool& VaoPool() {
			return vaoPool_;
		}

		/// Returns statistics about the render command pools
		static inline const CommandPool& CommandPool() {
			return commandPool_;
		}

	private:
		static Commands allCommands_;
		static Commands typedCommands_[(std::int32_t)RenderCommand::Type::Count];
		static Buffers typedBuffers_[(std::int32_t)RenderBuffersManager::BufferTypes::Count];
		static Textures textures_;
		static CustomBuffers customVbos_;
		static CustomBuffers customIbos_;
		static std::uint32_t index_;
		static std::uint32_t culledNodes_[2];
		static VaoPool vaoPool_;
		static CommandPool commandPool_;

		static void Reset();
		static void GatherStatistics(const RenderCommand& command);
		static void GatherStatistics(const RenderBuffersManager::ManagedBuffer& buffer);
		static inline void GatherVaoPoolStatistics(std::uint32_t poolSize, std::uint32_t poolCapacity)
		{
			vaoPool_.size = poolSize;
			vaoPool_.capacity = poolCapacity;
		}
		static inline void GatherCommandPoolStatistics(std::uint32_t usedSize, std::uint32_t freeSize)
		{
			commandPool_.usedSize = usedSize;
			commandPool_.freeSize = freeSize;
		}
		static inline void AddTexture(std::uint32_t datasize)
		{
			textures_.count++;
			textures_.dataSize += datasize;
		}
		static inline void RemoveTexture(std::uint32_t datasize)
		{
			textures_.count--;
			textures_.dataSize -= datasize;
		}
		static inline void AddCustomVbo(std::uint32_t datasize)
		{
			customVbos_.count++;
			customVbos_.dataSize += datasize;
		}
		static inline void RemoveCustomVbo(std::uint32_t datasize)
		{
			customVbos_.count--;
			customVbos_.dataSize -= datasize;
		}
		static inline void AddCustomIbo(std::uint32_t datasize)
		{
			customIbos_.count++;
			customIbos_.dataSize += datasize;
		}
		static inline void RemoveCustomIbo(std::uint32_t datasize)
		{
			customIbos_.count--;
			customIbos_.dataSize -= datasize;
		}
		static inline void AddCulledNode() {
			culledNodes_[index_]++;
		}
		static inline void AddVaoPoolReuse() {
			vaoPool_.reuses++;
		}
		static inline void AddVaoPoolBinding() {
			vaoPool_.bindings++;
		}
		static inline void AddCommandPoolRetrieval() {
			commandPool_.retrievals++;
		}
	};
}

#endif