#if defined(WITH_RHI_SW) //&& defined(DEATH_TARGET_VITA)

#include "TileRenderer.h"

#include <Containers/SmallVector.h>

#if defined(DEATH_ENABLE_NEON)
#	include <arm_neon.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstring>

using namespace Death::Containers;

namespace nCine::RHI
{
	// Forward declarations of internal rendering functions from RHI_SW.cpp
	// These are the immediate-mode rasterizers that we call per-tile.
	namespace TileInternal
	{
		void RenderCommandToTile(const DrawContext& ctx, PrimitiveType type,
		                         std::int32_t firstVertex, std::int32_t count,
		                         std::uint8_t* tileBuffer, std::int32_t tileX, std::int32_t tileY,
		                         std::int32_t tileW, std::int32_t tileH,
		                         std::int32_t fbWidth, std::int32_t fbHeight);
	}

	namespace TileRenderer
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

				// Command buffer (flat array, reused each frame)
				DeferredCommand commands[MaxCommands];
				std::int32_t commandCount = 0;

				// Per-tile command index lists for binning
				// Using uint16_t indices into the commands array
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
				std::int32_t workersActive = 0;
				bool shutdownRequested = false;

				// Work distribution
				std::atomic<std::int32_t> nextTileIndex{0};
				std::int32_t flushGeneration = 0; // Incremented each Flush to prevent worker re-entry
				std::int32_t workerGeneration[MaxWorkers] = {}; // Last generation each worker processed
				float clearR = 0, clearG = 0, clearB = 0, clearA = 1;
#endif
			};

			TileState g_tile;

			// Per-tile scratch buffer (thread-local concept - each worker uses its own slice)
			// 32x32x4 = 4096 bytes per tile
			alignas(64) std::uint8_t g_tileScratch[4][TileSize * TileSize * 4];

			// =====================================================================
			// Tile clear using NEON
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
			// Process a single tile: clear, render all binned commands, copy back
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

				// Get thread-local scratch buffer
				std::uint8_t* tileBuf = g_tileScratch[workerIndex];

				// Initialize tile with current framebuffer contents
				// (needed for correct blending with existing content)
				CopyFramebufferToTile(tileBuf, g_tile.targetBuffer,
				                      tileX, tileY, tileW, tileH,
				                      g_tile.fbWidth, g_tile.fbHeight, g_tile.isFboTarget);

				// Render all commands binned to this tile
				for (std::uint16_t cmdIdx : bin) {
					const DeferredCommand& cmd = g_tile.commands[cmdIdx];
					TileInternal::RenderCommandToTile(
						cmd.ctx, cmd.primType, cmd.firstVertex, cmd.count,
						tileBuf, tileX, tileY, tileW, tileH,
						cmd.viewportW, cmd.viewportH);
				}

				// Copy tile back to framebuffer
				CopyTileToFramebuffer(tileBuf, g_tile.targetBuffer,
				                      tileX, tileY, tileW, tileH,
				                      g_tile.fbWidth, g_tile.fbHeight, g_tile.isFboTarget);
			}

