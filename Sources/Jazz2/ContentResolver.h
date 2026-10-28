#pragma once

#include "ContentFileTypes.h"
#include "../Main.h"
#include "GameDifficulty.h"
#include "LevelDescriptor.h"
#include "PlayerType.h"
#include "Resources.h"
#include "UI/Font.h"

#include "../nCine/Audio/AudioBuffer.h"
#include "../nCine/Audio/AudioStreamPlayer.h"
#include "../nCine/Graphics/Texture.h"
#include "../nCine/Base/BitArray.h"
#include "../nCine/Base/HashMap.h"

#include <Containers/Function.h>
#include <Containers/Pair.h>
#include <Containers/Reference.h>
#include <Containers/SmallVector.h>
#include <Containers/StringView.h>
#include <IO/PakFile.h>
#include <IO/Stream.h>

#include "../nCine/Graphics/RHI/RhiFwd.h"

namespace ShaderCompiler
{
	struct Program;
}

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
#if defined(DEATH_TARGET_N64)
		// Eight fixed rows plus the dynamic recolors (see FirstDynamicPaletteRow): one player and a handful of
		// recolored enemies per level need a few, and every row costs 1 KB here and another 1 KB in the palette
		// texture the TLUTs are converted from - 256 rows were half a megabyte of the console's 8 MB
		static constexpr std::int32_t PaletteCount = 32;
#else
		static constexpr std::int32_t PaletteCount = 256;
#endif
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

		/** @} */

		/** @brief Returns static instance of main content resolver */
		static ContentResolver& Get();

		~ContentResolver();
		
		/** @brief Releases all cached assets */
		void Release();

#if defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_GAMECUBE) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Returns the directory the content was found in, if it is writable, otherwise an empty view
		 *
		 * Only the PlayStation 2 and the GameCube can answer anything but "no" here. Both run from a disc,
		 * where nothing can be written and settings have to go to a memory card, but both can equally be
		 * started from an SD card - in an MX4SIO adapter on the PlayStation 2, in an SD Gecko or an SD2SP2
		 * adapter on the GameCube - and then the directory the game is reading from is also the best place on
		 * the console to write to. @ref PreferencesCache prefers it over a memory card, which is what lets
		 * such a build save on a console with no memory card in it.
		 */
		StringView GetWritablePath() const;
#endif

		/** @brief Returns path to `"Content"` directory */
		StringView GetContentPath() const;
		/**
		 * @brief Returns path to the user-accessible `"Content"` directory, or an empty view if there is none
		 *
		 * On the platforms where `"Content"` is a part of the read-only application package (Android, Apple, AppImage
		 * and installed Linux packages, UWP, Switch, Vita), custom content can be placed into another `"Content"`
		 * directory next to `"Cache"` instead (next to `"Source"` on iOS, because `"Cache"` isn't reachable there).
		 * It's searched before everything else, so it can also replace any shipped file. Only assets streamed from
		 * a multiplayer server take precedence over it. The view is empty if the directory didn't exist at startup
		 * or if it's the packaged one.
		 */
		StringView GetUserContentPath() const;
		/**
		 * @brief Returns path to the `"Content"` directory files can be saved into, or an empty view if there is none
		 *
		 * It's the user `"Content"` directory where the platform has one (see @ref GetUserContentPath()) --- with
		 * @p create it's created if it doesn't exist yet and searched from then on --- and the `"Content"` directory
		 * itself elsewhere if it's writable, as it is in a portable copy of the game. The level editor saves levels
		 * and tile sets there. Nothing can be saved on the web, a file is downloaded instead.
		 */
		StringView GetWritableContentPath(bool create);
		/**
		 * @brief Returns full path to a file in the user `"Content"`, `"Content"` or `"Cache"` directory
		 *
		 * The directories are searched in this order, @p path is relative to them. Returns an empty string if the
		 * file wasn't found in any of them. Files in mounted `.pak` files aren't considered.
		 */
		String FindContentFile(StringView path) const;
		/** @brief Returns path to `"Cache"` directory */
		StringView GetCachePath() const;
		/** @brief Returns path to `"Source"` directory */
		StringView GetSourcePath() const;

		/** @brief Returns `true` if the application is running in headless mode (i.e., without any display) */
		bool IsHeadless() const;
		/** @brief Sets whether the application is running in headless mode */
		void SetHeadless(bool value);

		/**
		 * @brief Returns `true` if the `"Content"` directory was prepared ahead of time
		 *
		 * Such a tree is created by @ref asset-packer "AssetPacker" and already contains everything the
		 * first-run conversion would produce, so the game has nothing to convert, never looks for the
		 * original game files and leaves the `"Cache"` directory alone. It is recognized by the package
		 * the tool names @ref Compatibility::AssetConverter::PrebakedPackage.
		 */
		bool IsContentPrebaked() const;

