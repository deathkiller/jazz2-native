#pragma once

#include "../Main.h"
#include "AnimState.h"
#include "GameDifficulty.h"
#include "LevelDescriptor.h"
#include "Resources.h"
#include "WeaponType.h"
#include "UI/Font.h"

#include "../nCine/Audio/AudioBuffer.h"
#include "../nCine/Audio/AudioStreamPlayer.h"
#include "../nCine/Graphics/Camera.h"
#include "../nCine/Graphics/Sprite.h"
#include "../nCine/Graphics/Texture.h"
#include "../nCine/Graphics/Viewport.h"
#include "../nCine/Base/HashMap.h"

#include <Containers/Pair.h>
#include <Containers/Reference.h>
#include <Containers/SmallVector.h>
#include <Containers/StringView.h>
#include <IO/FileSystem.h>
#include <IO/PakFile.h>
#include <IO/Stream.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
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
		static constexpr std::uint8_t LevelFile = 1;
		static constexpr std::uint8_t EpisodeFile = 2;
		static constexpr std::uint8_t CacheIndexFile = 3;
		static constexpr std::uint8_t ConfigFile = 4;
		static constexpr std::uint8_t StateFile = 5;
		static constexpr std::uint8_t SfxListFile = 6;
		static constexpr std::uint8_t HighscoresFile = 7;

		static constexpr std::int32_t PaletteCount = 256;
		static constexpr std::int32_t ColorsPerPalette = 256;
		static constexpr std::int32_t InvalidValue = INT_MAX;

		static ContentResolver& Get();

		~ContentResolver();
		
		void Release();

		StringView GetContentPath() const;
		StringView GetCachePath() const;
		StringView GetSourcePath() const;

		bool IsHeadless() const;
		void SetHeadless(bool value);

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		void RemountPaks();
#endif
		std::unique_ptr<Stream> OpenContentFile(StringView path);

		void BeginLoading();
		void EndLoading();

		void PreloadMetadataAsync(StringView path);
		Metadata* RequestMetadata(StringView path);
		GenericGraphicResource* RequestGraphics(StringView path, std::uint16_t paletteOffset);

		std::unique_ptr<Tiles::TileSet> RequestTileSet(StringView path, std::uint16_t captionTileId, bool applyPalette, const std::uint8_t* paletteRemapping = nullptr);
		bool LevelExists(StringView episodeName, StringView levelName);
		bool TryLoadLevel(StringView path, GameDifficulty difficulty, LevelDescriptor& descriptor);
		void ApplyDefaultPalette();

		std::optional<Episode> GetEpisode(StringView name, bool withImages = false);
		std::optional<Episode> GetEpisodeByPath(StringView path, bool withImages = false);
		std::unique_ptr<AudioStreamPlayer> GetMusic(StringView path);
		UI::Font* GetFont(FontType fontType);
		Shader* GetShader(PrecompiledShader shader);
		void CompileShaders();
		static std::unique_ptr<Texture> GetNoiseTexture();

		const std::uint32_t* GetPalettes() const {
			return _palettes;
		}

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct StringRefEqualTo
		{
			inline bool operator()(const Reference<String>& a, const Reference<String>& b) const noexcept {
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
		
		std::unique_ptr<Shader> CompileShader(const char* shaderName, Shader::DefaultVertex vertex, const char* fragment, Shader::Introspection introspection = Shader::Introspection::Enabled, std::initializer_list<StringView> defines = {});
		std::unique_ptr<Shader> CompileShader(const char* shaderName, const char* vertex, const char* fragment, Shader::Introspection introspection = Shader::Introspection::Enabled, std::initializer_list<StringView> defines = {});
		
		void RecreateGemPalettes();
#if defined(DEATH_DEBUG)
		void MigrateGraphics(StringView path);
#endif

		bool _isHeadless;
		bool _isLoading;
		std::uint32_t _palettes[PaletteCount * ColorsPerPalette];
		HashMap<Reference<String>, std::unique_ptr<Metadata>, FNV1aHashFunc<String>, StringRefEqualTo> _cachedMetadata;
		HashMap<Pair<String, std::uint16_t>, std::unique_ptr<GenericGraphicResource>> _cachedGraphics;
#if defined(WITH_AUDIO)
		HashMap<String, std::unique_ptr<GenericSoundResource>> _cachedSounds;
#endif
		std::unique_ptr<UI::Font> _fonts[(int32_t)FontType::Count];
		std::unique_ptr<Shader> _precompiledShaders[(int32_t)PrecompiledShader::Count];
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		SmallVector<std::unique_ptr<PakFile>> _mountedPaks;
#endif

#if defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT)
		String _contentPath;
#endif
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT)
		String _cachePath;
		String _sourcePath;
#endif
	};
}