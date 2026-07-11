#pragma once

#include "../Main.h"
#include "GameDifficulty.h"
#include "LevelDescriptor.h"
#include "PlayerType.h"
#include "Resources.h"
#include "UI/Font.h"

#include "../nCine/Audio/AudioBuffer.h"
#include "../nCine/Audio/AudioStreamPlayer.h"
#include "../nCine/Graphics/Texture.h"
#include "../nCine/Base/HashMap.h"

#include <Containers/Function.h>
#include <Containers/Pair.h>
#include <Containers/Reference.h>
#include <Containers/SmallVector.h>
#include <Containers/StringView.h>
#include <IO/PakFile.h>
#include <IO/Stream.h>

#include "../nCine/Graphics/RHI/RhiFwd.h"

namespace nCine
{
	class RenderCommand;
}

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Jazz2::Resources;
using namespace nCine;

namespace Jazz2
{
	namespace Tiles
	{
		class TileSet;
	}

	/**
		@brief Manages loading of assets
		
		Process-wide singleton (see @ref ContentResolver::Get) that locates and loads every kind of game asset ---
		metadata, graphics, tile sets, levels, episodes, fonts, music and shaders --- caching the results so repeated
		requests are cheap. It also owns the shared 256x256 palette texture and the palette pipeline: applying tileset
		and sprite palettes, building and reference-counting per-player recolor palettes from a packed fur color, and
		configuring the palette-aware sprite shaders. In headless mode it skips all GPU work.
	*/
	class ContentResolver
	{
	public:
		/** @{ @name Constants */

		/** @brief Pixel size in bytes */
		static constexpr std::uint32_t PixelSize = 4;
		/** @brief Maximum number of palettes */
		static constexpr std::int32_t PaletteCount = 256;
		/** @brief Number of colors per palette */
		static constexpr std::int32_t ColorsPerPalette = 256;
		/**
		 * @brief First shared-palette row available for dynamic per-player allocation
		 *
		 * Lower rows are reserved for the sprite palette (row 0) and the generated gem palettes (rows 1-2).
		 */
		static constexpr std::int32_t FirstDynamicPaletteRow = 8;
		/** @brief Invalid value */
		static constexpr std::int32_t InvalidValue = INT_MAX;

		/** @{ @name Player recolor palette sections */

		/**
		 * @brief Number of recolorable fur sections
		 *
		 * @section player-recolor How a player's 4-byte fur color maps to recolored palette sections
		 *
		 * The fur color is 4 bytes, one per section (as in the original game). The low 7 bits of each byte are the
		 * starting palette index of an 8-color gradient in the sprite palette; those 8 colors are copied into the
		 * section's destination block(s) in the per-player palette (a byte of 0 keeps the original colors for that
		 * section). If @ref FurHueShiftFlag is also set, the gradient's hue is rotated by @ref HueShiftDegreesForGradient
		 * first (see @ref ShiftHue), which roughly doubles the selectable color variants without extra gradients.
		 *
		 * Every character's scheme uses the same section order --- Primary Fur, Secondary Fur, Weapon, Misc --- so a
		 * picked color recolors the analogous region on each one. The *destination* blocks differ per character
		 * (@ref GetFurSchemeIndex) because their sprites are drawn with different palette ranges: Jazz uses the standard
		 * rabbit ranges (0x10/0x18/0x20/0x28); Spaz reorders them to his own fur layout; Lori uses entirely different
		 * ranges (hair, purple suit, dark-blue weapon, lips), with her eyes left at their original color. The
		 * per-character schemes are defined in the source file.
		 */
		static constexpr std::int32_t FurSectionCount = 4;
		/** @brief Number of consecutive palette indices per fur section block */
		static constexpr std::int32_t FurSectionSize = 8;
		/**
		 * @brief Maximum number of destination blocks a single fur section can recolor
		 *
		 * Most sections map to one block, but a section may span several (e.g., Lori's hair occupies both the 0x28
		 * highlight block and the 0x38 gold-ramp block, recolored together as one logical section).
		 */
		static constexpr std::int32_t FurSectionMaxBlocks = 2;
		/**
		 * @brief High bit of a fur section byte requesting its gradient be hue-shifted by @ref HueShiftDegreesForGradient
		 *
		 * The low 7 bits stay the gradient start index (gradient starts never exceed 0x58, so this bit is always free)
		 */
		static constexpr std::uint8_t FurHueShiftFlag = 0x80;