#if defined(WITH_LIBRETRO)
		/** @brief Overrides `"Content"`, `"Source"` and `"Cache"` directories (used by the libretro core, which cannot rely on the working directory of the frontend process) */
		void OverridePaths(StringView contentPath, StringView sourcePath, StringView cachePath);
#endif

		/** @brief Scans the user `"Content"`, `"Content"` and `"Cache"` directories for `.pak` files and mounts them  */
		void RemountPaks();
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

		/**
			@brief Scope that marks a load, pairing @ref BeginLoading() with @ref EndLoading()

			The two do more than bracket the mark-and-sweep of the resource cache: they also tell the audio
			device that the thread feeding it is about to stop for longer than it queues ahead (see
			@relativeref{nCine,IAudioDevice::beginBlockingOperation()}). A load that gives up half way - a
			missing or corrupt level, which the game otherwise handles by returning to the menu - used to
			leave that unpaired, and on the PlayStation 2 the sound module stays closed once stopped, so the
			console went silent for the rest of the session. Held in a scope, every exit path ends the load.
		*/
		class LoadingScope
		{
		public:
			explicit LoadingScope(ContentResolver& resolver)
				: _resolver(resolver)
			{
				_resolver.BeginLoading();
			}

			~LoadingScope()
			{
				_resolver.EndLoading();
			}

			LoadingScope(const LoadingScope&) = delete;
			LoadingScope& operator=(const LoadingScope&) = delete;

		private:
			ContentResolver& _resolver;
		};

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
		 * @brief Keeps a metadata loaded with everything it uses, even across loadings that don't request it
		 *
		 * Everything not requested while something is loaded is released at the end of it (see @ref BeginLoading()).
		 * The level editor keeps the sprites of its previews this way while it loads a level to play it. Pins are
		 * counted, the metadata stays pinned until it was unpinned as many times as it was pinned.
		 */
		void PinMetadata(Metadata* metadata, bool pinned);
		/**
		 * @brief Keeps everything loaded across the loadings that follow, until @ref ReleaseLoadedResources()
		 *
		 * Nothing is released at the end of a loading while any is held, also what was loaded since, see
		 * @ref BeginLoading(). Calls are counted, each is paired with @ref ReleaseLoadedResources(). The level editor
		 * keeps the level played this way while it loads the level it shows, or another one to play.
		 */
		void RetainLoadedResources();
		/** @brief Lets the resources go again after @ref RetainLoadedResources(), they're released by the next loading that doesn't request them */
		void ReleaseLoadedResources();
		/**
		 * @brief Loads specified graphics asset (cached)
		 *
		 * @param path           Relative path to the graphics asset
		 * @param paletteOffset  Index of the first palette color used to bake the sprite (ignored when `keepIndexed`)
		 * @param keepIndexed    Keep raw palette indices in the texture (don't bake the palette) for shader recoloring
		 */
		GenericGraphicResource* RequestGraphics(StringView path, std::uint16_t paletteOffset, bool keepIndexed = false);
		/**
		 * @brief Returns whether graphics @ref RequestGraphics() gave are still loaded, @ref EndLoading() frees them once nothing requested them
		 *
		 * The graphics aren't touched, so it can be asked of graphics that may be gone already. The arguments are those
		 * of @ref RequestGraphics() (the palette offset of graphics kept indexed doesn't matter).
		 */
		bool IsGraphicsCached(const GenericGraphicResource* graphics, StringView path, std::uint16_t paletteOffset, bool keepIndexed = false) const;
		/**
		 * @brief Returns the path loaded graphics were requested by (see @ref RequestGraphics()), or an empty string
		 *
		 * The separators are those of the platform. Every loaded graphics is looked at, so it's for a caller that keeps
		 * what it found.
		 */
		String FindGraphicsPath(const GenericGraphicResource* graphics) const;
		/**
		 * @brief Loads the graphics of a deferred animation entry and returns whether they are ready to be drawn
		 *
		 * Called by @ref Metadata::FindAnimation() the first time a deferred animation (see @ref metadata-deferred)
		 * is looked up --- there is no reason to call it directly. An entry whose graphics cannot be loaded is
		 * marked so that the attempt is not repeated on the next lookup.
		 */
		bool ResolveAnimation(Metadata& metadata, GraphicResource& animation);
		/**
			@brief Reads deferred animations in ahead of their first use while the heap has room for them

			Called once a level has finished loading, from behind its loading screen. Only the platforms that
			defer animations do anything here; see the implementation for what gets read and when it stops.
		*/
		void PreloadDeferredAnimations();

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
		/** @brief Returns a number that changes whenever the sprite palette (the first row of the palette texture) changes */
		std::uint32_t GetSpritePaletteRevision() const {
			return _spritePaletteRevision;
		}
		/**
		 * @brief Declares the channel order of a texture whose texels are palette entries
		 *
		 * A palette entry is a `std::uint32_t` VALUE with red in the lowest byte, and such an array is uploaded by
		 * reinterpreting it as bytes - which is R,G,B,A only on a little-endian host. Any texture filled that way has
		 * to say so, so that backends which SAMPLE it read the channels back correctly; see the definition for why the
		 * bytes are described rather than reordered. Public because the cinematics player builds its own palette the
		 * same way (@ref UI::Cinematics).
		 */
		static void ConfigurePaletteTextureChannels(Texture& texture);
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
		 * @brief Acquires a reference-counted palette offset of the sprite palette shifted by @p shift entries
		 *
		 * Index `i` of the row is the color of index `(i + shift) & 255` of the sprite palette, index 0 stays the
		 * transparent one, and a pixel moved onto index 0 gets its color fully opaque, as `SPRITE::PALSHIFT` of JJ2+
		 * draws it (see @ref Tiles::LayerRendererType::PaletteShift). The row is built again whenever the sprite
		 * palette changes. Callers with the same shift share one row. Returns 0 (the sprite palette itself) for a
		 * shift of 0 (unless @p premultiplied), otherwise the offset, or -1 if no row is free. Release it with
		 * @ref ReleasePaletteOffset. With @p premultiplied the colors of the row are premultiplied by their alpha,
		 * as a layer that is screened or multiplied is drawn through (see @ref Tiles::LayerBlend), a row of its own.
		 */
		std::int32_t AcquireShiftedPaletteOffset(std::uint8_t shift, bool premultiplied = false);
		/**
		 * @brief Acquires a reference-counted palette offset of the sprite palette in a single hue of it
		 *
		 * Index `i` of the row is the color of index `gradientStart + (i & 7)` of the sprite palette, the gradient of 8
		 * entries from @p gradientStart in place of the one of each index, as `SPRITE::SINGLEHUE` of JJ2+ draws it. Index 0
		 * stays the transparent one. The row is built again whenever the sprite palette changes. Callers with the same
		 * @p gradientStart share one row. Returns the offset, or -1 if no row is free. Release it with @ref ReleasePaletteOffset.
		 */
		std::int32_t AcquireSingleHuePaletteOffset(std::uint8_t gradientStart);
		/**
		 * @brief Acquires a reference-counted palette offset of the sprite palette as a gem of the given color
		 *
		 * Index `i` of the row is entry `i & 127` of the gradient of the gem (see @ref GetGemColor()), as `SPRITE::GEM`
		 * of JJ2+ draws it - its translucency is up to the caller. Index 0 stays the transparent one. The row is built
		 * again whenever the sprite palette changes. Callers with the same @p gem share one row. Returns the offset, or
		 * -1 if no row is free. Release it with @ref ReleasePaletteOffset.
		 */
		std::int32_t AcquireGemPaletteOffset(std::uint8_t gem);
		/**
		 * @brief Acquires a reference-counted palette offset of a single color of the sprite palette, its opacity by the index
		 *
		 * Index `i` of the row is the color of index @p color of the sprite palette with the alpha of `i`, as
		 * `SPRITE::ALPHAMAP` of JJ2+ draws it. The row is built again whenever the sprite palette changes. Callers with the
		 * same @p color share one row. Returns the offset, or -1 if no row is free. Release it with @ref ReleasePaletteOffset.
		 * With @p premultiplied the colors of the row are premultiplied by their alpha, as a layer that is screened or
		 * multiplied is drawn through (see @ref Tiles::LayerBlend), a row of its own.
		 */
		std::int32_t AcquireAlphaMapPaletteOffset(std::uint8_t color, bool premultiplied = false);
		/**
		 * @brief Fills 256 colors as the row of @ref AcquireShiftedPaletteOffset() or, with @p alphaMap, of
		 *		@ref AcquireAlphaMapPaletteOffset() is, without taking a row
		 */
		void BuildLayerPalette(bool alphaMap, std::uint8_t param, std::uint32_t* outPalette) const;
		/**
		 * @brief Acquires a reference-counted palette offset of a color of the sprite palette for indices below a density
		 *
		 * Index `i` of the row is the color of index @p color of the sprite palette if `i` is less than @p density, transparent
		 * otherwise, so a texture of noise drawn through it shows as many pixels as `SPRITE::BLEND_DISSOLVE` of JJ2+. The row is
		 * built again whenever the sprite palette changes. Returns the offset, or -1 if no row is free. Release it with
		 * @ref ReleasePaletteOffset.
		 */
		std::int32_t AcquireDissolvePaletteOffset(std::uint8_t color, std::uint8_t density);
		/**
		 * @brief Acquires a reference-counted palette offset of the brightness of the sprite palette in color channels
		 *
		 * Index `i` of the row is a half of the brightness JJ2 has for index `i` of the sprite palette in the channels that
		 * @p channels picks (0 red, 1 green, 2 blue, 3 red and blue, 4 red and green, 5 green and blue, others all three),
		 * black in the others, so drawn additively it's `SPRITE::NEONGLOW` of JJ2+. The row is built again whenever the
		 * sprite palette changes. Returns the offset, or -1 if no row is free. Release it with @ref ReleasePaletteOffset.
		 */
		std::int32_t AcquireNeonGlowPaletteOffset(std::uint8_t channels);
		/**
		 * @brief Acquires a palette offset of a row of its own colors
		 *
		 * The row is the 256 colors of @p colors (packed as `0xAABBGGRR`) with index 0 transparent, and it stays so
		 * whatever happens to the sprite palette, e.g. an additional palette of a level (see
		 * @ref Tiles::LayerRendererType::PaletteMapping). Each call takes a row of its own. Returns the offset, or -1 if
		 * no row is free. Release it with @ref ReleasePaletteOffset. With @p premultiplied the colors of the row are
		 * premultiplied by their alpha, as a layer that is screened or multiplied is drawn through (see @ref Tiles::LayerBlend).
		 */
		std::int32_t AcquireFixedPaletteOffset(const std::uint32_t* colors, bool premultiplied = false);
		/**
		 * @brief Returns entry @p index of the gradient of a gem as `SPRITE::GEM` of JJ2+ draws it
		 *
		 * Gems 0 to 3 are the red, green, blue and purple gems of the game (the gradients the gems are drawn with),
		 * up to 12 are the other gradients JJ2+ shows for them, the others have none (returns a transparent color).
		 * Only the lower 7 bits of @p index are used. The color is packed as `0xAABBGGRR`.
		 */
		std::uint32_t GetGemColor(std::uint8_t gem, std::uint8_t index) const;
		/**
		 * @brief Releases one reference to a palette offset from @ref AcquirePaletteOffset and the like
		 *
		 * Also of @ref AcquireShiftedPaletteOffset, @ref AcquireSingleHuePaletteOffset and @ref AcquireGemPaletteOffset.
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
		 * per-instance palette offset (resolved through the command's cached instance uniforms). Call at draw
		 * time, after the other instance uniforms.
		 */
		void BindSpritePalette(RenderCommand& command, const Texture& diffuse, bool indexed, std::uint16_t paletteOffset);

		/**
		 * @brief Loads specified tile set and its palette
		 *
		 * @param path              Relative path to the tile set
		 * @param captionTileId     Tile used to render the level-preview caption thumbnail
		 * @param applyPalette      Apply the tile set's palette to the live sprite palette
		 * @param paletteRemapping  Optional 256-entry table remapping each tile's palette indices
		 * @param usedTiles         Optional per-tile bits selecting which tiles go into the diffuse atlas
		 * @param withDiffuse       If `false`, only the collision masks are loaded and the tile set has no
		 *                          textures --- for a caller that requests it again once it knows which tiles to
		 *                          pack (see @ref Tiles::TileMap::PruneTilesetAtlas())
		 * @param trueColorPalette  Optional 256 colors the 8-bit tiles are drawn in as true color tiles, so they
		 *                          don't follow the live palette (as `jjTilesFromTileset32()` of JJ2+)
		 */
		std::unique_ptr<Tiles::TileSet> RequestTileSet(StringView path, std::uint16_t captionTileId, bool applyPalette, const std::uint8_t* paletteRemapping = nullptr,
			const BitArray* usedTiles = nullptr, bool withDiffuse = true, const std::uint32_t* trueColorPalette = nullptr);
		/**
		 * @brief Loads a tile set from a stream
		 *
		 * The same as the overload above, for a tile set that isn't a file in `Tilesets/` --- the tile set editor
		 * hands over the one it edits this way. The @p path is what the tile set is known as.
		 */
		std::unique_ptr<Tiles::TileSet> RequestTileSet(Stream& s, StringView path, std::uint16_t captionTileId, bool applyPalette,
			const std::uint8_t* paletteRemapping = nullptr, const BitArray* usedTiles = nullptr, bool withDiffuse = true,
			const std::uint32_t* trueColorPalette = nullptr);
		/** @brief Creates a tile set of transparent tiles without collision, palette-indexed if @p indexed, for images saved into them later */
		std::unique_ptr<Tiles::TileSet> CreateBlankTileSet(std::uint16_t tileCount, bool indexed);
		/**
		 * @brief Opens the file of a tile set, found the same way as by @ref RequestTileSet()
		 *
		 * For a caller that needs what the file stores rather than a tile set made of it, see @ref Formats::TileSetFile.
		 * Returns `nullptr` if there is no such tile set.
		 */
		std::unique_ptr<Stream> OpenTileSetFile(StringView path);
		/** @brief Returns `true` if specified level exists */
		bool LevelExists(StringView levelName);
		/**
		 * @brief Returns the full name of a level that a server configuration or an admin command names
		 *
		 * A name without an episode (`battle1`) is a level of no episode, a name ending with a slash (`prince/`) is
		 * the first level of that episode. If there's no such episode, the name is returned as it is, so it can be
		 * reported.
		 */
		String ResolveLevelName(StringView name);
		/**
		 * @brief Loads specified level into a level descriptor
		 *
		 * The level is looked up as `Episodes/<path>.j2l` in the user content, the content and the cache, in that order.
		 */
		bool TryLoadLevel(StringView path, const LevelLoadOptions& options, LevelDescriptor& descriptor);
		/**
		 * @brief Loads a level from a stream into a level descriptor
		 *
		 * The @p fullPath is what the level is known as, the level script is looked up next to it.
		 */
		bool TryLoadLevel(Stream& s, StringView fullPath, const LevelLoadOptions& options, LevelDescriptor& descriptor);
		/** @brief Reads only the name of the next level from a given level file, without loading the level */
		bool TryGetNextLevelName(StringView path, String& nextLevel);
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
		/**
		 * @brief Overrides the pixels of a tile of a tile map by palette indices of a level, 32 × 32 of them
		 *
		 * An indexed tile set keeps the indices, so the tile follows the palette as it changes, otherwise the colors of
		 * the current palette are baked in.
		 */
		void ApplyTileDiffuseOverride(Tiles::TileMap& tileMap, std::int32_t tileId, const std::uint8_t* indices);
		/**
		 * @brief Overrides the pixels of a tile of a tile map by colors, 32 × 32 of them, 4 bytes RGBA each
		 *
		 * Only a tile of a tile set that isn't indexed can take them, returns `false` otherwise.
		 */
		bool ApplyTileDiffuseOverrideRgba(Tiles::TileMap& tileMap, std::int32_t tileId, const std::uint8_t* rgba);

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
#if defined(TILEMAP_LAYER_EFFECTS) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
			@brief Compiles the shaders of the special tile layers, if they aren't compiled yet

			Called once a level that has an x-ray, a blur-behind or a reflection layer is loaded, so a game that never
			plays one never spends the startup time or the memory on them. Returns `false` if they couldn't be
			compiled, the layers are then drawn as on the backends that can't show the effects. The shader of the
			reflection layers isn't required, without it only they aren't drawn.
		*/
		bool CompileLayerEffectShaders();
