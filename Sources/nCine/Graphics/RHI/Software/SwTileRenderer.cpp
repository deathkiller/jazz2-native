#if defined(WITH_RHI_SOFTWARE)

#include "SwTileRenderer.h"

#include <Containers/SmallVector.h>

#if defined(DEATH_ENABLE_NEON)
#	include <arm_neon.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstring>

using namespace Death::Containers;

namespace nCine::RhiSoftware
{
	// Per-tile rasterization entry point, defined in SwTileRasterizer.cpp. Kept in this internal namespace
	// so it has external linkage across the two translation units without leaking into the public API.
	namespace TileInternal
	{
		void RenderCommandToTile(const DrawContext& ctx, PrimitiveType type,
		                         std::int32_t firstVertex, std::int32_t count,
		                         std::uint8_t* tileBuffer, std::int32_t tileX, std::int32_t tileY,
		                         std::int32_t tileW, std::int32_t tileH,
		                         std::int32_t viewportX, std::int32_t viewportY,
		                         std::int32_t viewportW, std::int32_t viewportH);
	}

	namespace SwTileRenderer
	{
		// =====================================================================
		// Internal state
		// =====================================================================
		namespace
		{
			struct TileState
			{
				bool initialized = false;
				std::int32_t fbWidth = 0;
				std::int32_t fbHeight = 0;
				std::int32_t tilesX = 0;
				std::int32_t tilesY = 0;
				std::int32_t totalTiles = 0;

				// Viewport snapshotted into each submitted command (mirrors SwRaster's viewport so the
				// deferred vertex transform is identical to the immediate one)
				std::int32_t viewportX = 0;
				std::int32_t viewportY = 0;
				std::int32_t viewportW = 0;
				std::int32_t viewportH = 0;

				// Command buffer (flat array, reused each frame)
				DeferredCommand commands[MaxCommands];
				std::int32_t commandCount = 0;

				// Per-tile command index lists for binning (uint16_t indices into the commands array)
				SmallVector<std::uint16_t, 64> tileBins[MaxTiles];

				// Current render target buffer
				std::uint8_t* targetBuffer = nullptr;
				bool isFboTarget = false;

				// Worker threads for parallel tile processing
#if defined(WITH_THREADS)
				static constexpr std::int32_t MaxWorkers = 3; // 3 workers + main thread = 4 cores
				Thread workers[MaxWorkers];
				Mutex mutex;
				CondVariable workReady;
				CondVariable workDone;
				std::atomic<std::int32_t> workersActive{0};
				std::int32_t numSpawnedWorkers = 0; // Actual threads created — may be < MaxWorkers if spawn fails
				bool shutdownRequested = false;

				// Work distribution
				std::atomic<std::int32_t> nextTileIndex{0};
				std::int32_t flushGeneration = 0; // Incremented each Flush to prevent worker re-entry
				std::int32_t workerGeneration[MaxWorkers] = {}; // Last generation each worker processed

				// Signal and join the workers before this object's mutex / condition variables are destroyed
				// at static teardown, so they are never left blocked on a destroyed primitive (the renderer
				// keeps the pool alive for the whole process and has no explicit Shutdown() call site).
				~TileState()
				{
					if (!initialized) {
						return;
					}
					mutex.Lock();
					shutdownRequested = true;
					workReady.Broadcast();
					mutex.Unlock();
					for (std::int32_t i = 0; i < MaxWorkers; i++) {
						if (static_cast<bool>(workers[i])) {
							workers[i].Join();
						}
					}
				}
#endif
			};

			TileState g_tile;

			// Per-tile scratch buffer (each worker uses its own slice): 32x32x4 = 4096 bytes per tile
			alignas(64) std::uint8_t g_tileScratch[4][TileSize * TileSize * 4];