		/** @} */

#ifndef DOXYGEN_GENERATING_OUTPUT
		static constexpr std::uint8_t LevelFile = 1;
		static constexpr std::uint8_t EpisodeFile = 2;
		static constexpr std::uint8_t CacheIndexFile = 3;
		static constexpr std::uint8_t ConfigFile = 4;
		static constexpr std::uint8_t StateFile = 5;
		static constexpr std::uint8_t SfxListFile = 6;
		static constexpr std::uint8_t HighscoresFile = 7;
#endif

		/** @} */

		/** @brief Returns static instance of main content resolver */
		static ContentResolver& Get();

		~ContentResolver();
		
		/** @brief Releases all cached assets */
		void Release();

		/** @brief Returns path to `"Content"` directory */
		StringView GetContentPath() const;
		/** @brief Returns path to `"Cache"` directory */
		StringView GetCachePath() const;
		/** @brief Returns path to `"Source"` directory */
		StringView GetSourcePath() const;

		/** @brief Returns `true` if the application is running in headless mode (i.e., without any display) */
		bool IsHeadless() const;
		/** @brief Sets whether the application is running in headless mode */
		void SetHeadless(bool value);

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		/** @brief Scans the `"Content"` and `"Cache"` directories for `.pak` files and mounts them  */
		void RemountPaks();
#endif
		/** @brief Tries to find and open a file specified by the path */
		std::unique_ptr<Stream> OpenContentFile(StringView path, std::int32_t bufferSize = 8192);
		/**
		 * @brief Tries to open a file from the `"Source"` directory (case-insensitive)
		 *
		 * Used for original/custom game files. The returned stream is invalid if the file wasn't found.
		 */
		std::unique_ptr<Stream> OpenSourceFile(StringView path, std::int32_t bufferSize = 8192);
		
		/** @brief Marks beginning of the loading assets */
		void BeginLoading();
		/** @brief Marks end of the loading assets */
		void EndLoading();

		/** @brief Overrides the default path handler */
		void OverridePathHandler(Function<String(StringView)>&& callback);

		/** @brief Preloads specified metadata and its linked assets to cache */
		void PreloadMetadataAsync(StringView path);
		/**
		 * @brief Loads specified metadata and its linked assets (cached)
		 *
		 * @param path          Relative path to the metadata asset
		 * @param forceIndexed  Load all linked graphics as indexed (palette not baked) so they can be recolored at draw
		 *                      time - used for the player so each player can have a custom color scheme
		 */
		Metadata* RequestMetadata(StringView path, bool forceIndexed = false);
		/**
		 * @brief Loads specified graphics asset (cached)
		 *
		 * @param path           Relative path to the graphics asset
		 * @param paletteOffset  Index of the first palette color used to bake the sprite (ignored when `keepIndexed`)
		 * @param keepIndexed    Keep raw palette indices in the texture (don't bake the palette) for shader recoloring
		 */
		GenericGraphicResource* RequestGraphics(StringView path, std::uint16_t paletteOffset, bool keepIndexed = false);

