#pragma once

#include "../Main.h"
#include "GameDifficulty.h"
#include "LevelDescriptor.h"
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

namespace nCine
{
	class RenderCommand;
	class GLUniformBlockCache;
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

	/** @brief Manages loading of assets */
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
		/** @brief First palette row in the shared palette texture available for dynamic per-player allocation; lower
			rows are reserved for the sprite palette (row 0) and the generated gem palettes (rows 1-2) */
		static constexpr std::int32_t FirstDynamicPaletteRow = 8;
		/** @brief Invalid value */
		static constexpr std::int32_t InvalidValue = INT_MAX;

		/** @{ @name Player recolor palette sections

			The player's fur color is 4 bytes (one per section, as in the original game). The low 7 bits of each byte
			are the starting palette index of an 8-color gradient in the sprite palette; those 8 colors are copied into
			the section's fixed range in the per-player palette (a byte of 0 keeps the original colors for that section).
			If @ref FurHueShiftFlag is also set, the gradient's hue is rotated by @ref FurHueShiftDegrees first (see
			@ref ShiftHue), which roughly doubles the selectable color variants without extra gradients in the palette.
			@b TODO: @ref FurSectionStarts are best-guess placeholders - adjust them to the real player sprite palette. */
		/** @brief Number of recolorable fur sections */
		static constexpr std::int32_t FurSectionCount = 4;
		/** @brief Number of consecutive palette indices per fur section */
		static constexpr std::int32_t FurSectionSize = 8;
		/** @brief Starting palette index of each fur section in the per-player palette */
		static constexpr std::int32_t FurSectionStarts[FurSectionCount] = { 0x10, 0x18, 0x20, 0x28 };
		/** @brief High bit of a fur section byte requesting its gradient be hue-shifted by @ref FurHueShiftDegrees;
			the low 7 bits stay the gradient start index (gradient starts never exceed 0x58, so this bit is always free) */
		static constexpr std::uint8_t FurHueShiftFlag = 0x80;
		/** @brief Hue rotation (in degrees) applied to a gradient when @ref FurHueShiftFlag is set. The sprite palette's
			fur gradients are warm-heavy (red/orange/yellow) with few cyan/green/blue tones; ~150° spreads those warm
			gradients into the hues the palette lacks (rather than just their 180° complements, which would map several
			of them back onto existing hues), maximizing the variety of selectable colors while staying harmonious. */
		static constexpr float FurHueShiftDegrees = 150.0f;
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
		/** @brief Tries to open a file from the `"Source"` directory (original/custom game files, case-insensitive);
			the returned stream is invalid if the file wasn't found */
		std::unique_ptr<Stream> OpenSourceFile(StringView path, std::int32_t bufferSize = 8192);
		
		/** @brief Marks beginning of the loading assets */
		void BeginLoading();
		/** @brief Marks end of the loading assets */
		void EndLoading();

		/** @brief Overrides the default path handler */
		void OverridePathHandler(Function<String(StringView)>&& callback);

		/** @brief Preloads specified metadata and its linked assets to cache */
		void PreloadMetadataAsync(StringView path);
		/** @brief Loads specified metadata and its linked assets if not in cache already and returns it
			@param forceIndexed Load all linked graphics as indexed (palette not baked) so they can be recolored at
				draw time - used for the player so each player can have a custom color scheme */
		Metadata* RequestMetadata(StringView path, bool forceIndexed = false);
		/** @brief Loads specified graphics asset if not in cache already and returns it
			@param keepIndexed Keep raw palette indices in the texture (don't bake the palette) for shader recoloring */
		GenericGraphicResource* RequestGraphics(StringView path, std::uint16_t paletteOffset, bool keepIndexed = false);

		/** @brief Builds a 256-color palette for a player from a packed 4-byte fur color (one section per byte, each
			byte a gradient start in the sprite palette; 0 = original); see @ref FurSectionStarts */
		void BuildPlayerColorPalette(std::uint32_t furColor, std::uint32_t* outPalette) const;
		/** @brief Rotates the hue of a packed `0xAABBGGRR` color by `degrees` in YIQ space, preserving its perceived
			brightness (luma) and chroma - so the result keeps the original's lightness and saturation and only the
			hue changes (alpha is kept untouched). Used to derive the 180°-hue-shifted fur variants */
		static std::uint32_t ShiftHue(std::uint32_t color, float degrees);
		/** @brief Builds/updates a standalone 256x1 palette texture for a player fur color into `texture` (created on
			first use) and returns it; used for off-screen previews (e.g. the profile menu). Returns `nullptr` in
			headless mode. In-game recoloring uses @ref AcquirePaletteOffset instead. */
		Texture* ApplyPlayerColorPalette(std::unique_ptr<Texture>& texture, std::uint32_t furColor);
		/** @brief Returns the shared 256x256 palette texture (row 0 = sprite palette, rows 1-2 = gems, rows
			@ref FirstDynamicPaletteRow+ = dynamically allocated per-player palettes), uploading any rows changed
			since the last call; bound to texture unit 1 for palette shaders. Returns `nullptr` in headless mode. */
		Texture* GetPaletteTexture();
		/** @brief Returns the shared palette texture; indexed sprites that must not be recolored sample row 0 (the
			default sprite palette) by using a palette offset of 0. Returns `nullptr` in headless mode. */
		Texture* GetDefaultPaletteTexture();
		/** @brief Acquires a palette offset (the flat offset into the shared palette texture passed to
			@ref ActorBase::ActorRenderer::SetPalette and the palette-aware shaders) for the given packed fur color,
			reference-counted: callers with the same fur color share one palette (so e.g. many corpses of the same
			character cost a single row). Builds the recolored palette on first use. Returns the offset, or -1 if none
			are free. Release it with @ref ReleasePaletteOffset when the holder disconnects/despawns. */
		std::int32_t AcquirePaletteOffset(std::uint32_t furColor);
		/** @brief Releases one reference to a palette offset previously returned by @ref AcquirePaletteOffset; the
			underlying palette is returned to the pool only when the last holder releases it */
		void ReleasePaletteOffset(std::int32_t paletteOffset);