			// =====================================================================
			// Tile clear
			// =====================================================================
			inline void ClearTileBuffer(std::uint8_t* tile, std::int32_t pixelCount, std::uint32_t clearColor)
			{
#if defined(DEATH_ENABLE_NEON)
				uint32x4_t pattern = vdupq_n_u32(clearColor);
				std::int32_t i = 0;
				std::uint32_t* dst32 = reinterpret_cast<std::uint32_t*>(tile);
				for (; i + 16 <= pixelCount; i += 16) {
					vst1q_u32(dst32 + i, pattern);
					vst1q_u32(dst32 + i + 4, pattern);
					vst1q_u32(dst32 + i + 8, pattern);
					vst1q_u32(dst32 + i + 12, pattern);
				}
				for (; i + 4 <= pixelCount; i += 4) {
					vst1q_u32(dst32 + i, pattern);
				}
				for (; i < pixelCount; i++) {
					dst32[i] = clearColor;
				}
#else
				std::uint32_t* dst32 = reinterpret_cast<std::uint32_t*>(tile);
				for (std::int32_t i = 0; i < pixelCount; i++) {
					dst32[i] = clearColor;
				}
#endif
			}

			// =====================================================================
			// Copy tile buffer back to framebuffer
			// =====================================================================
			inline void CopyTileToFramebuffer(const std::uint8_t* tile, std::uint8_t* fb,
			                                  std::int32_t tileX, std::int32_t tileY,
			                                  std::int32_t tileW, std::int32_t tileH,
			                                  std::int32_t fbWidth, std::int32_t fbHeight,
			                                  bool flipY)
			{
				const std::int32_t rowBytes = tileW * 4;
				for (std::int32_t row = 0; row < tileH; row++) {
					const std::uint8_t* src = tile + row * TileSize * 4;
					const std::int32_t dstY = flipY ? (fbHeight - 1 - (tileY + row)) : (tileY + row);
					if DEATH_UNLIKELY(dstY < 0 || dstY >= fbHeight) {
						continue;
					}
					std::uint8_t* dst = fb + (dstY * fbWidth + tileX) * 4;
					std::memcpy(dst, src, rowBytes);
				}
			}

			// =====================================================================
			// Copy framebuffer region into tile buffer (for read-modify-write blending)
			// =====================================================================
			inline void CopyFramebufferToTile(std::uint8_t* tile, const std::uint8_t* fb,
			                                  std::int32_t tileX, std::int32_t tileY,
			                                  std::int32_t tileW, std::int32_t tileH,
			                                  std::int32_t fbWidth, std::int32_t fbHeight,
			                                  bool flipY)
			{
				const std::int32_t rowBytes = tileW * 4;
				for (std::int32_t row = 0; row < tileH; row++) {
					std::uint8_t* dst = tile + row * TileSize * 4;
					const std::int32_t srcY = flipY ? (fbHeight - 1 - (tileY + row)) : (tileY + row);
					if DEATH_UNLIKELY(srcY < 0 || srcY >= fbHeight) {
						std::memset(dst, 0, rowBytes);
						continue;
					}
					const std::uint8_t* src = fb + (srcY * fbWidth + tileX) * 4;
					std::memcpy(dst, src, rowBytes);
				}
			}

			// =====================================================================
			// Process a single tile: read back if needed, render all binned commands, copy back
			// =====================================================================
			void ProcessTile(std::int32_t tileIndex, std::int32_t workerIndex)
			{
				const std::int32_t tileCol = tileIndex % g_tile.tilesX;
				const std::int32_t tileRow = tileIndex / g_tile.tilesX;
				const std::int32_t tileX = tileCol * TileSize;
				const std::int32_t tileY = tileRow * TileSize;
				const std::int32_t tileW = std::min(TileSize, g_tile.fbWidth - tileX);
				const std::int32_t tileH = std::min(TileSize, g_tile.fbHeight - tileY);

				if DEATH_UNLIKELY(tileW <= 0 || tileH <= 0) {
					return;
				}

				const auto& bin = g_tile.tileBins[tileIndex];
				if (bin.empty()) {
					return; // No commands touch this tile - nothing to do
				}

				// Thread-local scratch buffer
				std::uint8_t* tileBuf = g_tileScratch[workerIndex];

				// Optimization: skip reading the framebuffer if the first command fully covers this tile with an
				// opaque draw (no blending needed for a background). Only safe when boundsAreAccurate —
				// conservative full-frame bounds would match any tile even if the geometry doesn't cover it.
				const DeferredCommand& firstCmd = g_tile.commands[bin[0]];
				const bool needsReadBack = (firstCmd.ctx.blendingEnabled || !firstCmd.boundsAreAccurate ||
				    firstCmd.screenMinX > tileX || firstCmd.screenMinY > tileY ||
				    firstCmd.screenMaxX < tileX + tileW - 1 || firstCmd.screenMaxY < tileY + tileH - 1);

				if (needsReadBack) {
					// Initialize the tile with current framebuffer contents (needed for correct blending)
					CopyFramebufferToTile(tileBuf, g_tile.targetBuffer,
					                      tileX, tileY, tileW, tileH,
					                      g_tile.fbWidth, g_tile.fbHeight, g_tile.isFboTarget);
				}

				// Render all commands binned to this tile
				for (std::uint16_t cmdIdx : bin) {
					const DeferredCommand& cmd = g_tile.commands[cmdIdx];
					TileInternal::RenderCommandToTile(
						cmd.ctx, cmd.primType, cmd.firstVertex, cmd.count,
						tileBuf, tileX, tileY, tileW, tileH,
						cmd.viewportX, cmd.viewportY, cmd.viewportW, cmd.viewportH);
				}

				// Copy the tile back to the framebuffer
				CopyTileToFramebuffer(tileBuf, g_tile.targetBuffer,
				                      tileX, tileY, tileW, tileH,
				                      g_tile.fbWidth, g_tile.fbHeight, g_tile.isFboTarget);
			}

#if defined(WITH_THREADS)
			// =====================================================================
			// Worker thread function: process tiles from a shared atomic counter
			// =====================================================================
			void WorkerThreadFunc(void* arg)
			{
				const std::int32_t workerIndex = static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(arg));

