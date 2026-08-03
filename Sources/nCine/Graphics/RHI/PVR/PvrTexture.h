#pragma once

#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

#include <dc/pvr.h>

using namespace Death::Containers;

namespace nCine::RHI::PVR
{
	/**
		@brief Texture object of the PVR backend (aliased as `RHI::Texture`)

		Keeps a LINEAR host store of level 0 in the uploaded pixel format (the same native R8/RG8/RGBA8
		layout the software backend uses) plus a VRAM store padded up to the PowerVR's power-of-two
		dimension requirement (the padding is compensated by @ref GetUScale() / @ref GetVScale(), which the
		device multiplies into every submitted texture coordinate):
		- R8 index textures upload as twiddled 8bpp paletted texels; the palette BANK is selected per draw
		  by the device from the instance's `palOffset` (4 hardware banks of 256 ARGB8888 entries).
		- RGBA8 / RGB8 textures convert to twiddled ARGB4444.
		- RG8 (index + per-pixel alpha) has no paletted PVR equivalent (a paletted texel's alpha comes from
		  the palette entry), so those keep only the linear store and the device requests a per-palette-row
		  CPU bake to ARGB4444 (@ref EnsureBakedArgb4444), cached until the row or texel content changes.
		The shared 256x256 palette texture (labelled "Palettes") is intercepted: it keeps only the linear
		store and the device loads its rows into the hardware palette banks on demand.

		Render targets allocate a non-twiddled RGB565 VRAM surface the tile accelerator renders into
		(`pvr_scene_begin_txr`). Mip levels above 0 and compressed formats are accepted but not stored.
	*/
	class PvrTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit PvrTexture(TextureTarget target);
		~PvrTexture();

		PvrTexture(const PvrTexture&) = delete;
		PvrTexture& operator=(const PvrTexture&) = delete;

		/** @brief Returns a backend-neutral identifier uniquely identifying the texture (feeds material sort keys) */
		inline std::uint32_t GetUniqueId() const {
			return handle_;
		}
		/** @brief Returns the texture target */
		inline TextureTarget GetTarget() const {
			return target_;
		}

		/** @brief Returns the width of level 0 in texels */
		inline std::int32_t GetWidth() const {
			return width_;
		}
		/** @brief Returns the height of level 0 in texels */
		inline std::int32_t GetHeight() const {
			return height_;
		}
		/** @brief Returns the pixel format of the linear host store (native, like the software backend) */
		inline PixelFormat GetFormat() const {
			return format_;
		}
		/** @brief Returns the original upload format (R8/RG8 kept so the palette path can tell them apart) */
		inline PixelFormat GetUploadFormat() const {
			return uploadFormat_;
		}
		/** @brief Returns the byte distance between two consecutive rows of the linear host store */
		inline std::int32_t GetStrideBytes() const {
			return strideBytes_;
		}
		/**
			@brief Returns the base pointer of the linear host store (may be `nullptr` before an upload)

			Static R8/RG8 content keeps its host store run-length compressed to save main memory (see
			@ref StorePixels()), in which case there is no linear pointer to hand out and this returns
			`nullptr`. The only external consumer on this backend is the device's palette-texture path,
			and the palette texture is never compressed.
		*/
		inline const std::uint8_t* GetPixels(std::int32_t level = 0) const {
			static_cast<void>(level);
			return (pixels_.empty() || pixelsCompressed_) ? nullptr : pixels_.data();
		}
		/** @brief Returns a writable base pointer of the linear host store (`nullptr` when the store is compressed) */
		inline std::uint8_t* MutablePixels() {
			return (pixels_.empty() || pixelsCompressed_) ? nullptr : pixels_.data();
		}
		/** @brief Returns the horizontal texture-coordinate wrap mode */
		inline SamplerWrapping GetWrapS() const {
			return wrap_;
		}
		/** @brief Returns the vertical texture-coordinate wrap mode (single stored mode) */
		inline SamplerWrapping GetWrapT() const {
			return wrap_;
		}
		/** @brief Returns the four-channel sampling swizzle (identity by default; informational on PVR) */
		inline const SwizzleChannel* GetSwizzle() const {
			return swizzle_;
		}
		/** @brief Returns the magnification filter */
		inline nCine::SamplerFilter GetMagFiltering() const {
			return magFilter_;
		}
		/** @brief Alias of @ref GetMagFiltering() */
		inline nCine::SamplerFilter GetMagFilter() const {
			return magFilter_;
		}
		/** @brief Returns `true` if the texture is bound as a color render target (tile-accelerator RTT surface) */
		inline bool IsRenderTarget() const {
			return isRenderTarget_;
		}
		/** @brief Marks the texture as (or no longer as) a color render target; becoming one allocates the RTT surface */
		void SetRenderTarget(bool isRenderTarget);
		/** @brief Returns a globally monotonic stamp of the texel store, advanced by every allocation or upload */
		inline std::uint32_t GetContentVersion() const {
			return contentVersion_;
		}

