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
		/** @brief Invalid value */
		static constexpr std::int32_t InvalidValue = INT_MAX;

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
		std::unique_ptr<Stream> OpenContentFile(StringView path);
		
		/** @brief Marks beginning of the loading assets */
		void BeginLoading();
		/** @brief Marks end of the loading assets */
		void EndLoading();

		/** @brief Overrides the default path handler */
		void OverridePathHandler(Function<String(StringView)>&& callback);

		/** @brief Preloads specified metadata and its linked assets to cache */
		void PreloadMetadataAsync(StringView path);
		/** @brief Loads specified metadata and its linked assets if not in cache already and returns it */
		Metadata* RequestMetadata(StringView path);
		/** @brief Loads specified graphics asset if not in cache already and returns it */
		GenericGraphicResource* RequestGraphics(StringView path, std::uint16_t paletteOffset);

		/** @brief Loads specified tile set and its palette */
		std::unique_ptr<Tiles::TileSet> RequestTileSet(StringView path, std::uint16_t captionTileId, bool applyPalette, const std::uint8_t* paletteRemapping = nullptr);
		/** @brief Returns `true` if specified level exists */
		bool LevelExists(StringView levelName);
		/** @brief Loads specified level into a level descriptor */
		bool TryLoadLevel(StringView path, GameDifficulty difficulty, LevelDescriptor& descriptor);
		/** @brief Loads default (sprite) palette */
		void ApplyDefaultPalette();

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

		GenericGraphicResource* RequestGraphicsAura(StringView path, std::uint16_t paletteOffset);
		static void ReadImageFromFile(std::unique_ptr<Stream>& s, std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount);
		static void ExpandTileDiffuse(std::uint8_t* pixelsOffset, std::uint32_t widthWithPadding);

		std::unique_ptr<Shader> CompileShader(const char* shaderName, Shader::DefaultVertex vertex, const char* fragment, Shader::Introspection introspection = Shader::Introspection::Enabled, std::initializer_list<StringView> defines = {});
		std::unique_ptr<Shader> CompileShader(const char* shaderName, const char* vertex, const char* fragment, Shader::Introspection introspection = Shader::Introspection::Enabled, std::initializer_list<StringView> defines = {});
		
		void RecreateGemPalettes();
#if defined(DEATH_DEBUG)
		void MigrateGraphics(StringView path);
#endif

		bool _isHeadless;
		bool _isLoading;
		std::uint32_t _palettes[PaletteCount * ColorsPerPalette];
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