				while (true) {
					// Wait for new work (generation must advance)
					g_tile.mutex.Lock();
					while (g_tile.workerGeneration[workerIndex] == g_tile.flushGeneration && !g_tile.shutdownRequested) {
						g_tile.workReady.Wait(g_tile.mutex);
					}
					if DEATH_UNLIKELY(g_tile.shutdownRequested) {
						g_tile.mutex.Unlock();
						return;
					}
					g_tile.workerGeneration[workerIndex] = g_tile.flushGeneration;
					g_tile.mutex.Unlock();

					// Process tiles using an atomic counter (work-stealing pattern)
					while (true) {
						std::int32_t idx = g_tile.nextTileIndex.fetch_add(1, std::memory_order_relaxed);
						if (idx >= g_tile.totalTiles) {
							break;
						}
						ProcessTile(idx, workerIndex + 1); // +1 because the main thread uses slot 0
					}

					// Signal completion
					std::atomic_thread_fence(std::memory_order_release); // Ensure all tile writes are pushed out
					g_tile.mutex.Lock();
					g_tile.workersActive--;
					if (g_tile.workersActive == 0) {
						g_tile.workDone.Signal();
					}
					g_tile.mutex.Unlock();
				}
			}
#endif
		}

		// =====================================================================
		// Public API
		// =====================================================================

		void Initialize()
		{
			// Idempotent: the device calls this before every draw, so once the pool is up and the state is
			// reset, subsequent calls must be true no-ops (resetting commandCount/targetBuffer here would
			// discard the frame's batched draws and break batching).
			if (g_tile.initialized) {
				return;
			}

#if defined(WITH_THREADS)
			g_tile.shutdownRequested = false;
			g_tile.workersActive = 0;
			g_tile.numSpawnedWorkers = 0;
			g_tile.flushGeneration = 0;
			std::memset(g_tile.workerGeneration, 0, sizeof(g_tile.workerGeneration));

			// Spawn worker threads
#if defined(DEATH_TARGET_VITA)
			// Vita has 4 cores: core 0 (OS) + cores 1-3 (app). Force 3 workers.
			const std::int32_t numWorkers = 3;
#else
			const std::int32_t numWorkers = std::min(
				static_cast<std::int32_t>(Thread::GetProcessorCount()) - 1,
				static_cast<std::int32_t>(TileState::MaxWorkers));
#endif

			for (std::int32_t i = 0; i < numWorkers; i++) {
				g_tile.workers[i] = Thread(WorkerThreadFunc,
					reinterpret_cast<void*>(static_cast<std::intptr_t>(i)));
				// Only count workers that actually started
				if (static_cast<bool>(g_tile.workers[i])) {
					g_tile.numSpawnedWorkers++;
				}
			}
#endif
			g_tile.initialized = true;
			g_tile.fbWidth = 0;
			g_tile.fbHeight = 0;
			g_tile.totalTiles = 0;
			g_tile.commandCount = 0;
			g_tile.targetBuffer = nullptr;
		}