		/**
		 * @brief Builds a 256-color palette for a player from a packed 4-byte fur color
		 *
		 * Each byte is one section: a gradient start index in the sprite palette (0 = keep the original colors). The
		 * destination blocks recolored by each section depend on `playerType` (see @ref player-recolor).
		 */
		void BuildPlayerColorPalette(std::uint32_t furColor, PlayerType playerType, std::uint32_t* outPalette) const;
		/**
		 * @brief Rotates the hue of a packed `0xAABBGGRR` color by `degrees`
		 *
		 * Rotates in YIQ space, so the result keeps the original's perceived brightness (luma) and chroma - only the
		 * hue changes, and alpha is untouched. Used to derive the per-gradient hue-shifted fur variants.
		 */
		static std::uint32_t ShiftHue(std::uint32_t color, float degrees);
		/**
		 * @brief Returns the hue rotation (degrees) for the gradient starting at `gradientStart` when @ref FurHueShiftFlag is set
		 *
		 * Each selectable gradient gets its own angle, chosen so its hue-shifted twin lands where the base gradients are
		 * sparse (the sprite palette clusters into a warm wedge ~0-73° and a cool wedge ~207-335°, leaving green/cyan/indigo
		 * underrepresented). A single global angle would push ~2 of the cool gradients back onto hues that already exist;
		 * per-gradient angles spread all twins evenly. Returns `0` for the near-neutral gradients (and any unrecognized
		 * start), which barely change under rotation and so get no twin.
		 */
		static float HueShiftDegreesForGradient(std::int32_t gradientStart);
		/**
		 * @brief Builds/updates a standalone 256x1 palette texture for a player fur color and returns it
		 *
		 * `texture` is created on first use. Used for off-screen previews (e.g., the profile menu); in-game recoloring
		 * uses @ref AcquirePaletteOffset instead. The recolored blocks depend on `playerType` (see @ref player-recolor).
		 * Returns `nullptr` in headless mode.
		 */
		Texture* ApplyPlayerColorPalette(std::unique_ptr<Texture>& texture, std::uint32_t furColor, PlayerType playerType);
		/**
		 * @brief Returns the shared 256x256 palette texture, uploading any rows changed since the last call
		 *
		 * Row 0 is the sprite palette, rows 1-2 the gems, rows @ref FirstDynamicPaletteRow+ the dynamically allocated
		 * per-player palettes. Bound to texture unit 1 for palette shaders. Returns `nullptr` in headless mode. Indexed
		 * sprites that must keep their original colors sample row 0 (the default palette) via a palette offset of 0.
		 */
		Texture* GetPaletteTexture();
		/**
		 * @brief Acquires a reference-counted palette offset for the given packed fur color
		 *
		 * The offset is the flat offset into the shared palette texture, passed to @ref ActorBase::ActorRenderer::SetPalette
		 * and the palette-aware shaders. Callers with the same fur color *and* recolor scheme (@ref GetFurSchemeIndex,
		 * derived from `playerType`) share one palette (so e.g., many corpses of the same character cost a single row);
		 * the recolored palette is built on first use. Returns the offset, or -1 if none are free. Release it with
		 * @ref ReleasePaletteOffset when the holder disconnects/despawns.
		 */
		std::int32_t AcquirePaletteOffset(std::uint32_t furColor, PlayerType playerType);
		/**
		 * @brief Releases one reference to a palette offset from @ref AcquirePaletteOffset
		 *
		 * The underlying palette is returned to the pool only when the last holder releases it.
		 */
		void ReleasePaletteOffset(std::int32_t paletteOffset);

		/**
		 * @brief Configures a manually-built render command for a (possibly indexed) sprite
		 *
		 * Selects the @ref PrecompiledShader::PaletteRemap shader when `indexed` (recolored at draw time via the shared
		 * palette), else the plain Sprite program. When the shader changes it also reserves uniform memory, sets
		 * triangle-strip draw params, and points the diffuse/palette samplers at units 0/1. Returns whether the shader
		 * changed (use it to gate one-time per-command setup). For game-world graphics drawn outside the standard
		 * ActorRenderer.
		 */
		bool ConfigureSpriteShader(RenderCommand& command, bool indexed);
		/**
		 * @brief Binds the diffuse (and, when `indexed`, the palette) textures for a sprite render command
		 *
		 * Binds `diffuse` to texture unit 0 and, when `indexed`, the shared palette texture to unit 1 plus the
		 * per-instance palette offset on `instanceBlock`. Call at draw time, after the other instance uniforms.
		 */
		void BindSpritePalette(RenderCommand& command, Rhi::UniformBlockCache& instanceBlock, const Texture& diffuse, bool indexed, std::uint16_t paletteOffset);

		/**
		 * @brief Loads specified tile set and its palette
		 *
		 * @param path              Relative path to the tile set
		 * @param captionTileId     Tile used to render the level-preview caption thumbnail
		 * @param applyPalette      Apply the tile set's palette to the live sprite palette
		 * @param paletteRemapping  Optional 256-entry table remapping each tile's palette indices
		 */
		std::unique_ptr<Tiles::TileSet> RequestTileSet(StringView path, std::uint16_t captionTileId, bool applyPalette, const std::uint8_t* paletteRemapping = nullptr);
		/** @brief Returns `true` if specified level exists */
		bool LevelExists(StringView levelName);
		/** @brief Loads specified level into a level descriptor */
		bool TryLoadLevel(StringView path, GameDifficulty difficulty, LevelDescriptor& descriptor);
		/** @brief Loads default (sprite) palette */
		void ApplyDefaultPalette();
		/**
		 * @brief Overrides the active sprite palette (row 0 of the shared palette)
		 *
		 * Takes up to @ref ColorsPerPalette packed colors (`0xAABBGGRR`) and refreshes everything derived from it (gem
		 * gradients and per-player recolor rows), so the change takes effect on the next frame. No visible effect in
		 * headless mode.
		 */
		void SetSpritePalette(ArrayView<const std::uint32_t> palette);