#if defined(WITH_THREADS)
			// =====================================================================
			// Worker thread function: process tiles from shared atomic counter
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

					// Process tiles using atomic counter (work-stealing pattern)
					while (true) {
						std::int32_t idx = g_tile.nextTileIndex.fetch_add(1, std::memory_order_relaxed);
						if (idx >= g_tile.totalTiles) {
							break;
						}
						ProcessTile(idx, workerIndex + 1); // +1 because main thread uses slot 0
					}

					// Signal completion
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
#if defined(WITH_THREADS)
			if (!g_tile.initialized) {
				g_tile.shutdownRequested = false;
				g_tile.workersActive = 0;
				g_tile.flushGeneration = 0;

				// Spawn worker threads
				const std::int32_t numWorkers = std::min(
					static_cast<std::int32_t>(Thread::GetProcessorCount()) - 1,
					static_cast<std::int32_t>(TileState::MaxWorkers));

				for (std::int32_t i = 0; i < numWorkers; i++) {
					g_tile.workers[i] = Thread(WorkerThreadFunc,
						reinterpret_cast<void*>(static_cast<std::intptr_t>(i)));
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
#endif
			g_tile.initialized = false;
		}

		bool IsActive()
		{
			return g_tile.initialized;
		}

		void SetTargetBuffer(std::uint8_t* buffer, std::int32_t width, std::int32_t height, bool isFboTarget)
		{
			if DEATH_UNLIKELY(!g_tile.initialized) {
				return;
			}

			// If the target is changing and we have pending commands, flush them first
			if (g_tile.commandCount > 0 && (buffer != g_tile.targetBuffer || width != g_tile.fbWidth || height != g_tile.fbHeight)) {
				Flush();
			}

			// Validate dimensions fit within our static tile bin array
			if DEATH_UNLIKELY(width > MaxWidth || height > MaxHeight || width <= 0 || height <= 0) {
				// Too large for tile renderer - disable until next valid target
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
			if DEATH_UNLIKELY(g_tile.commandCount >= MaxCommands) {
				// Buffer full - flush and retry, or fall back to immediate
				Flush();
				if (g_tile.commandCount >= MaxCommands) return false;
			}

			// Use the actual viewport dimensions for NDC→screen transform
			const std::int32_t vpW = g_tile.fbWidth;
			const std::int32_t vpH = g_tile.fbHeight;

			// Compute screen-space AABB from the draw command
			std::int32_t screenMinX, screenMinY, screenMaxX, screenMaxY;

			if DEATH_LIKELY(type == PrimitiveType::TriangleStrip && count == 4 && firstVertex == 0 && ctx.vertexFormat == nullptr) {
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

				// NDC to screen
				const float halfW = vpW * 0.5f;
				const float halfH = vpH * 0.5f;

				float sx0 = (x0 + 1.0f) * halfW;
				float sy0 = (1.0f - y0) * halfH;
				float sx1 = (x1 + 1.0f) * halfW;
				float sy1 = (1.0f - y1) * halfH;
				float sx2 = (x2 + 1.0f) * halfW;
				float sy2 = (1.0f - y2) * halfH;
				float sx3 = (x3 + 1.0f) * halfW;
				float sy3 = (1.0f - y3) * halfH;

				float fMinX = std::min({sx0, sx1, sx2, sx3});
				float fMinY = std::min({sy0, sy1, sy2, sy3});
				float fMaxX = std::max({sx0, sx1, sx2, sx3});
				float fMaxY = std::max({sy0, sy1, sy2, sy3});

				screenMinX = std::max(0, static_cast<std::int32_t>(fMinX));
				screenMinY = std::max(0, static_cast<std::int32_t>(fMinY));
				screenMaxX = std::min(vpW - 1, static_cast<std::int32_t>(fMaxX));
				screenMaxY = std::min(vpH - 1, static_cast<std::int32_t>(fMaxY));
			} else {
				// For non-procedural quads, use full framebuffer bounds (conservative)
				screenMinX = 0;
				screenMinY = 0;
				screenMaxX = vpW - 1;
				screenMaxY = vpH - 1;
			}

			// Scissor clip (Y must be flipped for FBO targets, matching immediate path)
			if DEATH_UNLIKELY(ctx.scissorEnabled) {
				std::int32_t scY0, scY1;
				if (g_tile.isFboTarget) {
					scY0 = g_tile.fbHeight - ctx.scissorRect.Y - ctx.scissorRect.H;
					scY1 = g_tile.fbHeight - 1 - ctx.scissorRect.Y;
				} else {
					scY0 = ctx.scissorRect.Y;
					scY1 = ctx.scissorRect.Y + ctx.scissorRect.H - 1;
				}
				screenMinX = std::max(screenMinX, ctx.scissorRect.X);
				screenMinY = std::max(screenMinY, scY0);
				screenMaxX = std::min(screenMaxX, ctx.scissorRect.X + ctx.scissorRect.W - 1);
				screenMaxY = std::min(screenMaxY, scY1);
			}

			if DEATH_UNLIKELY(screenMinX > screenMaxX || screenMinY > screenMaxY) {
				return true; // Command is fully clipped - accepted but discarded
			}

			// Store command
			const std::int32_t cmdIdx = g_tile.commandCount;
			DeferredCommand& cmd = g_tile.commands[cmdIdx];
			cmd.ctx = ctx;
			// Store scissor in screen-space (Y already flipped for FBO)
			if DEATH_UNLIKELY(ctx.scissorEnabled && g_tile.isFboTarget) {
				cmd.ctx.scissorRect.Y = g_tile.fbHeight - ctx.scissorRect.Y - ctx.scissorRect.H;
			}
			cmd.primType = type;
			cmd.firstVertex = firstVertex;
			cmd.count = count;
			cmd.viewportW = vpW;
			cmd.viewportH = vpH;
			cmd.screenMinX = screenMinX;
			cmd.screenMinY = screenMinY;
			cmd.screenMaxX = screenMaxX;
			cmd.screenMaxY = screenMaxY;
			g_tile.commandCount++;

			// Bin into overlapping tiles (clamp to valid tile range)
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

			// Target buffer must have been set via SetTargetBuffer()
			if (g_tile.targetBuffer == nullptr || g_tile.totalTiles == 0) {
				DiscardPending();
				return;
			}

#if defined(WITH_THREADS)
			// Multi-threaded tile processing using atomic work counter
			g_tile.nextTileIndex.store(0, std::memory_order_relaxed);

			// Wake workers with new generation
			const std::int32_t numWorkers = std::min(
				static_cast<std::int32_t>(Thread::GetProcessorCount()) - 1,
				static_cast<std::int32_t>(TileState::MaxWorkers));

			g_tile.mutex.Lock();
			g_tile.flushGeneration++;
			g_tile.workersActive = numWorkers;
			g_tile.workReady.Broadcast();
			g_tile.mutex.Unlock();

			// Main thread also processes tiles (worker slot 0)
			while (true) {
				std::int32_t idx = g_tile.nextTileIndex.fetch_add(1, std::memory_order_relaxed);
				if (idx >= g_tile.totalTiles) break;
				ProcessTile(idx, 0);
			}

			// Wait for workers to finish
			g_tile.mutex.Lock();
			while (g_tile.workersActive > 0) {
				g_tile.workDone.Wait(g_tile.mutex);
			}
			g_tile.mutex.Unlock();
#else
			// Single-threaded fallback: process tiles sequentially
			for (std::int32_t i = 0; i < g_tile.totalTiles; i++) {
				ProcessTile(i, 0);
			}
#endif

			// Reset for next frame
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