		void Shutdown()
		{
			if DEATH_UNLIKELY(!g_tile.initialized) {
				return;
			}

#if defined(WITH_THREADS)
			// Signal workers to exit
			g_tile.mutex.Lock();
			g_tile.shutdownRequested = true;
			g_tile.workReady.Broadcast();
			g_tile.mutex.Unlock();

			// Join all workers
			for (std::int32_t i = 0; i < TileState::MaxWorkers; i++) {
				if (static_cast<bool>(g_tile.workers[i])) {
					g_tile.workers[i].Join();
				}
			}
			// mutex/workReady/workDone are destroyed automatically by their destructors
#endif
			g_tile.initialized = false;
		}

		bool IsActive()
		{
			return g_tile.initialized;
		}

		void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
		{
			g_tile.viewportX = x;
			g_tile.viewportY = y;
			g_tile.viewportW = width;
			g_tile.viewportH = height;
		}

		void SetTargetBuffer(std::uint8_t* buffer, std::int32_t width, std::int32_t height, bool isFboTarget)
		{
			if DEATH_UNLIKELY(!g_tile.initialized) {
				return;
			}

			// The device sets the same target before every draw; do nothing (and never flush) when nothing
			// changed so consecutive draws to the same surface keep batching into one flush.
			if (buffer == g_tile.targetBuffer && width == g_tile.fbWidth &&
			    height == g_tile.fbHeight && isFboTarget == g_tile.isFboTarget) {
				return;
			}

			// The target is actually changing: flush whatever is still queued for the old one first
			if (g_tile.commandCount > 0) {
				Flush();
			}

			// Validate that the dimensions fit within our static tile-bin array
			if DEATH_UNLIKELY(width > MaxWidth || height > MaxHeight || width <= 0 || height <= 0) {
				// Too large for the tile renderer - disable until the next valid target
				g_tile.targetBuffer = nullptr;
				g_tile.fbWidth = 0;
				g_tile.fbHeight = 0;
				g_tile.totalTiles = 0;
				g_tile.isFboTarget = false;
				return;
			}

			g_tile.targetBuffer = buffer;
			g_tile.isFboTarget = isFboTarget;
			g_tile.fbWidth = width;
			g_tile.fbHeight = height;
			g_tile.tilesX = (width + TileSize - 1) >> TileSizeShift;
			g_tile.tilesY = (height + TileSize - 1) >> TileSizeShift;
			g_tile.totalTiles = g_tile.tilesX * g_tile.tilesY;
		}