		/** @brief Returns specified episode by name */
		std::optional<Episode> GetEpisode(StringView name, bool withImages = false);
		/** @brief Returns specified episode by full path */
		std::optional<Episode> GetEpisodeByPath(StringView path, bool withImages = false);
		/** @brief Loads specified music */
		std::unique_ptr<AudioStreamPlayer> GetMusic(StringView path);
		/** @brief Returns specified font type */
		UI::Font* GetFont(FontType fontType);
		/** @brief Returns specified precompiled shader */
		Shader* GetShader(PrecompiledShader shader);
		/** @brief Precompiles all required shaders */
		void CompileShaders();
		/** @brief Returns a noise texture with random pixels */
		std::unique_ptr<Texture> GetNoiseTexture();

		/** @brief Returns currently loaded set of palettes */
		StaticArrayView<PaletteCount * ColorsPerPalette, const std::uint32_t> GetPalettes() const {
			return _palettes;
		}

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct StringRefEqualTo
		{
			inline bool operator()(const Reference<const String>& a, const Reference<const String>& b) const noexcept {
				return a.get() == b.get();
			}
		};
#endif

		ContentResolver();

		ContentResolver(const ContentResolver&) = delete;
		ContentResolver& operator=(const ContentResolver&) = delete;

		void InitializePaths();

		// Cache key offset for indexed graphics (palette indices kept in the texture instead of baked), distinct
		// from any real paletteOffset so indexed and baked variants of the same sprite are cached separately
		static constexpr std::uint16_t IndexedGraphicsCacheKey = UINT16_MAX;

		GenericGraphicResource* RequestGraphicsAura(StringView path, std::uint16_t paletteOffset, bool keepIndexed = false);
		static void ReadImageFromFile(std::unique_ptr<Stream>& s, std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount);
		// Copies a tile's edge pixels into its 1px atlas padding (so sampling never bleeds across tiles); `bytesPerPixel`
		// is 1 for an indexed (R8) atlas or 4 for a baked RGBA atlas
		static void ExpandTileDiffuse(std::uint8_t* pixelsOffset, std::uint32_t widthWithPadding, std::uint32_t bytesPerPixel);

		// Reads the tileset's 256-color palette from the decompressed stream and applies it to the live sprite palette
		// (drops baked fonts, regenerates gem palettes); just skips the bytes when headless or `applyPalette` is false
		void ReadTilesetPalette(Stream& uc, bool applyPalette);
		// Reads the tileset's per-pixel collision mask from the decompressed stream (1 byte per pixel); `maskSizeBits`
		// receives the total number of mask pixels
		static std::unique_ptr<std::uint8_t[]> ReadTilesetMask(Stream& uc, std::uint32_t& maskSizeBits);
		// Builds the padded tileset diffuse atlas (R8 indexed for all-8-bit tilesets, RGBA8 baked when any tile is
		// 32-bit), the per-tile fully-opaque flags and the optional caption thumbnail. Reads pixels from the raw stream
		// `s` (the image content follows the compressed block). Sets `indexTiles` to the chosen mode.
		std::unique_ptr<Texture> BuildTilesetDiffuse(std::unique_ptr<Stream>& s, const char* name, std::uint8_t channelCount,
			std::uint32_t width, std::uint32_t height, std::uint16_t tileCount, const std::uint8_t* is32bitTile,
			const std::uint8_t* paletteRemapping, std::uint16_t captionTileId, bool& indexTiles,
			std::unique_ptr<std::uint8_t[]>& tileDiffuseOpaque, std::unique_ptr<Color[]>& captionTile);
		// Packs an indexed sprite/tile (palette index in the red/first channel) into the smallest texture format: R8
		// when alpha is on/off only (4x less VRAM than RGBA8), or RG8 keeping the per-pixel alpha in green (sampled
		// into .a via swizzle) when alpha is partial. `srcChannels` is the bytes-per-pixel of `pixels`: 1 (index
		// only) or 2 (index + alpha) are pre-packed and upload directly with no scan; 4 (RGBA, e.g., a user-supplied PNG)
		// packs to RG8. Indexed tilesets are handed over as a single index channel by RequestTileSet, taking the R8 path.
		// `paletteBaseTransparent` must be true if the sprite's palette entry 0 (its row base) is transparent - only
		// then can on/off transparency be dropped and reproduced by the palette (false, e.g., for gems, which keep RG8)
		static std::unique_ptr<Texture> CreateIndexedTexture(const char* name, const std::uint8_t* pixels, std::int32_t width, std::int32_t height, std::int32_t srcChannels, bool paletteBaseTransparent);