#endif
		/** @brief Returns a noise texture with random pixels */
		std::unique_ptr<Texture> GetNoiseTexture();

		/**
			@brief Loads the samples of @p sound if they are not loaded yet

			Sounds are described by the metadata but, on the platforms that defer them, their samples are only
			read when something first plays them and are given up again at the next load boundary.
			Returns whether the sound has any sample to play; one whose samples cannot be read is marked so the
			read is not retried on every play.
		*/
		bool ResolveSound(Resources::SoundResource& sound);
#if defined(NCINE_HAS_NATIVE_AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
			@brief Returns the console's own copy of a sound sample (relative to `Animations`), or an empty string

			The content prepared for the Nintendo 64 keeps every sound effect as a `.wav64` file next to the pak
			instead of inside it, and the audio device streams it from the cartridge while it plays, so a sound
			costs no RAM at all (see @ref nCine::N64AudioDevice). It is the only form the console plays: nothing
			is decoded there, so a sample without its `.wav64` stays silent.
		*/
		String GetNativeSoundPath(StringView assetPath) const;
#endif

		/** @brief Returns currently loaded set of palettes */
		StaticArrayView<PaletteCount * ColorsPerPalette, const std::uint32_t> GetPalettes() const {
			return _palettes;
		}

		/**
			@brief Sprite sheet of a graphics asset (`.aura`) as it's stored, see @ref ReadSpriteSheet()

			The fields have the meaning of the same fields of @ref GenericGraphicResource, the frames are laid out the
			same way. The pixels keep the channels of the file - 1 (palette index), 2 (palette index and alpha) or 4
			(RGBA).
		*/
		struct SpriteSheet
		{
			/** @brief Size of the sheet in pixels */
			Vector2i Size;
			/** @brief Bytes per pixel of @ref Pixels */
			std::int32_t ChannelCount;
			/** @brief Whether the pixels are palette indices rather than colors */
			bool IsIndexed;
			/** @brief Size of the logical cell of a frame */
			Vector2i FrameDimensions;
			/** @brief Columns and rows of the regular grid */
			Vector2i FrameConfiguration;
			/** @brief Number of frames */
			std::int32_t FrameCount;
			/** @brief Animation duration (in normalized frames) */
			float AnimDuration;
			/** @brief Hotspot */
			Vector2i Hotspot;
			/** @brief Coldspot, @ref InvalidValue if there's none */
			Vector2i Coldspot;
			/** @brief Gunspot, @ref InvalidValue if there's none */
			Vector2i Gunspot;
			/** @brief Where each frame sits in a tightly packed sheet, empty for a regular grid */
			SmallVector<Resources::FrameRect, 0> FrameRects;
			/** @brief Pixels, @ref ChannelCount bytes each, rows packed continuously */
			std::unique_ptr<std::uint8_t[]> Pixels;
		};

		/**
			@brief Reads a sprite sheet (`.aura`, relative to `Animations`) into memory without making a texture of it

			For what needs the pixels themselves, the level scripts edit the sprites this way. Without @p withPixels
			only the header is read and @ref SpriteSheet::Pixels stays empty. Returns `false` if there's no such sprite
			sheet or it can't be read.
		*/
		bool ReadSpriteSheet(StringView path, SpriteSheet& sheet, bool withPixels = true);

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
		// Looks for the package that marks a prepared content tree, has to run again if the paths change
		void DetectPrebakedContent();
		// Looks for the user-accessible "Content" directory (see GetUserContentPath()), has to run again if the paths change
		void DetectUserContent();
		// Returns where the user-accessible "Content" directory is or would be, empty where the platform has none
		String GetUserContentCandidate() const;
		// Opens the file FindContentFile() would find
		std::unique_ptr<Stream> OpenFoundContentFile(StringView path, String& fullPath, std::int32_t bufferSize) const;

		// Cache key offset for indexed graphics (palette indices kept in the texture instead of baked), distinct
		// from any real paletteOffset so indexed and baked variants of the same sprite are cached separately
		static constexpr std::uint16_t IndexedGraphicsCacheKey = UINT16_MAX;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct AuraHeader
		{
			std::uint8_t Flags;
			std::uint8_t ChannelCount;
			Vector2i FrameDimensions;
			Vector2i FrameConfiguration;
			std::uint16_t FrameCount;
			std::uint16_t AnimDuration;
			std::uint16_t Hotspot[2];
			std::uint16_t Coldspot[2];
			std::uint16_t Gunspot[2];
			SmallVector<Resources::FrameRect, 0> FrameRects;
			// Size of the image that follows the header
			std::uint32_t Width;
			std::uint32_t Height;
		};
