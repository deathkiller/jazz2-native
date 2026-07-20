#if defined(WITH_RHI_SOFTWARE)

#include "SwTileRenderer.h"
#include "SwShaderRuntime.h"	// sw::swTexture / sw::floor / sw::mod, replicated by the palette-LUT builder

#include <Containers/SmallVector.h>

#if defined(DEATH_ENABLE_NEON)
#	include <arm_neon.h>
#endif

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstring>

using namespace Death::Containers;

namespace nCine::RHI::Software
{
	// Per-tile rasterization entry points, defined in SwTileRasterizer.cpp. Kept in this internal namespace
	// so they have external linkage across the two translation units without leaking into the public API.
	namespace TileInternal
	{
		void PrepareQuad(const DrawContext& ctx, std::int32_t viewportX, std::int32_t viewportY,
		                 std::int32_t viewportW, std::int32_t viewportH, SwTileRenderer::PreparedQuad& prep);

		void RenderCommandToTile(const DrawContext& ctx, const SwTileRenderer::PreparedQuad* prep,
		                         PrimitiveType type,
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
			// Cache key of one palette LUT within a flush window. Draws sharing the palette texture (and
			// its content version), the palette row offset, the tint and the index-texture byte mapping
			// produce identical tables - and one frame of tile-layer draws submits hundreds of such
			// commands - so SubmitCommand reuses a pooled table instead of rebuilding 256 entries per draw.
			struct PaletteLutKey
			{
				const SwTexture* palette;
				std::uint32_t paletteVersion;
				float paletteOffset;
				float tint[4];
				std::int32_t indexByteOffset;
				std::int32_t alphaByteOffset;
			};

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

				// Palette-LUT pool of the current flush window (parallel arrays; commands store indices,
				// resolved to pointers by Flush once the pool stops growing). Reset by DiscardPending.
				SmallVector<SwPaletteLut, 0> paletteLuts;
				SmallVector<PaletteLutKey, 0> paletteLutKeys;

				// Current render target buffer
				std::uint8_t* targetBuffer = nullptr;
				bool isFboTarget = false;

				// Worker threads for parallel tile processing
#if defined(WITH_THREADS)
				// Upper bound on worker threads; the runtime count leaves CPU headroom (see Initialize) so
				// the main thread (which also processes tiles) keeps a core and background threads (audio
				// decode/mixer, OS, other processes) don't preempt a tile worker mid-flush. PS Vita is
				// pinned to 3 workers below.
				static constexpr std::int32_t MaxWorkers = 15;
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

			// =====================================================================
			// Palette-LUT builder for the PaletteRemap fast path (see SwPaletteLut in SwRaster.h)
			// =====================================================================

			// Maps a sampling-swizzle source channel to its byte offset in the promoted RGBA8 texel, or a
			// negative value for the non-byte sources (Zero / One)
			inline std::int32_t SwizzleChannelByte(SwizzleChannel channel)
			{
				switch (channel) {
					case SwizzleChannel::Red:	return 0;
					case SwizzleChannel::Green:	return 1;
					case SwizzleChannel::Blue:	return 2;
					case SwizzleChannel::Alpha:	return 3;
					default:					return -3;
				}
			}

			// Validates the fast-path constraints for a PaletteRemap draw (ctx.paletteRemapHint) and returns
			// the index of a pooled SwPaletteLut for it, building one when no cached table matches. Returns -1
			// when any constraint fails - the draw then keeps the generic transpiled fragment, so the LUT is a
			// pure optimization. `ctx` must be the command's own snapshot (its userData already repointed).
			std::int32_t AcquirePaletteLut(const DrawContext& ctx)
			{
				// The parameter block is the transpiled fragment's single vPaletteOffset float
				if (ctx.fragmentShader == nullptr || ctx.fragmentShaderUserData == nullptr ||
				    ctx.fragmentShaderUserDataSize < sizeof(float)) {
					return -1;
				}
				if (!ctx.ff.hasTexture || ctx.ff.textureUnit < 0 || ctx.ff.textureUnit >= std::int32_t(MaxTextureUnits)) {
					return -1;
				}
				const SwTexture* indexTex = ctx.textures[ctx.ff.textureUnit];
				if (indexTex == nullptr || indexTex->GetPixels(0) == nullptr ||
				    indexTex->GetWidth() <= 0 || indexTex->GetHeight() <= 0) {
					return -1;
				}
				// The rasterizer's gather phases fetch raw (unswizzled) texel bytes, which the LUT indexes by.
				// A bilinear gather would average palette indices - something the fragment's own nearest
				// re-sample never sees - so the fast path requires nearest sampling. The condition mirrors
				// the quad rasterizers' useLinear exactly.
				if (indexTex->GetMagFilter() == nCine::SamplerFilter::Linear && indexTex->GetWidth() > 1 && indexTex->GetHeight() > 1) {
					return -1;
				}
				// The fragment reads the index through the texture's sampling swizzle: `.r` selects the index
				// byte and `.a` the source-alpha byte. An R8 index texture keeps the identity swizzle (its
				// promoted store is [index,255,255,255], so `.a` reads the constant 255 = opaque); an RG8 one
				// maps `.a` to green ([index,alpha,255,255]). Zero / One in `.a` fold to a constant.
				const SwizzleChannel* swizzle = indexTex->GetSwizzle();
				const std::int32_t indexByteOffset = SwizzleChannelByte(swizzle[0]);
				if (indexByteOffset < 0) {
					return -1;
				}
				std::int32_t alphaByteOffset = SwizzleChannelByte(swizzle[3]);
				if (alphaByteOffset < 0) {
					if (swizzle[3] == SwizzleChannel::One) {
						alphaByteOffset = -1;	// Constant 1.0 source alpha
					} else if (swizzle[3] == SwizzleChannel::Zero) {
						alphaByteOffset = -2;	// Constant 0.0 source alpha
					} else {
						return -1;
					}
				}
				// The fragment samples the palette on unit 1 (PaletteRemap.shader: texture_unit(1)). A missing
				// palette is fine (swTexture yields transparent black, baked below the same way), but a
				// CPU-rendered target could change without a content-version bump, so it stays generic.
				const SwTexture* palette = ctx.textures[1];
				if (palette != nullptr && palette->IsRenderTarget()) {
					return -1;
				}

				PaletteLutKey key;
				key.palette = palette;
				key.paletteVersion = (palette != nullptr ? palette->GetContentVersion() : 0);
				key.paletteOffset = *reinterpret_cast<const float*>(ctx.fragmentShaderUserData);
				key.tint[0] = ctx.ff.color[0];
				key.tint[1] = ctx.ff.color[1];
				key.tint[2] = ctx.ff.color[2];
				key.tint[3] = ctx.ff.color[3];
				key.indexByteOffset = indexByteOffset;
				key.alphaByteOffset = alphaByteOffset;

				// Most draws of a window share one palette / tint (tile layers submit runs of hundreds), so a
				// most-recent-first linear scan almost always hits its first entry
				for (std::int32_t i = std::int32_t(g_tile.paletteLutKeys.size()) - 1; i >= 0; i--) {
					const PaletteLutKey& k = g_tile.paletteLutKeys[i];
					if (k.palette == key.palette && k.paletteVersion == key.paletteVersion &&
					    k.paletteOffset == key.paletteOffset &&
					    k.tint[0] == key.tint[0] && k.tint[1] == key.tint[1] &&
					    k.tint[2] == key.tint[2] && k.tint[3] == key.tint[3] &&
					    k.indexByteOffset == key.indexByteOffset && k.alphaByteOffset == key.alphaByteOffset) {
						return i;
					}
				}

				// Build a new table by replicating the transpiled PaletteRemap fragment for each of the 256
				// index bytes, with the identical float operations in the identical order so the results are
				// bit-exact. floor(src.r * 255 + 0.5) recovers the index byte exactly (src.r is idx / 255), so
				// per index everything but the per-pixel source-alpha factor collapses to constants.
				g_tile.paletteLutKeys.push_back(key);
				SwPaletteLut& lut = g_tile.paletteLuts.emplace_back();
				lut.tintAlpha = ctx.ff.color[3];
				lut.indexByteOffset = indexByteOffset;
				lut.alphaByteOffset = alphaByteOffset;

				FragmentShaderInput paletteInput = {};
				paletteInput.textures = ctx.textures;
				// The constant source alpha baked into packed[][3]: 1.0 both for the swizzle-One case and for
				// the per-pixel case's alpha-byte-255 shortcut (255/255 == 1.0 exactly); 0.0 for swizzle-Zero
				const float bakedSrcAlpha = (alphaByteOffset == -2 ? 0.0f : 1.0f);
				for (std::int32_t i = 0; i < 256; i++) {
					const float srcR = i / 255.0f;
					const float palIndex = sw::floor(key.paletteOffset + 0.5f) + sw::floor(srcR * 255.0f + 0.5f);
					const float palX = (sw::mod(palIndex, 256.0f) + 0.5f) / 256.0f;
					const float palY = (sw::floor(palIndex / 256.0f) + 0.5f) / 256.0f;
					const sw::vec4 color = sw::swTexture(paletteInput, 1, sw::vec2(palX, palY));
					lut.packed[i][0] = SwQuantizeColor(color.r * ctx.ff.color[0]);
					lut.packed[i][1] = SwQuantizeColor(color.g * ctx.ff.color[1]);
					lut.packed[i][2] = SwQuantizeColor(color.b * ctx.ff.color[2]);
					lut.packed[i][3] = SwQuantizeColor((color.a * bakedSrcAlpha) * lut.tintAlpha);
					// color.a always originates from a byte (or the exact 0.0 / 1.0 of a Zero / One swizzle),
					// so this recovers it losslessly; dividing it by 255 at apply time reproduces the exact
					// float the fragment's own sample would produce
					lut.palAlphaByte[i] = std::uint8_t(std::int32_t(color.a * 255.0f + 0.5f));
				}
				return std::int32_t(g_tile.paletteLuts.size()) - 1;
			}

			// Per-tile scratch buffer (each worker uses its own slice; slot 0 belongs to the main thread,
			// slots 1..MaxWorkers to the workers): 32x32x4 = 4096 bytes per slice, so each slice also starts
			// on its own cache line
#if defined(WITH_THREADS)
			alignas(64) std::uint8_t g_tileScratch[TileState::MaxWorkers + 1][TileSize * TileSize * 4];
#else
			alignas(64) std::uint8_t g_tileScratch[1][TileSize * TileSize * 4];
#endif

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
						cmd.ctx, &cmd.prep, cmd.primType, cmd.firstVertex, cmd.count,
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
			// Leave scheduling headroom instead of saturating every logical CPU: the flush barrier waits
			// for ALL workers, so with `workers + main == logical CPUs` any other runnable thread (audio
			// decode/mixer, OS, a browser in the background) preempts one worker for a scheduler quantum
			// and stalls the whole frame - measured on a 16-thread CPU as random 10-60 ms frame spikes
			// several times per second. Reserving 1 logical CPU for the main thread (it rasterizes tiles
			// too) plus a quarter of the CPUs (at least 2) for everything else eliminated the spikes with
			// an unchanged median frame time (the extra workers bought nothing once the CPU was saturated).
			// Small machines keep at least the 3 workers the tile renderer always used there.
			const std::int32_t logicalCpus = static_cast<std::int32_t>(Thread::GetProcessorCount());
			const std::int32_t reservedCpus = std::max(2, logicalCpus / 4);
			const std::int32_t numWorkers = std::clamp(
				logicalCpus - 1 - reservedCpus,
				std::min(3, logicalCpus - 1),
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

			// Line and point primitives are rasterized only by the immediate path (SwRaster) - they are
			// rare (the HUD weapon wheel's arcs) and a tile-clipped line walk is not worth its complexity
			// here. Declining makes the caller flush the queue first, so the draw order is preserved.
			if DEATH_UNLIKELY(type == PrimitiveType::Points || type == PrimitiveType::Lines ||
			                  type == PrimitiveType::LineLoop || type == PrimitiveType::LineStrip) {
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

			// Fill the command slot first: the submit-time quad preparation below runs on the snapshot (its
			// userData pointer must already be the stable per-command copy). A discarded command simply never
			// increments commandCount, so the slot is reused by the next submission.
			const std::int32_t cmdIdx = g_tile.commandCount;
			DeferredCommand& cmd = g_tile.commands[cmdIdx];
			cmd.ctx = ctx;
			// Snapshot the fragment-callback parameter block into the command's own storage (ctx points at
			// caller-stack memory) and repoint, so the deferred draw carries its own copy.
			if (ctx.fragmentShader != nullptr && ctx.fragmentShaderUserData != nullptr) {
				std::memcpy(cmd.userDataStorage, ctx.fragmentShaderUserData, ctx.fragmentShaderUserDataSize);
				cmd.ctx.fragmentShaderUserData = cmd.userDataStorage;
			}
			// Snapshot a general draw's vertices the same way: ctx.vertexData points into the device's
			// per-draw scratch buffer, which the next draw overwrites, whereas the deferred draw outlives it.
			if (ctx.vertexData != nullptr) {
				const std::int32_t strideFloats = (ctx.vertexStride > 0 ? ctx.vertexStride / std::int32_t(sizeof(float)) : 4);
				const std::size_t floatCount = (std::size_t(firstVertex) + std::size_t(count)) * std::size_t(strideFloats);
				const float* src = static_cast<const float*>(ctx.vertexData);
				cmd.vertexStorage.assign(src, src + floatCount);
				cmd.ctx.vertexData = cmd.vertexStorage.data();
			}
			// scissorRect.Y is stored in top-down screen space so the tile rasterizer can use it directly as a
			// pixel-row clip. ctx.scissorRect.Y is bottom-up (the RHI scissor convention), so flip it here.
			if DEATH_UNLIKELY(ctx.scissorEnabled) {
				cmd.ctx.scissorRect.Y = g_tile.fbHeight - ctx.scissorRect.Y - ctx.scissorRect.H;
			}

			// Compute the screen-space AABB from the draw command
			std::int32_t screenMinX, screenMinY, screenMaxX, screenMaxY;
			bool accurateBounds;

			if DEATH_LIKELY(type == PrimitiveType::TriangleStrip && count == 4 && firstVertex == 0 && ctx.vertexData == nullptr) {
				// Procedural sprite quad: run the submit-time preparation (the four FetchVertex transforms
				// plus the texture / tint / blend / UV-step derivation the tile rasterizers used to redo per
				// binned tile, see PreparedQuad) and take the binning bounds from the exact screen-space
				// vertices the rasterizer will consume.
				TileInternal::PrepareQuad(cmd.ctx, vpX, vpY, vpW, vpH, cmd.prep);
				if DEATH_UNLIKELY(!cmd.prep.valid) {
					return true; // Degenerate quad - accepted but discarded (it draws nothing on any path)
				}
				screenMinX = std::max(0, static_cast<std::int32_t>(cmd.prep.fxMin));
				screenMinY = std::max(0, static_cast<std::int32_t>(cmd.prep.fyMin));
				screenMaxX = std::min(g_tile.fbWidth - 1, static_cast<std::int32_t>(cmd.prep.fxMax));
				screenMaxY = std::min(g_tile.fbHeight - 1, static_cast<std::int32_t>(cmd.prep.fyMax));
				accurateBounds = true;
			} else if (cmd.ctx.vertexData != nullptr) {
				// General vertex-fed draw: bin by the transformed vertices' bounding box (the same NDC ->
				// screen mapping the rasterizer's vertex fetch applies, padded a pixel for its snap).
				// accurateBounds stays false - an AABB does not promise the geometry covers it, and the
				// read-back-skip optimization must only fire for full-cover draws.
				cmd.prep.valid = false;
				const std::int32_t strideFloats = (cmd.ctx.vertexStride > 0 ? cmd.ctx.vertexStride / std::int32_t(sizeof(float)) : 4);
				const float* v = static_cast<const float*>(cmd.ctx.vertexData) + std::size_t(firstVertex) * std::size_t(strideFloats);
				float fxMin = FLT_MAX, fxMax = -FLT_MAX, fyMin = FLT_MAX, fyMax = -FLT_MAX;
				for (std::int32_t i = 0; i < count; i++, v += strideFloats) {
					const float sx = (v[0] + 1.0f) * 0.5f * static_cast<float>(vpW) + static_cast<float>(vpX);
					const float sy = (1.0f - v[1]) * 0.5f * static_cast<float>(vpH) + static_cast<float>(vpY);
					fxMin = std::min(fxMin, sx);
					fxMax = std::max(fxMax, sx);
					fyMin = std::min(fyMin, sy);
					fyMax = std::max(fyMax, sy);
				}
				screenMinX = std::max(0, static_cast<std::int32_t>(fxMin) - 1);
				screenMinY = std::max(0, static_cast<std::int32_t>(fyMin) - 1);
				screenMaxX = std::min(g_tile.fbWidth - 1, static_cast<std::int32_t>(fxMax) + 1);
				screenMaxY = std::min(g_tile.fbHeight - 1, static_cast<std::int32_t>(fyMax) + 1);
				accurateBounds = false;
			} else {
				// For non-procedural quads, use full framebuffer bounds (conservative)
				cmd.prep.valid = false;
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

			// PaletteRemap fast path: bake the command's palette row + tint into a pooled 256-entry LUT the
			// tile rasterizer indexes instead of calling the transpiled fragment per pixel. -1 (constraint
			// not met) keeps the generic fragment. Uses the snapshot ctx so the userData pointer is stable.
			cmd.paletteLutIndex = (ctx.paletteRemapHint ? AcquirePaletteLut(cmd.ctx) : -1);
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

			// Resolve the palette-LUT pool indices into pointers: submissions are done for this window, so
			// the pool no longer grows and the pointers stay stable for every worker
			for (std::int32_t i = 0; i < g_tile.commandCount; i++) {
				DeferredCommand& cmd = g_tile.commands[i];
				cmd.ctx.paletteLut = (cmd.paletteLutIndex >= 0 ? &g_tile.paletteLuts[cmd.paletteLutIndex] : nullptr);
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
			// The palette LUTs belong to the discarded commands (keys include per-window texture versions)
			g_tile.paletteLuts.clear();
			g_tile.paletteLutKeys.clear();
		}

		std::int32_t GetPendingCommandCount()
		{
			return g_tile.commandCount;
		}
	}
}

#endif