		std::unique_ptr<Shader> CompileShader(const char* shaderName, Shader::DefaultVertex vertex, const char* fragment, Shader::Introspection introspection = Shader::Introspection::Enabled, std::initializer_list<StringView> defines = {});
		std::unique_ptr<Shader> CompileShader(const char* shaderName, const char* vertex, Shader::DefaultFragment fragment, Shader::Introspection introspection = Shader::Introspection::Enabled, std::initializer_list<StringView> defines = {});
		std::unique_ptr<Shader> CompileShader(const char* shaderName, const char* vertex, const char* fragment, Shader::Introspection introspection = Shader::Introspection::Enabled, std::initializer_list<StringView> defines = {});
		
		void RecreateGemPalettes();
		// Expands the dirty palette-row range so the next GetPaletteTexture re-uploads at least rows [firstRow, lastRow]
		void MarkPaletteDirty(std::int32_t firstRow, std::int32_t lastRow);
		// Rebuilds every in-use dynamic palette row from the current base palette (after the sprite palette changes)
		void RefreshDynamicPaletteRows();
		// Maps a player type to its fur recolor scheme index (which destination blocks each section recolors)
		static std::int32_t GetFurSchemeIndex(PlayerType playerType);
		// Core recolor builder, keyed by the scheme index stored on each dynamic palette row
		void BuildPlayerColorPaletteForScheme(std::uint32_t furColor, std::int32_t schemeIndex, std::uint32_t* outPalette) const;
#if defined(DEATH_DEBUG)
		void MigrateGraphics(StringView path);
#endif

		bool _isHeadless;
		bool _isLoading;
		std::uint32_t _palettes[PaletteCount * ColorsPerPalette];
		// Shared palette texture (256x256: one palette per row). Rows changed since the last upload are tracked by
		// the dirty range below and re-uploaded lazily. Dynamically allocated per-player rows are reference-counted
		// (_paletteRowRefCount > 0 = in use) and keyed by both fur color (_paletteRowColor) and recolor scheme
		// (_paletteRowScheme) so identical recolors share a row (a character's scheme decides which blocks it recolors).
		std::unique_ptr<Texture> _paletteTexture;
		std::int32_t _paletteDirtyFirstRow;
		std::int32_t _paletteDirtyLastRow;
		std::int32_t _paletteRowRefCount[PaletteCount];
		std::uint32_t _paletteRowColor[PaletteCount];
		std::uint8_t _paletteRowScheme[PaletteCount];
		HashMap<Reference<const String>, std::unique_ptr<Metadata>, 
#if defined(DEATH_TARGET_32BIT)
			xxHash32Func<String>,
#else
			xxHash64Func<String>,
#endif
			StringRefEqualTo> _cachedMetadata;
		HashMap<Pair<String, std::uint16_t>, std::unique_ptr<GenericGraphicResource>> _cachedGraphics;
#if defined(WITH_AUDIO)
		HashMap<String, std::unique_ptr<GenericSoundResource>> _cachedSounds;
#endif
		std::unique_ptr<UI::Font> _fonts[(std::int32_t)FontType::Count];
		std::unique_ptr<Shader> _precompiledShaders[(std::int32_t)PrecompiledShader::Count];
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		SmallVector<std::unique_ptr<PakFile>> _mountedPaks;
#endif
		Function<String(StringView)> _pathHandler;

#if defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT)
		String _contentPath;
#endif
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT)
		String _cachePath;
		String _sourcePath;
#endif
	};
}