		/** @brief Returns `true` when the VRAM store is 8bpp paletted (draws need a palette bank selected) */
		inline bool IsIndexed() const {
			return (uploadFormat_ == PixelFormat::R8);
		}
		/** @brief Returns `true` when the texture needs the per-palette-row CPU bake (RG8 index + alpha) */
		inline bool NeedsPaletteBake() const {
			return (uploadFormat_ == PixelFormat::RG8);
		}
		/** @brief Returns `true` when this is the intercepted shared palette texture (rows become palette banks) */
		inline bool IsPaletteTexture() const {
			return isPaletteTexture_;
		}

		/** @brief Returns the VRAM texture pointer, or `nullptr` when none exists (RG8 without a bake, palette texture) */
		inline pvr_ptr_t GetVramPointer() const {
			return vram_;
		}
		/**
			@brief Returns the VRAM texture pointer, rebuilding the store if it was reclaimed

			Should be used by the draw path: running out of video memory drops the least recently used
			stores (see @ref AllocateVram()), and a texture that is drawn again afterwards has to be
			uploaded from its main-memory copy once more.
		*/
		pvr_ptr_t AcquireVramPointer();
		/** @brief Returns the PVR texture format word of the VRAM store WITHOUT the palette-bank bits (the device ors them in) */
		inline std::uint32_t GetVramFormat() const {
			return vramFormat_;
		}
		/** @brief Returns the power-of-two padded VRAM width */
		inline std::int32_t GetPaddedWidth() const {
			return paddedWidth_;
		}
		/** @brief Returns the power-of-two padded VRAM height */
		inline std::int32_t GetPaddedHeight() const {
			return paddedHeight_;
		}
		/** @brief Returns the U compensation factor for the power-of-two padding (`realW / paddedW`) */
		inline float GetUScale() const {
			return uScale_;
		}
		/** @brief Returns the V compensation factor for the power-of-two padding (`realH / paddedH`) */
		inline float GetVScale() const {
			return vScale_;
		}

		/**
		 * @brief Returns the VRAM pointer of an ARGB4444 copy of an RG8 store baked through one palette row
		 *
		 * Analogous to the GX backend's bake: index resolved through @p paletteRow (256 RGBA8 entries),
		 * alpha from the texel's own alpha byte; one cached copy, rebuilt when the row/generation/content
		 * changes. Returns `nullptr` on allocation failure.
		 */
		pvr_ptr_t EnsureBakedArgb4444(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex, std::uint32_t paletteGeneration, const void* palette);

		/**
			@brief Returns a writable pointer to the VRAM store, for content that is rebuilt every frame

			Answered only for the formats the hardware samples verbatim from video memory (RGB565), which is
			what lets the cinematics build a frame straight where it will be read from instead of into a buffer
			that is then copied into the host store and copied again into video memory. Returns `nullptr` for
			anything else, and for a store that cannot be allocated.

			@p strideBytes receives the row pitch, which is the power-of-two padded width rather than the
			texture's own. Nothing written here reaches the host copy, so such a texture must be rewritten in
			full every frame and never read back.
		*/
		void* MapStreamingTexels(std::int32_t& strideBytes);