#endif

		GenericGraphicResource* RequestGraphicsAura(StringView path, std::uint16_t paletteOffset, bool keepIndexed = false);
		// Reads just the frame size out of a sheet's header, for a deferred metadata's bounding box
		bool ReadAuraFrameDimensions(StringView path, Vector2i& frameDimensions);
		// Reads the header of a sheet up to its image, returns `false` if it isn't a sprite sheet
		static bool ReadAuraHeader(Stream& s, AuraHeader& header);
		// `contentEndsStream` states that the image is the last thing in the stream, which lets it be read into
		// memory in one go; pass `false` when anything follows it, so the position is left right after the image.
		// `lz4` says the content is one LZ4 block (see JJ2Anims::ImageContentLz4Flag); returns `false` if it could
		// not be decoded, in which case `data` holds nothing usable
		static bool ReadImageFromFile(std::unique_ptr<Stream>& s, std::uint8_t* data, std::int32_t width, std::int32_t height,
			std::int32_t channelCount, bool contentEndsStream = true, bool lz4 = false);
		// Copies a tile's edge pixels into its 1px atlas padding (so sampling never bleeds across tiles); `bytesPerPixel`
		// is 1 for an indexed (R8) atlas or 4 for a baked RGBA atlas
		static void ExpandTileDiffuse(std::uint8_t* pixelsOffset, std::uint32_t widthWithPadding, std::uint32_t bytesPerPixel);
		// Marks a metadata and the graphics and sounds it points at as referenced, so EndLoading() keeps them
		static void MarkReferenced(Metadata& metadata);

		// Reads the tileset's 256-color palette from the decompressed stream and applies it to the live sprite palette
		// (drops baked fonts, regenerates gem palettes); just skips the bytes when headless or `applyPalette` is false
		void ReadTilesetPalette(Stream& uc, bool applyPalette);
		// Reads the tileset's packed collision mask from the decompressed stream (kept in the cache's 1-bit-per-pixel
		// form, see TileSet::MaskBytesPerTile); it always has the size of `tileCount` masks, whatever the file says,
		// which `maskSize` receives in bytes
		static std::unique_ptr<std::uint8_t[]> ReadTilesetMask(Stream& uc, std::uint16_t tileCount, std::uint32_t& maskSize);
		// Builds the padded tileset diffuse atlas (R8 indexed for all-8-bit tilesets, RGBA8 baked when any tile is
		// 32-bit), the per-tile fully-opaque flags and the optional caption thumbnail. Reads pixels from the raw stream
		// `s` (the image content follows the compressed block). Sets `indexTiles` to the chosen mode.
		// Returns one texture normally, or several consecutive row-band chunks when the atlas exceeds the
		// device's texture-size limit (console targets); see TileSet::ResolveTextureDiffuse
		SmallVector<std::unique_ptr<Texture>, 1> BuildTilesetDiffuse(Stream& s, const char* name, std::uint8_t channelCount,
			bool lz4Content, const BitArray* usedTiles, std::unique_ptr<std::uint16_t[]>& atlasSlot,
			std::uint32_t width, std::uint32_t height, std::uint16_t tileCount, const std::uint8_t* is32bitTile,
			const std::uint8_t* paletteRemapping, const std::uint32_t* trueColorPalette, std::uint16_t captionTileId, bool& indexTiles,
			std::unique_ptr<std::uint8_t[]>& tileDiffuseOpaque, std::unique_ptr<std::uint8_t[]>& tileEffectInfo, std::unique_ptr<Color[]>& captionTile);
		// Packs an indexed sprite/tile (palette index in the red/first channel) into the smallest texture format: R8
		// when alpha is on/off only (4x less VRAM than RGBA8), or RG8 keeping the per-pixel alpha in green (sampled
		// into .a via swizzle) when alpha is partial. `srcChannels` is the bytes-per-pixel of `pixels`: 1 (index
		// only) or 2 (index + alpha) are pre-packed and upload directly with no scan; 4 (RGBA, e.g., a user-supplied PNG)
		// packs to RG8. Indexed tilesets are handed over as a single index channel by RequestTileSet, taking the R8 path.
		// `paletteBaseTransparent` must be true if the sprite's palette entry 0 (its row base) is transparent - only
		// then can on/off transparency be dropped and reproduced by the palette (false, e.g., for gems, which keep RG8)
		static std::unique_ptr<Texture> CreateIndexedTexture(const char* name, const std::uint8_t* pixels, std::int32_t width, std::int32_t height, std::int32_t srcChannels, bool paletteBaseTransparent);

		// Compiles one variant of a ShaderCompiler-generated program. Batched shaders get their batch size from
		// the offline-reflected std140 instance stride, so no probe compilation is needed anymore.
		// Pass nullptr (the default) as variantName for the unnamed base variant (program.Variants[0]).
		std::unique_ptr<Shader> CompileShader(const char* shaderName, const ShaderCompiler::Program& program, const char* variantName = nullptr, Shader::Introspection introspection = Shader::Introspection::Enabled);
		// Convenience overload - compiles the base variant with explicit introspection
		std::unique_ptr<Shader> CompileShader(const char* shaderName, const ShaderCompiler::Program& program, Shader::Introspection introspection) {
			return CompileShader(shaderName, program, nullptr, introspection);
		}
		
		void RecreateGemPalettes();
		// Expands the dirty palette-row range so the next GetPaletteTexture re-uploads at least rows [firstRow, lastRow]
		void MarkPaletteDirty(std::int32_t firstRow, std::int32_t lastRow);
		// Rebuilds every in-use dynamic palette row from the current base palette (after the sprite palette changes)
		void RefreshDynamicPaletteRows();
		// Maps a player type to its fur recolor scheme index (which destination blocks each section recolors)
		static std::int32_t GetFurSchemeIndex(PlayerType playerType);
		// Core recolor builder, keyed by the scheme index stored on each dynamic palette row
		void BuildPlayerColorPaletteForScheme(std::uint32_t furColor, std::int32_t schemeIndex, std::uint32_t* outPalette) const;
		// Builds the sprite palette shifted by the given number of entries, see AcquireShiftedPaletteOffset()
		void BuildShiftedPalette(std::uint8_t shift, std::uint32_t* outPalette) const;
		// Builds the sprite palette in the gradient of the given start, see AcquireSingleHuePaletteOffset()
		void BuildSingleHuePalette(std::uint8_t gradientStart, std::uint32_t* outPalette) const;
		// Builds the sprite palette as a gem of the given color, see AcquireGemPaletteOffset()
		void BuildGemPalette(std::uint8_t gem, std::uint32_t* outPalette) const;
		// Builds a single color of the sprite palette with the alpha of each index, see AcquireAlphaMapPaletteOffset()
		void BuildAlphaMapPalette(std::uint8_t color, std::uint32_t* outPalette) const;
		// Builds a color of the sprite palette for the indices below a density, see AcquireDissolvePaletteOffset()
		void BuildDissolvePalette(std::uint32_t value, std::uint32_t* outPalette) const;
		// Builds the brightness of the sprite palette in color channels, see AcquireNeonGlowPaletteOffset()
		void BuildNeonGlowPalette(std::uint8_t channels, std::uint32_t* outPalette) const;
		// Builds the gradient of 128 entries a gem is drawn with from its 5 stops in the sprite palette, 32 entries each
		void BuildGemGradient(const std::uint8_t* stops, std::uint32_t* outGradient) const;
		// Premultiplies the colors of a row by their alpha, see AcquireShiftedPaletteOffset()
		static void PremultiplyPalette(std::uint32_t* palette);
		// Returns a dynamic palette row derived from the sprite palette by a scheme of its own and a value, see AcquireShiftedPaletteOffset()
		std::int32_t AcquireDerivedPaletteOffset(std::uint8_t scheme, std::uint32_t value);
