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

		/** @brief Largest destination width the static tile-bin array is sized for (4K UHD; ~1.2 MB of bins) */
		static constexpr std::int32_t MaxWidth = 3840;
		/** @brief Largest destination height the static tile-bin array is sized for (4K UHD) */
		static constexpr std::int32_t MaxHeight = 2160;
		/** @brief Number of tile columns at @ref MaxWidth */
		static constexpr std::int32_t MaxTilesX = (MaxWidth + TileSize - 1) / TileSize;
		/** @brief Number of tile rows at @ref MaxHeight */
		static constexpr std::int32_t MaxTilesY = (MaxHeight + TileSize - 1) / TileSize;
		/** @brief Total number of tile bins the layer can address */
		static constexpr std::int32_t MaxTiles = MaxTilesX * MaxTilesY;

		/**
			@brief Submit-time precomputed state of a procedural sprite-quad command

			Everything a quad command re-derived per tile before is computed once in
			@ref SubmitCommand() (via `TileInternal::PrepareQuad`) and carried here: the four transformed
			screen-space vertices (the exact @c FetchVertex output, including the pixel snap), the
			texture / tint / blend derivation shared by both quad rasterizers, and the axis-aligned or
			affine setup (fixed-point UV steps resp. UV gradients and edge-function increments). A
			full-screen quad at 720p used to redo all of this ~900x per flush - once per binned tile.
			Only the tile/scissor clip and the per-row state remain per-tile, computed from these fields
			with the identical expressions, so the rasterized pixels are bit-identical.

			`valid == false` marks a command the tile path treats as a no-op (a degenerate quad the
			rasterizers would reject) or a non-quad command that takes the generic primitive path.
		*/
		struct PreparedQuad
		{
			/** @brief Whether the fields below are filled (procedural quad); `false` = generic path / no-op */
			bool valid;
			/** @brief Whether the quad is axis-aligned (selects the axis-aligned or the affine rasterizer) */
			bool axisAligned;
			/** @brief The four transformed screen-space vertices (exact `FetchVertex` output, snap included) */
			Vertex2D v[4];

			// -- Shared derived state (both quad rasterizers) --

			/** @brief Level-0 texel base of the sampled texture, or `nullptr` for an untextured draw */
			const std::uint8_t* texPixels;
			/** @brief Sampled texture width in texels (0 when untextured) */
			std::int32_t texW;
			/** @brief Sampled texture height in texels (0 when untextured) */
			std::int32_t texH;
			/** @brief Horizontal wrap mode of the sampled texture */
			nCine::SamplerWrapping wrapS;
			/** @brief Vertical wrap mode of the sampled texture */
			nCine::SamplerWrapping wrapT;
			/** @brief Whether the texture is sampled bilinearly */
			bool useLinear;
			/** @brief Tint red in `[0, 255]` */
			std::int32_t tR;
			/** @brief Tint green in `[0, 255]` */
			std::int32_t tG;
			/** @brief Tint blue in `[0, 255]` */
			std::int32_t tB;
			/** @brief Tint alpha in `[0, 255]` */
			std::int32_t tA;
			/** @brief Whether the tint is a full-white no-op */
			bool whiteTint;
			/** @brief Whether blending is enabled */
			bool useBlend;
			/** @brief Whether the blend factors are the SrcAlpha / OneMinusSrcAlpha fast pair */
			bool useFastBlend;
			/** @brief Whether the draw is a constant-color solid fill (no texture, constant fragment output) */
			bool constantFill;
			/** @brief The constant fill color (the fragment's packed output), valid when @ref constantFill */
			std::uint8_t constColor[4];

			// -- Axis-aligned setup (valid when axisAligned) --

			/** @brief Screen-space bounding box minimum X */
			float fxMin;
			/** @brief Screen-space bounding box maximum X */
			float fxMax;
			/** @brief Screen-space bounding box minimum Y */
			float fyMin;
			/** @brief Screen-space bounding box maximum Y */
			float fyMax;
			/** @brief Bounding box width (`fxMax - fxMin`) */
			float fullW;
			/** @brief Bounding box height (`fyMax - fyMin`) */
			float fullH;
			/** @brief U at the left edge */
			float uLeft;
			/** @brief U at the right edge */
			float uRight;
			/** @brief V at the top edge */
			float vTop;
			/** @brief V at the bottom edge */
			float vBot;
			/** @brief Texture width in 16.16 fixed point */
			std::int32_t texWFix;
			/** @brief Horizontal fixed-point UV step per pixel */
			std::int32_t dtxFix;
			/** @brief Vertical fixed-point UV step per pixel */
			std::int32_t dtyFix;
			/** @brief Whether the horizontal wrap is Repeat */
			bool useRepeatS;
			/** @brief Whether the vertical wrap is Repeat */
			bool useRepeatT;
			/** @brief Whether the horizontal wrap is ClampToEdge */
			bool useClampS;
			/** @brief Whether the scanline-buffer (gather + SIMD blend) path applies */
			bool useScanBuf;

			// -- Affine setup (valid when !axisAligned) --

			/** @brief Affine UV gradient du/dx */
			float dudx;
			/** @brief Affine UV gradient du/dy */
			float dudy;
			/** @brief Affine UV gradient dv/dx */
			float dvdx;
			/** @brief Affine UV gradient dv/dy */
			float dvdy;
			/** @brief Per-pixel edge-function increments of triangle 1 (v0, v1, v2) */
			float t1w0dx, t1w1dx, t1w2dx;
			/** @brief Per-pixel edge-function increments of triangle 2 (v2, v1, v3) */
			float t2w0dx, t2w1dx, t2w2dx;
			/** @brief Whether triangle 1 has non-degenerate area */
			bool tri1Valid;
			/** @brief Whether triangle 2 has non-degenerate area */
			bool tri2Valid;
			/** @brief Sign of triangle 1's area (coverage-test orientation) */
			bool sign1;
			/** @brief Sign of triangle 2's area */
			bool sign2;
			/** @brief du/dx in 16.16 fixed point, premultiplied by the texture width */
			std::int32_t dudxFix;
			/** @brief dv/dx in 16.16 fixed point, premultiplied by the texture height */
			std::int32_t dvdxFix;
		};

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

			/**
			 * @brief Index of this command's @ref SwPaletteLut in the flush window's LUT pool, or `-1`
			 *
			 * An index rather than a pointer because the pool still grows while commands are being
			 * submitted; @ref Flush() resolves it into @ref DrawContext::paletteLut once the pool is final.
			 */
			std::int32_t paletteLutIndex;

			/**
			 * @brief Snapshot of a general (vertex-fed) draw's interleaved clip-space vertices
			 *
			 * @ref DrawContext::vertexData points into the device's per-draw scratch buffer, which is
			 * overwritten by the next draw; deferral outlives that, so @ref SubmitCommand copies the
			 * vertex floats here and repoints @ref ctx.vertexData at it (mirrors @ref userDataStorage).
			 * Heap capacity is retained across reuse of the command slot.
			 */
			SmallVector<float, 0> vertexStorage;

			/** @brief Submit-time precomputed vertices and derived state of a procedural quad command */
			PreparedQuad prep;
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
