#pragma once

#include "../Common.h"
#include "AnimState.h"
#include "LevelInitialization.h"

#include "../nCine/Audio/AudioBuffer.h"
#include "../nCine/Graphics/Camera.h"
#include "../nCine/Graphics/Sprite.h"
#include "../nCine/Graphics/Texture.h"
#include "../nCine/Graphics/Viewport.h"
#include "../nCine/IO/FileSystem.h"
#include "../nCine/Base/HashMap.h"

#include <Containers/Pair.h>
#include <Containers/SmallVector.h>

using namespace Death::Containers;
using namespace nCine;

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
		None = 0,

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
		float FrameDuration;
		int FrameCount;
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
		float FrameDuration;
		int FrameCount;
		int FrameOffset;
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
		None = 0,

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
			: Flags(MetadataFlags::None), BoundingBox()
		{
		}
	};

	enum class LayerType {
		Sprite,
		Sky,
		Other
	};

	enum class BackgroundStyle {
		Plain,
		Sky,
		Circle
	};

	enum class TileDestructType {
		None,
		Weapon,
		Speed,
		Collapse,
		Special,
		Trigger
	};

	enum class SuspendType {
		None,
		Vine,
		Hook,
		SwingingVine
	};

	class ContentResolver
	{
	public:
		static constexpr int PaletteCount = 256;
		static constexpr int ColorsPerPalette = 256;
		static constexpr int InvalidValue = INT_MAX;

		ContentResolver();
		~ContentResolver();
		
		void Initialize();
		void Release();

		void BeginLoading();
		void EndLoading();

		void PreloadMetadataAsync(const StringView& path);
		Metadata* RequestMetadata(const StringView& path);
		GenericGraphicResource* RequestGraphics(const StringView& path, uint16_t paletteOffset);

		std::unique_ptr<Tiles::TileSet> RequestTileSet(const StringView& path, bool applyPalette, Color* customPalette);
		bool LoadLevel(LevelHandler* levelHandler, const StringView& path, GameDifficulty difficulty);
		void ApplyPalette(const StringView& path);

		const uint32_t* GetPalettes() const {
			return _palettes;
		}

		static ContentResolver& Current();

	private:
		/// Deleted copy constructor
		ContentResolver(const ContentResolver&) = delete;
		/// Deleted assignment operator
		ContentResolver& operator=(const ContentResolver&) = delete;

		void RecreateGemPalettes();

		bool _isLoading;
		uint32_t _palettes[PaletteCount * ColorsPerPalette];
		HashMap<String, std::unique_ptr<Metadata>> _cachedMetadata;
		HashMap<Pair<String, uint16_t>, std::unique_ptr<GenericGraphicResource>> _cachedGraphics;
	};
}