#if defined(DEATH_DEBUG)
		void MigrateGraphics(StringView path);
#endif

		bool _isHeadless;
		bool _isLoading;
		bool _isContentPrebaked;
		std::uint32_t _palettes[PaletteCount * ColorsPerPalette];
		// Shared palette texture (256x256: one palette per row). Rows changed since the last upload are tracked by
		// the dirty range below and re-uploaded lazily. Dynamically allocated per-player rows are reference-counted
		// (_paletteRowRefCount > 0 = in use) and keyed by both fur color (_paletteRowColor) and recolor scheme
		// (_paletteRowScheme) so identical recolors share a row (a character's scheme decides which blocks it recolors).
		// A row of the shifted sprite palette has ShiftedPaletteScheme as its scheme and the shift as its color, a row of
		// the sprite palette in a single hue SingleHuePaletteScheme and the start of the gradient.
		std::unique_ptr<Texture> _paletteTexture;
		std::int32_t _paletteDirtyFirstRow;
		std::uint32_t _spritePaletteRevision = 0;
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
		// How many times each metadata is pinned, see PinMetadata(), and how many retains are held, see RetainLoadedResources()
		SmallVector<Pair<Metadata*, std::int32_t>, 0> _pinnedMetadata;
		std::int32_t _retainedResources = 0;
		std::unique_ptr<UI::Font> _fonts[(std::int32_t)FontType::Count];
		std::unique_ptr<Shader> _precompiledShaders[(std::int32_t)PrecompiledShader::Count];
		SmallVector<std::unique_ptr<PakFile>> _mountedPaks;
		// Number of leading entries in _mountedPaks that come from the user "Content" directory
		std::size_t _userPakCount = 0;
		Function<String(StringView)> _pathHandler;

		/** @brief Drops the cached fonts whose atlas baked the palette, leaving the palette-indexed ones alone */
		void DropBakedFonts();

#if defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_IOS) || defined(DEATH_TARGET_WINDOWS_RT) || defined(DEATH_TARGET_PS2) || \
		defined(DEATH_TARGET_GAMECUBE)
		String _contentPath;
#endif
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT) || \
		defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_GAMECUBE)
		String _cachePath;
		String _sourcePath;
#endif
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT) || \
		defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_VITA)
		// Set only when the directory exists - see GetUserContentPath()
		String _userContentPath;
#endif
#if defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_GAMECUBE)
		/** @brief Set only when the content was found on writable storage - see @ref GetWritablePath() */
		String _writablePath;
#endif
	};
}
