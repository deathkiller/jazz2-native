#pragma once

#if defined(WITH_RHI_SW) //&& defined(DEATH_TARGET_VITA)

#include "RHI_SW.h"

#include <Containers/SmallVector.h>

#if defined(WITH_THREADS)
#	include "../../../Threading/Thread.h"
#	include "../../../Threading/ThreadSync.h"
#endif

#include <cstdint>

namespace nCine::RHI
{
	/// Tile-based deferred renderer optimized for PS Vita.
	/// Collects draw commands during the frame, then processes them
	/// in tile order to maximize L1 cache utilization on ARM Cortex-A9.
	namespace TileRenderer
	{
		// Tile dimensions: 32x32 = 4KB per tile (RGBA8) fits in 32KB L1D cache
		static constexpr std::int32_t TileSize = 32;
		static constexpr std::int32_t TileSizeShift = 5; // log2(32)
		static constexpr std::int32_t MaxCommands = 4096;

		// Maximum framebuffer dimensions supported
		static constexpr std::int32_t MaxWidth = 1920;
		static constexpr std::int32_t MaxHeight = 1080;
		static constexpr std::int32_t MaxTilesX = (MaxWidth + TileSize - 1) / TileSize;  // 60
		static constexpr std::int32_t MaxTilesY = (MaxHeight + TileSize - 1) / TileSize; // 34
		static constexpr std::int32_t MaxTiles = MaxTilesX * MaxTilesY;                  // 2040

		/// Deferred draw command with pre-computed screen-space bounds
		struct DeferredCommand
		{
			DrawContext ctx;
			PrimitiveType primType;
			std::int32_t firstVertex;
			std::int32_t count;

			// Viewport state at submit time (for correct FetchVertex transform)
			std::int32_t viewportW;
			std::int32_t viewportH;

			// Screen-space AABB for tile binning (inclusive pixel bounds)
			std::int32_t screenMinX;
			std::int32_t screenMinY;
			std::int32_t screenMaxX;
			std::int32_t screenMaxY;
		};

		/// Initialize the tile renderer (called once at startup)
		void Initialize();

		/// Shutdown and free resources
		void Shutdown();

		/// Returns true if the tile renderer is active and accepting deferred commands
		bool IsActive();

		/// Reconfigure for a new render target. Flushes pending commands first.
		/// Pass nullptr to reset to the main color buffer.
		void SetTargetBuffer(std::uint8_t* buffer, std::int32_t width, std::int32_t height, bool isFboTarget = false);

		/// Submit a draw command for deferred tile-based rendering.
		/// Returns true if the command was accepted, false if it should be rendered immediately.
		bool SubmitCommand(const DrawContext& ctx, PrimitiveType type,
		                   std::int32_t firstVertex, std::int32_t count);

		/// Flush all deferred commands: bin into tiles, render tiles (multi-threaded), copy back.
		/// Called before framebuffer read-back or FBO bind.
		void Flush();

		/// Discard all pending commands (e.g., after a clear that covers the whole screen)
		void DiscardPending();

		/// Returns the number of pending commands
		std::int32_t GetPendingCommandCount();
	}
}

#endif