		bool SubmitCommand(const DrawContext& ctx, PrimitiveType type,
		                   std::int32_t firstVertex, std::int32_t count)
		{
			if DEATH_UNLIKELY(!g_tile.initialized || g_tile.targetBuffer == nullptr) {
				return false;
			}

			// An effect's fragment-callback parameter block (fragmentShaderUserData) lives on the caller's
			// stack in SwDevice::Dispatch and dies when the draw returns, whereas a deferred draw outlives it.
			// We snapshot the block into the command below (it is trivially copyable) to make the deferred
			// draw self-contained; decline the draw — SwRaster then rasterizes it immediately — when the block
			// can't be snapshotted (no declared size, or larger than the per-command storage).
			if (ctx.fragmentShader != nullptr && ctx.fragmentShaderUserData != nullptr) {
				if (ctx.fragmentShaderUserDataSize == 0 ||
				    ctx.fragmentShaderUserDataSize > MaxFragmentShaderUserDataSize) {
					return false;
				}
			}

			if DEATH_UNLIKELY(g_tile.commandCount >= MaxCommands) {
				// Buffer full - flush and retry, or fall back to immediate
				Flush();
				if (g_tile.commandCount >= MaxCommands) return false;
			}

			// Use the viewport snapshot for the NDC→screen transform (mirrors SwRaster::SetViewport). Fall
			// back to the full buffer when no explicit viewport was set (matches SwDevice::Dispatch).
			std::int32_t vpX = g_tile.viewportX;
			std::int32_t vpY = g_tile.viewportY;
			std::int32_t vpW = g_tile.viewportW;
			std::int32_t vpH = g_tile.viewportH;
			if (vpW <= 0 || vpH <= 0) {
				vpX = 0;
				vpY = 0;
				vpW = g_tile.fbWidth;
				vpH = g_tile.fbHeight;
			}

			// Compute the screen-space AABB from the draw command
			std::int32_t screenMinX, screenMinY, screenMaxX, screenMaxY;
			bool accurateBounds;

			if DEATH_LIKELY(type == PrimitiveType::TriangleStrip && count == 4 && firstVertex == 0 && ctx.vertexData == nullptr) {
				// Procedural sprite quad - compute bounds from MVP + sprite size
				const float* m = ctx.ff.mvpMatrix;
				const float sw = ctx.ff.spriteSize[0];
				const float sh = ctx.ff.spriteSize[1];

				// The 4 corners in clip space (triangle strip order: TL, BL, TR, BR)
				// v0: ax=1,ay=0 → wx=sw, wy=0
				// v1: ax=1,ay=1 → wx=sw, wy=sh
				// v2: ax=0,ay=0 → wx=0,  wy=0
				// v3: ax=0,ay=1 → wx=0,  wy=sh
				float x0 = m[0] * sw + m[12];
				float y0 = m[1] * sw + m[13];
				float x1 = m[0] * sw + m[4] * sh + m[12];
				float y1 = m[1] * sw + m[5] * sh + m[13];
				float x2 = m[12];
				float y2 = m[13];
				float x3 = m[4] * sh + m[12];
				float y3 = m[5] * sh + m[13];

				// NDC to screen (same transform as FetchVertex, including the viewport offset)
				const float halfW = vpW * 0.5f;
				const float halfH = vpH * 0.5f;
				const float ox = static_cast<float>(vpX);
				const float oy = static_cast<float>(vpY);

				float sx0 = (x0 + 1.0f) * halfW + ox;
				float sy0 = (1.0f - y0) * halfH + oy;
				float sx1 = (x1 + 1.0f) * halfW + ox;
				float sy1 = (1.0f - y1) * halfH + oy;
				float sx2 = (x2 + 1.0f) * halfW + ox;
				float sy2 = (1.0f - y2) * halfH + oy;
				float sx3 = (x3 + 1.0f) * halfW + ox;
				float sy3 = (1.0f - y3) * halfH + oy;

				float fMinX = std::min({sx0, sx1, sx2, sx3});
				float fMinY = std::min({sy0, sy1, sy2, sy3});
				float fMaxX = std::max({sx0, sx1, sx2, sx3});
				float fMaxY = std::max({sy0, sy1, sy2, sy3});

				screenMinX = std::max(0, static_cast<std::int32_t>(fMinX));
				screenMinY = std::max(0, static_cast<std::int32_t>(fMinY));
				screenMaxX = std::min(g_tile.fbWidth - 1, static_cast<std::int32_t>(fMaxX));
				screenMaxY = std::min(g_tile.fbHeight - 1, static_cast<std::int32_t>(fMaxY));
				accurateBounds = true;
			} else {
				// For non-procedural quads, use full framebuffer bounds (conservative)
				screenMinX = 0;
				screenMinY = 0;
				screenMaxX = g_tile.fbWidth - 1;
				screenMaxY = g_tile.fbHeight - 1;
				accurateBounds = false;
			}

			// Scissor clip — Y always flipped for tile culling because tile rows are indexed top-down in
			// screen space but the framebuffer stores rows bottom-up.
			if DEATH_UNLIKELY(ctx.scissorEnabled) {
				std::int32_t scY0 = g_tile.fbHeight - ctx.scissorRect.Y - ctx.scissorRect.H;
				std::int32_t scY1 = g_tile.fbHeight - 1 - ctx.scissorRect.Y;
				screenMinX = std::max(screenMinX, ctx.scissorRect.X);
				screenMinY = std::max(screenMinY, scY0);
				screenMaxX = std::min(screenMaxX, ctx.scissorRect.X + ctx.scissorRect.W - 1);
				screenMaxY = std::min(screenMaxY, scY1);
			}

			if DEATH_UNLIKELY(screenMinX > screenMaxX || screenMinY > screenMaxY) {
				return true; // Command is fully clipped - accepted but discarded
			}

			// Store the command
			const std::int32_t cmdIdx = g_tile.commandCount;
			DeferredCommand& cmd = g_tile.commands[cmdIdx];
			cmd.ctx = ctx;
			// Snapshot the fragment-callback parameter block into the command's own storage (ctx points at
			// caller-stack memory) and repoint, so the deferred draw carries its own copy.
			if (ctx.fragmentShader != nullptr && ctx.fragmentShaderUserData != nullptr) {
				std::memcpy(cmd.userDataStorage, ctx.fragmentShaderUserData, ctx.fragmentShaderUserDataSize);
				cmd.ctx.fragmentShaderUserData = cmd.userDataStorage;
			}
			// scissorRect.Y is stored in top-down screen space so the tile rasterizer can use it directly as a
			// pixel-row clip. ctx.scissorRect.Y is bottom-up (the RHI scissor convention), so flip it here.
			if DEATH_UNLIKELY(ctx.scissorEnabled) {
				cmd.ctx.scissorRect.Y = g_tile.fbHeight - ctx.scissorRect.Y - ctx.scissorRect.H;
			}
			cmd.primType = type;
			cmd.firstVertex = firstVertex;
			cmd.count = count;
			cmd.viewportX = vpX;
			cmd.viewportY = vpY;
			cmd.viewportW = vpW;
			cmd.viewportH = vpH;
			cmd.screenMinX = screenMinX;
			cmd.screenMinY = screenMinY;
			cmd.screenMaxX = screenMaxX;
			cmd.screenMaxY = screenMaxY;
			cmd.boundsAreAccurate = accurateBounds;
			g_tile.commandCount++;

			// Bin into overlapping tiles (clamp to the valid tile range)
			const std::int32_t tileMinCol = std::max(0, screenMinX >> TileSizeShift);
			const std::int32_t tileMaxCol = std::min(g_tile.tilesX - 1, screenMaxX >> TileSizeShift);
			const std::int32_t tileMinRow = std::max(0, screenMinY >> TileSizeShift);
			const std::int32_t tileMaxRow = std::min(g_tile.tilesY - 1, screenMaxY >> TileSizeShift);

			for (std::int32_t row = tileMinRow; row <= tileMaxRow; row++) {
				for (std::int32_t col = tileMinCol; col <= tileMaxCol; col++) {
					const std::int32_t tileIdx = row * g_tile.tilesX + col;
					g_tile.tileBins[tileIdx].push_back(static_cast<std::uint16_t>(cmdIdx));
				}
			}

			return true;
		}