		/** @brief Configures a manually-built sprite render command for a (possibly indexed) sprite: selects the
			@ref PrecompiledShader::PaletteRemap shader when `indexed` (recolored at draw time via the shared palette),
			else the plain Sprite program; when the shader changes it also reserves uniform memory, sets triangle-strip
			draw params, and points the diffuse/palette samplers at units 0/1. Returns whether the shader changed (use
			it to gate one-time per-command setup). For drawing game-world graphics outside the standard ActorRenderer. */
		bool ConfigureSpriteShader(RenderCommand& command, bool indexed);
		/** @brief Binds `diffuse` to texture unit 0 and, when `indexed`, the shared palette texture to unit 1 plus the
			per-instance palette offset on `instanceBlock`. Call at draw time after the other instance uniforms. */
		void BindSpritePalette(RenderCommand& command, GLUniformBlockCache& instanceBlock, const Texture& diffuse, bool indexed, std::uint16_t paletteOffset);

		/** @brief Loads specified tile set and its palette */
		std::unique_ptr<Tiles::TileSet> RequestTileSet(StringView path, std::uint16_t captionTileId, bool applyPalette, const std::uint8_t* paletteRemapping = nullptr);
		/** @brief Returns `true` if specified level exists */
		bool LevelExists(StringView levelName);
		/** @brief Loads specified level into a level descriptor */
		bool TryLoadLevel(StringView path, GameDifficulty difficulty, LevelDescriptor& descriptor);
		/** @brief Loads default (sprite) palette */
		void ApplyDefaultPalette();
		/** @brief Overrides the active sprite palette (row 0 of the shared palette) with up to @ref ColorsPerPalette
			packed colors (`0xAABBGGRR`) and refreshes everything derived from it (gem gradients and per-player recolor
			rows) so the change takes effect on the next frame. Has no visible effect in headless mode. */
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
		static void ExpandTileDiffuse(std::uint8_t* pixelsOffset, std::uint32_t widthWithPadding);
		// Packs an indexed sprite/tile (palette index in the red/first channel) into the smallest texture format: R8
		// when alpha is on/off only (4x less VRAM than RGBA8), or RG8 keeping the per-pixel alpha in green (sampled
		// into .a via swizzle) when alpha is partial. `srcChannels` is the bytes-per-pixel of `pixels`: 1 (index
		// only) or 2 (index + alpha) are pre-packed and upload directly, 4 (RGBA) is scanned and repacked.
		// `paletteBaseTransparent` must be true if the sprite's palette entry 0 (its row base) is transparent - only
		// then can on/off transparency be dropped and reproduced by the palette (false e.g. for gems, which keep RG8)
		static std::unique_ptr<Texture> CreateIndexedTexture(const char* name, const std::uint8_t* pixels, std::int32_t width, std::int32_t height, std::int32_t srcChannels, bool paletteBaseTransparent);

		std::unique_ptr<Shader> CompileShader(const char* shaderName, Shader::DefaultVertex vertex, const char* fragment, Shader::Introspection introspection = Shader::Introspection::Enabled, std::initializer_list<StringView> defines = {});
		std::unique_ptr<Shader> CompileShader(const char* shaderName, const char* vertex, const char* fragment, Shader::Introspection introspection = Shader::Introspection::Enabled, std::initializer_list<StringView> defines = {});
		
		void RecreateGemPalettes();
		// Expands the dirty palette-row range so the next GetPaletteTexture re-uploads at least rows [firstRow, lastRow]
		void MarkPaletteDirty(std::int32_t firstRow, std::int32_t lastRow);
		// Rebuilds every in-use dynamic palette row from the current base palette (after the sprite palette changes)
		void RefreshDynamicPaletteRows();
#if defined(DEATH_DEBUG)
		void MigrateGraphics(StringView path);
#endif

		bool _isHeadless;
		bool _isLoading;
		std::uint32_t _palettes[PaletteCount * ColorsPerPalette];
		// Shared palette texture (256x256: one palette per row). Rows changed since the last upload are tracked by
		// the dirty range below and re-uploaded lazily. Dynamically allocated per-player rows are reference-counted
		// (_paletteRowRefCount > 0 = in use) and keyed by fur color (_paletteRowColor) so identical colors share a row.
		std::unique_ptr<Texture> _paletteTexture;
		std::int32_t _paletteDirtyFirstRow;
		std::int32_t _paletteDirtyLastRow;
		std::int32_t _paletteRowRefCount[PaletteCount];
		std::uint32_t _paletteRowColor[PaletteCount];
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