		/** @brief Binds the texture to the specified texture unit on the device */
		bool Bind(std::uint32_t textureUnit) const;
		/** @brief Binds the texture to texture unit 0 */
		inline bool Bind() const {
			return Bind(0);
		}
		/** @brief Unbinds the texture from the unit it was last bound to */
		bool Unbind() const;
		/** @brief Unbinds any texture from the specified texture unit */
		static bool Unbind(std::uint32_t textureUnit);

		/** @brief Allocates level-0 storage of the given format/size and optionally uploads its texels */
		void TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data);
		/** @brief Updates a rectangular subregion of level 0 */
		void TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data);
		/** @brief Allocates immutable level-0 storage of the given format/size (no texels yet) */
		void TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height);
		/** @brief Compressed upload (unsupported, accepted as a no-op) */
		void CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data);
		/** @brief Compressed sub-upload (unsupported, accepted as a no-op) */
		void CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data);
		/** @brief Reads back level-0 texels of the linear host store into client memory */
		void GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels);

		/** @brief Sets the minification filter */
		void SetMinFiltering(nCine::SamplerFilter filter);
		/** @brief Sets the magnification filter */
		void SetMagFiltering(nCine::SamplerFilter filter);
		/** @brief Sets the wrap mode */
		void SetWrap(SamplerWrapping wrap);
		/** @brief Sets the sampling swizzle (stored, informational - the palette path keys off the upload format instead) */
		void SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a);
		/** @brief Sets the highest defined mipmap level (ignored) */
		void SetMaxLevel(std::int32_t maxLevel);
		/** @brief Sets the client pixel-row alignment of uploads (ignored, uploads are tightly packed) */
		static void SetUnpackAlignment(std::int32_t alignment);

		/** @brief Sets a debug label; "Palettes" marks the shared palette texture whose rows become palette banks */
		void SetObjectLabel(StringView label);

		static bool SupportsImmutableStorage() {
			return false;
		}
		static bool SupportsTextureReadback() {
			return true;
		}
		static void ClearErrors() {}
		static bool CheckErrors() {
			return false;
		}
		static void CheckFormatSupport(PixelFormat format) {
			static_cast<void>(format);
		}

		/** @brief Returns the number of bytes occupied by one texel of the given format (0 if unsupported) */
		static std::int32_t BytesPerPixel(PixelFormat format);

	private:
		static std::uint32_t nextHandle_;
		static std::uint32_t nextContentVersion_;

		std::uint32_t handle_;
		std::uint32_t contentVersion_;
		TextureTarget target_;
		PixelFormat format_;
		PixelFormat uploadFormat_;
		std::int32_t width_;
		std::int32_t height_;
		std::int32_t strideBytes_;
		std::int32_t bytesPerPixel_;
		nCine::SamplerFilter minFilter_;
		nCine::SamplerFilter magFilter_;
		SamplerWrapping wrap_;
		SwizzleChannel swizzle_[4];
		mutable std::uint32_t textureUnit_;
		SmallVector<std::uint8_t, 0> pixels_;
		// Whether pixels_ holds the PackBits-compressed image instead of the linear one. The host store
		// exists only so the VRAM store can be rebuilt after eviction (and rows baked per palette), and all
		// of those paths read it strictly front to back - while a full set of linear copies costs several
		// megabytes of the console's 16 MB per level (the two biggest stock levels did not fit). Static
		// indexed content (R8/RG8) compresses to roughly a third, so it is kept compressed and streamed
		// through RlePixelReader where it is consumed.
		bool pixelsCompressed_;
		bool isRenderTarget_;
		bool isPaletteTexture_;

		pvr_ptr_t vram_;
		std::uint32_t vramFormat_;
		/** @brief Bytes per texel the current VRAM store was allocated for, so a format change reallocates it */
		std::int32_t vramBytesPerTexel_;
		std::int32_t paddedWidth_;
		std::int32_t paddedHeight_;
		float uScale_;
		float vScale_;

		// One baked copy is kept per palette row: the tile accelerator consumes textures only at scene end,
		// so rebaking a row that an already submitted quad references would corrupt that quad (several rows
		// are commonly alive within one scene, e.g. text and its shadow). Each copy costs as much video
		// memory as the texture itself, and there are only 8 MB of it shared with the framebuffers, so the
		// count is a compromise between reuse and pressure on the video memory.
		static constexpr std::int32_t BakedSlotCount = 3;
		struct BakedSlot {
			pvr_ptr_t Vram;
			bool Valid;
			std::uint32_t PaletteRow;
			std::uint32_t PaletteGeneration;
			std::uint32_t ContentVersion;
			std::uint32_t LastUsedScene;
			const void* Palette;
		};
		BakedSlot bakedSlots_[BakedSlotCount];
		std::int32_t nextBakedSlot_;


		/**
			@brief Makes sure a VRAM store of the right size exists, reallocating it if the format changed

			A texture does not always end up in the format it was first uploaded in - a true-color one that
			turns out to fit a color table halves in size - and writing the new format into the old allocation
			would either waste half of it or, the other way round, run past the end of it.
		*/
		bool EnsureVramStore(std::int32_t bytesPerTexel);

		// Every texture with video memory attached is linked into this list, most recently used first, so
		// an allocation that runs out of memory can reclaim the least recently used stores (see Reclaim())
		static PvrTexture* liveHead_;
		static PvrTexture* liveTail_;
		PvrTexture* livePrev_;
		PvrTexture* liveNext_;
		// The scene this texture was last drawn in. NeverUsed marks one that has been uploaded but not
		// drawn yet, which must stay evictable - during level loading the scene counter does not advance,
		// so an untouched texture would otherwise look like part of the scene being assembled
		static constexpr std::uint32_t NeverUsed = ~std::uint32_t(0);
		std::uint32_t lastUsedScene_;

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
		void RefreshVramStore();
		void FreeVramStores();

		/** @brief Returns the size of the linear (uncompressed) level-0 image in bytes */
		inline std::int32_t RawPixelsSize() const {
			return strideBytes_ * (height_ > 0 ? height_ : 0);
		}
		/**
			@brief Returns `true` when this texture keeps its host store compressed

			Only static indexed content (R8/RG8) qualifies: those stores are read strictly sequentially
			(VRAM rebuild, per-palette-row bake), so they can be streamed out of a compressed form. The
			palette texture is excluded because the device reads its rows directly through @ref GetPixels(),
			and render targets have no meaningful host store at all.
		*/
		bool CanCompressPixels() const;
		/** @brief Replaces the whole host store with @p data (compressed when @ref CanCompressPixels()) */
		void StorePixels(const std::uint8_t* data);
		/**
			@brief Makes the host store linear and writable, decompressing (or zero-filling) it if needed

			Used by partial sub-uploads (tileset overrides), which patch arbitrary rows in place - the only
			access pattern the compressed form cannot serve.
		*/
		void MaterializePixelsRaw();
		/** @brief Re-compresses a linear host store after an in-place patch, if this texture qualifies */
		void RecompressPixels();

		/** @brief Moves this texture to the front of the live list and stamps it with the current scene */
		void Touch();
		/**
			@brief Links this texture at the back of the live list if it is not in it yet

			Attaching video memory has to make the texture visible to the eviction walk right away - a store
			uploaded during level loading would otherwise be unreachable (and its memory unreclaimable) until
			the first draw calls @ref Touch(). Linked least recently used, so untouched uploads are also the
			first to go when memory runs short.
		*/
		void LinkAsLeastRecent();
		/** @brief Unlinks this texture from the live list */
		void Unlink();

		/**
			@brief Allocates video memory, freeing the least recently used stores if necessary

			The 8 MB of video memory is shared by the framebuffers, the vertex buffer and every texture, so
			a level with many tilesets (or a menu section that pulls in more graphics) can exhaust it. The
			stores are a cache of the copies kept in main memory, so instead of failing the allocation the
			oldest ones are dropped and rebuilt when they are needed again.
		*/
		static pvr_ptr_t AllocateVram(std::size_t size, const PvrTexture* keepAlive);
	};
}
