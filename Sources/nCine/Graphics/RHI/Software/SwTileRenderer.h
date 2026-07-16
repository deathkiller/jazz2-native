#pragma once

#if defined(WITH_RHI_SOFTWARE)

#include "SwRaster.h"

#include <Containers/SmallVector.h>

#if defined(WITH_THREADS)
#	include "../../../Threading/Thread.h"
#	include "../../../Threading/ThreadSync.h"
#endif

#include <cstdint>

namespace nCine::RhiSoftware
{
	/**
		@brief Tile-based deferred rasterization layer sitting in front of @ref SwRaster

		Collects the frame's draw calls instead of rasterizing them immediately, then processes the
		destination surface one small tile at a time so each tile's working set stays resident in the L1
		data cache while every command that touches it is drawn. On a build with `WITH_THREADS` the tiles
		are handed out to a pool of worker threads through an atomic counter, so the flush scales across
		cores; without threads it falls back to a sequential single-pass loop.

		The layer is transparent to the device: @ref SwRaster forwards each draw to @ref SubmitCommand(),
		which either accepts it for deferral (returning `true`) or declines it (returning `false`) so the
		caller runs it through the immediate rasterizer instead. @ref Flush() is called before the surface
		is read back (present) or a different render target is bound, and it never returns until every
		worker has finished writing, so the pixels are complete and race-free by the time it does.
	*/
	namespace SwTileRenderer
	{
		/** @brief Tile edge length in pixels (`32*32*4` = 4 KB fits comfortably in a 32 KB L1 data cache) */
		static constexpr std::int32_t TileSize = 32;
		/** @brief `log2(TileSize)`, used to turn pixel coordinates into tile coordinates with a shift */
		static constexpr std::int32_t TileSizeShift = 5;
		/** @brief Upper bound on the commands deferred in one flush window (fixed, statically allocated) */
		static constexpr std::int32_t MaxCommands = 4096;

		/** @brief Largest destination width the static tile-bin array is sized for */
		static constexpr std::int32_t MaxWidth = 1920;
		/** @brief Largest destination height the static tile-bin array is sized for */
		static constexpr std::int32_t MaxHeight = 1080;
		/** @brief Number of tile columns at @ref MaxWidth */
		static constexpr std::int32_t MaxTilesX = (MaxWidth + TileSize - 1) / TileSize;
		/** @brief Number of tile rows at @ref MaxHeight */
		static constexpr std::int32_t MaxTilesY = (MaxHeight + TileSize - 1) / TileSize;
		/** @brief Total number of tile bins the layer can address */
		static constexpr std::int32_t MaxTiles = MaxTilesX * MaxTilesY;

		/**
			@brief One deferred draw call together with its pre-computed screen-space bounds

			A snapshot of the @ref DrawContext plus the primitive range and the viewport that was active when
			the command was submitted, so the worker can reproduce the exact vertex transform later on another
			thread. The screen-space AABB (inclusive pixel bounds) drives tile binning.
		*/
		struct DeferredCommand
		{
			/** @brief Snapshot of the per-draw context (textures, fixed-function state, blend / scissor) */
			DrawContext ctx;
			/** @brief Primitive topology of the deferred range */
			PrimitiveType primType;
			/** @brief Index of the first vertex */
			std::int32_t firstVertex;
			/** @brief Number of vertices */
			std::int32_t count;

			/** @brief Viewport origin X at submit time (for the correct NDC-to-screen transform) */
			std::int32_t viewportX;
			/** @brief Viewport origin Y at submit time */
			std::int32_t viewportY;
			/** @brief Viewport width at submit time */
			std::int32_t viewportW;
			/** @brief Viewport height at submit time */
			std::int32_t viewportH;

			/** @brief Inclusive screen-space AABB minimum X used for tile binning */
			std::int32_t screenMinX;
			/** @brief Inclusive screen-space AABB minimum Y */
			std::int32_t screenMinY;
			/** @brief Inclusive screen-space AABB maximum X */
			std::int32_t screenMaxX;
			/** @brief Inclusive screen-space AABB maximum Y */
			std::int32_t screenMaxY;

			/**
			 * @brief Whether the AABB was computed from real geometry (procedural sprite quad) rather than
			 * given conservative full-frame bounds. The read-back-skip optimization may only fire when `true`.
			 */
			bool boundsAreAccurate;

			/**
			 * @brief Inline snapshot of the effect's fragment-callback parameter block
			 *
			 * The device fills @ref DrawContext::fragmentShaderUserData with a pointer into its own
			 * caller-stack storage, which dies when the draw call returns; deferral outlives that, so
			 * @ref SubmitCommand copies the block here (it is trivially copyable) and repoints
			 * @ref ctx.fragmentShaderUserData at it, making the deferred draw fully self-contained.
			 */
			alignas(16) std::uint8_t userDataStorage[MaxFragmentShaderUserDataSize];
		};

		/** @brief Spins up the worker pool and resets the queue (idempotent; called once at startup) */
		void Initialize();

		/** @brief Signals the workers to exit, joins them and releases resources */
		void Shutdown();

		/** @brief Returns `true` when the layer is initialized and accepting deferred commands */
		bool IsActive();

		/** @brief Records the current viewport, snapshotted into every command submitted afterwards */
		void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);

		/**
			@brief Points the layer at a new destination surface, flushing the previous one first

			Flushes any commands still queued for the old target when the target actually changes, then
			reconfigures the tile grid for the new one. A surface larger than @ref MaxWidth x @ref MaxHeight
			disables the layer until a valid target is set again. Passing an unchanged target is a no-op.

			@param buffer		Base of the tightly packed `width * height * 4` RGBA8 destination
			@param width		Destination width in pixels
			@param height		Destination height in pixels
			@param isFboTarget	`true` for a render-target texture (rows stored bottom-up), `false` for the screen
		*/
		void SetTargetBuffer(std::uint8_t* buffer, std::int32_t width, std::int32_t height, bool isFboTarget = false);

		/**
			@brief Submits a draw call for deferred tile-based rendering

			@returns `true` if the command was accepted (and will be drawn by @ref Flush()), `false` if the
			caller should rasterize it immediately (layer inactive, no target, or buffer full)
		*/
		bool SubmitCommand(const DrawContext& ctx, PrimitiveType type,
		                   std::int32_t firstVertex, std::int32_t count);

		/**
			@brief Renders every queued command tile by tile, then clears the queue

			Bins the commands into tiles, processes the tiles (multi-threaded when `WITH_THREADS` is set,
			sequential otherwise) and copies the results back into the destination surface. Called before the
			surface is read for presentation or a different render target is bound. Waits for all workers to
			finish before returning.
		*/
		void Flush();

		/** @brief Drops all queued commands without rendering them (e.g. after a full-surface clear) */
		void DiscardPending();

		/** @brief Returns the number of commands currently queued */
		std::int32_t GetPendingCommandCount();
	}
}

#endif