		void Flush()
		{
			if DEATH_UNLIKELY(!g_tile.initialized || g_tile.commandCount == 0) {
				return;
			}

			// The target buffer must have been set via SetTargetBuffer()
			if (g_tile.targetBuffer == nullptr || g_tile.totalTiles == 0) {
				DiscardPending();
				return;
			}

#if defined(WITH_THREADS)
			// Multi-threaded tile processing using an atomic work counter
			g_tile.nextTileIndex.store(0, std::memory_order_relaxed);

			g_tile.mutex.Lock();
			g_tile.flushGeneration++;
			// Set the active count based on the successfully spawned threads
			g_tile.workersActive.store(g_tile.numSpawnedWorkers, std::memory_order_release);
			g_tile.workReady.Broadcast();
			g_tile.mutex.Unlock();

			// The main thread also processes tiles (worker slot 0)
			while (true) {
				std::int32_t idx = g_tile.nextTileIndex.fetch_add(1, std::memory_order_relaxed);
				if (idx >= g_tile.totalTiles) break;
				ProcessTile(idx, 0);
			}

			// Wait for all workers to finish
			g_tile.mutex.Lock();
			while (g_tile.workersActive.load(std::memory_order_acquire) > 0) {
				g_tile.workDone.Wait(g_tile.mutex);
			}
			g_tile.mutex.Unlock();

			// Ensure all worker pixel writes are globally visible before the engine moves on to
			// DiscardPending or flipping buffers.
			std::atomic_thread_fence(std::memory_order_acquire);
#else
			// Single-threaded fallback: process tiles sequentially
			for (std::int32_t i = 0; i < g_tile.totalTiles; i++) {
				ProcessTile(i, 0);
			}
#endif

			// Reset for the next frame
			DiscardPending();
		}

		void DiscardPending()
		{
			g_tile.commandCount = 0;
			for (std::int32_t i = 0; i < g_tile.totalTiles; i++) {
				g_tile.tileBins[i].clear();
			}
		}

		std::int32_t GetPendingCommandCount()
		{
			return g_tile.commandCount;
		}
	}
}

#endif
