#pragma once

#include "../Common.h"
#include "AnimState.h"
#include "GameDifficulty.h"
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
#include <Containers/SmallVector.h>
#include <Containers/StringView.h>
#include <IO/FileSystem.h>
#include <IO/Stream.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace nCine;

#define ALLOW_RESCALE_SHADERS

namespace Jazz2
{
	class LevelHandler;

	namespace Tiles
	{
		class TileSet;
	}

	enum class AnimationLoopMode {
		// The animation is played once an then remains in its last frame.
		Once,
		// The animation is looped: When reaching the last frame, it begins again at the first one.
		Loop,
		// A fixed, single frame is displayed.
		FixedSingle
	};

	enum class GenericGraphicResourceFlags {
		None = 0x00,

		Referenced = 0x01
	};

	DEFINE_ENUM_OPERATORS(GenericGraphicResourceFlags);

	class GenericGraphicResource
	{
	public:
		GenericGraphicResourceFlags Flags;
		//GenericGraphicResourceAsyncFinalize AsyncFinalize;

		std::unique_ptr<Texture> TextureDiffuse;
		std::unique_ptr<Texture> TextureNormal;
		std::unique_ptr<uint8_t[]> Mask;
		Vector2i FrameDimensions;
		Vector2i FrameConfiguration;
		float AnimDuration;
		int32_t FrameCount;
		Vector2i Hotspot;
		Vector2i Coldspot;
		Vector2i Gunspot;
	};

	class GraphicResource
	{
	public:
		GenericGraphicResource* Base;
		//GraphicResourceAsyncFinalize AsyncFinalize;

		SmallVector<AnimState, 4> State;
		//std::unique_ptr<Material> Material;
		float AnimDuration;
		int32_t FrameCount;
		int32_t FrameOffset;
		AnimationLoopMode LoopMode;

		bool HasState(AnimState state)
		{
			for (auto& current : State) {
				if (state == current) {
					return true;
				}
			}
			return false;
		}
	};

	class SoundResource
	{
	public:
		SmallVector<std::unique_ptr<AudioBuffer>, 1> Buffers;
	};

	enum class MetadataFlags {
		None = 0x00,

		Referenced = 0x01,
		AsyncFinalizingRequired = 0x02
	};

	DEFINE_ENUM_OPERATORS(MetadataFlags);

	class Metadata
	{
	public:
		MetadataFlags Flags;

		HashMap<String, GraphicResource> Graphics;
		HashMap<String, SoundResource> Sounds;
		Vector2i BoundingBox;

		Metadata()
			: Flags(MetadataFlags::None)
		{
		}
	};

	enum class TileDestructType {
		None = 0x00,

		Weapon = 0x01,
		Speed = 0x02,
		Collapse = 0x04,
		Special = 0x08,
		Trigger = 0x10,

		IgnoreSolidTiles = 0x20
	};

	DEFINE_ENUM_OPERATORS(TileDestructType);

	struct TileCollisionParams {
		TileDestructType DestructType;
		bool Downwards;
		WeaponType UsedWeaponType;
		int32_t WeaponStrength;
		float Speed;
		/*out*/ int32_t TilesDestroyed;
	};

	enum class SuspendType {
		None,
		Vine,
		Hook,
		SwingingVine
	};

	struct Episode {
		String Name;
		String DisplayName;
		String FirstLevel;
		String PreviousEpisode;
		String NextEpisode;
		uint16_t Position;
	};

	enum class FontType {
		Small,
		Medium,

		Count
	};

	enum class PrecompiledShader {
		Lighting,
		BatchedLighting,

		Blur,
		Downsample,
		Combine,
		CombineWithWater,
		CombineWithWaterLow,

		TexturedBackground,
		TexturedBackgroundCircle,

		Colorized,
		BatchedColorized,
		Tinted,
		BatchedTinted,
		Outline,
		BatchedOutline,
		WhiteMask,
		BatchedWhiteMask,
		PartialWhiteMask,
		BatchedPartialWhiteMask,
		FrozenMask,
		BatchedFrozenMask,
		ShieldFire,
		BatchedShieldFire,
		ShieldLightning,
		BatchedShieldLightning,

#if defined(ALLOW_RESCALE_SHADERS)
		ResizeHQ2x,
		Resize3xBrz,
		ResizeCrtScanlines,
		ResizeCrtShadowMask,
		ResizeCrtApertureGrille,
		ResizeMonochrome,
		ResizeScanlines,
#endif
		Antialiasing,
		Transition,

		Count
	};

	class ContentResolver
	{
	public:
		static constexpr uint8_t LevelFile = 1;
		static constexpr uint8_t EpisodeFile = 2;
		static constexpr uint8_t CacheIndexFile = 3;
		static constexpr uint8_t ConfigFile = 4;

		static constexpr int32_t PaletteCount = 256;
		static constexpr int32_t ColorsPerPalette = 256;
		static constexpr int32_t InvalidValue = INT_MAX;

		static ContentResolver& Get();

		~ContentResolver();
		
		void Release();

		bool IsHeadless() const;
		void SetHeadless(bool value);

		void BeginLoading();
		void EndLoading();

		void PreloadMetadataAsync(const StringView& path);
		Metadata* RequestMetadata(const StringView& path);
		GenericGraphicResource* RequestGraphics(const StringView& path, uint16_t paletteOffset);

		std::unique_ptr<Tiles::TileSet> RequestTileSet(const StringView& path, uint16_t captionTileId, bool applyPalette, const uint8_t* paletteRemapping = nullptr);
		bool LevelExists(const StringView& episodeName, const StringView& levelName);
		bool LoadLevel(LevelHandler* levelHandler, const StringView& path, GameDifficulty difficulty);
		void ApplyDefaultPalette();

		std::optional<Episode> GetEpisode(const StringView& name);
		std::optional<Episode> GetEpisodeByPath(const StringView& path);
		std::unique_ptr<AudioStreamPlayer> GetMusic(const StringView& path);
		UI::Font* GetFont(FontType fontType);
		Shader* GetShader(PrecompiledShader shader);
		void CompileShaders();
		static std::unique_ptr<Texture> GetNoiseTexture();

		const uint32_t* GetPalettes() const {
			return _palettes;
		}

		StringView GetContentPath() const {
#if defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT)
			return _contentPath;
#elif defined(DEATH_TARGET_ANDROID)
			return "asset::"_s;
#elif defined(DEATH_TARGET_SWITCH)
			return "romfs:/"_s;
#elif defined(DEATH_TARGET_WINDOWS)
			return "Content\\"_s;
#else
			return "Content/"_s;
#endif
		}

		StringView GetCachePath() const {
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT)
			return _cachePath;
#elif defined(DEATH_TARGET_SWITCH)
			// Switch has some issues with UTF-8 characters, so use "Jazz2" instead
			return "sdmc:/Games/Jazz2/Cache/"_s;
#elif defined(DEATH_TARGET_WINDOWS)
			return "Cache\\"_s;
#else
			return "Cache/"_s;
#endif
		}

		StringView GetSourcePath() const {
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT)
			return _sourcePath;
#elif defined(DEATH_TARGET_SWITCH)
			// Switch has some issues with UTF-8 characters, so use "Jazz2" instead
			return "sdmc:/Games/Jazz2/Source/"_s;
#elif defined(DEATH_TARGET_WINDOWS)
			return "Source\\"_s;
#else
			return "Source/"_s;
#endif
		}

	private:
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
		HashMap<String, std::unique_ptr<Metadata>> _cachedMetadata;
		HashMap<Pair<String, uint16_t>, std::unique_ptr<GenericGraphicResource>> _cachedGraphics;
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