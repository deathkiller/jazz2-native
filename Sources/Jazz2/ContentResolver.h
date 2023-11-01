#pragma once

#include "../Common.h"
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

	class ContentResolver
	{
	public:
		static constexpr std::uint8_t LevelFile = 1;
		static constexpr std::uint8_t EpisodeFile = 2;
		static constexpr std::uint8_t CacheIndexFile = 3;
		static constexpr std::uint8_t ConfigFile = 4;

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

		void BeginLoading();
		void EndLoading();

		void PreloadMetadataAsync(const StringView& path);
		Metadata* RequestMetadata(const StringView& path);
		GenericGraphicResource* RequestGraphics(const StringView& path, uint16_t paletteOffset);

		std::unique_ptr<Tiles::TileSet> RequestTileSet(const StringView& path, uint16_t captionTileId, bool applyPalette, const uint8_t* paletteRemapping = nullptr);
		bool LevelExists(const StringView& episodeName, const StringView& levelName);
		bool TryLoadLevel(const StringView& path, GameDifficulty difficulty, LevelDescriptor& descriptor);
		void ApplyDefaultPalette();

		std::optional<Episode> GetEpisode(const StringView& name);
		std::optional<Episode> GetEpisodeByPath(const StringView& path);
		std::unique_ptr<AudioStreamPlayer> GetMusic(const StringView& path);
		UI::Font* GetFont(FontType fontType);
		Shader* GetShader(PrecompiledShader shader);
		void CompileShaders();
		static std::unique_ptr<Texture> GetNoiseTexture();

		const std::uint32_t* GetPalettes() const {
			return _palettes;
		}

	private:
		struct StringRefEqualTo
		{
			inline bool operator()(const Reference<String>& a, const Reference<String>& b) const noexcept {
				return a.get() == b.get();
			}
		};

		ContentResolver();

		ContentResolver(const ContentResolver&) = delete;
		ContentResolver& operator=(const ContentResolver&) = delete;

		void InitializePaths();

		GenericGraphicResource* RequestGraphicsAura(const StringView& path, uint16_t paletteOffset);
		static void ReadImageFromFile(std::unique_ptr<Stream>& s, uint8_t* data, int32_t width, int32_t height, int32_t channelCount);
		
		std::unique_ptr<Shader> CompileShader(const char* shaderName, Shader::DefaultVertex vertex, const char* fragment, Shader::Introspection introspection = Shader::Introspection::Enabled);
		std::unique_ptr<Shader> CompileShader(const char* shaderName, const char* vertex, const char* fragment, Shader::Introspection introspection = Shader::Introspection::Enabled);
		
		void RecreateGemPalettes();
#if defined(DEATH_DEBUG)
		void MigrateGraphics(const StringView& path);
#endif

		bool _isHeadless;
		bool _isLoading;
		uint32_t _palettes[PaletteCount * ColorsPerPalette];
		HashMap<Reference<String>, std::unique_ptr<Metadata>, FNV1aHashFunc<String>, StringRefEqualTo> _cachedMetadata;
		HashMap<Pair<String, uint16_t>, std::unique_ptr<GenericGraphicResource>> _cachedGraphics;
		HashMap<String, std::unique_ptr<GenericSoundResource>> _cachedSounds;
		std::unique_ptr<UI::Font> _fonts[(int32_t)FontType::Count];
		std::unique_ptr<Shader> _precompiledShaders[(int32_t)PrecompiledShader::Count];

#if defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT)
		String _contentPath;
#endif
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT)
		String _cachePath;
		String _sourcePath;
#endif
	};
}