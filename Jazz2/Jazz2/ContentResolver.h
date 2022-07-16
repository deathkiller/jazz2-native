#pragma once

#include "../Common.h"

#include "../nCine/Graphics/Camera.h"
#include "../nCine/Graphics/Sprite.h"
#include "../nCine/Graphics/Texture.h"
#include "../nCine/Graphics/Viewport.h"
#include "../nCine/IO/FileSystem.h"

#include "AnimState.h"

#include <unordered_map>
#include <SmallVector.h>

using namespace nCine;

namespace Jazz2
{
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

	class GenericGraphicResource {
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

	class GraphicResource {
	public:
		GenericGraphicResource* Base;
		//GraphicResourceAsyncFinalize AsyncFinalize;

		Death::SmallVector<AnimState, 4> State;
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

	class SoundResource {
	public:
		//AudioBuffer* Sound;
	};

	enum class MetadataFlags {
		None = 0,

		Referenced = 0x01,
		AsyncFinalizingRequired = 0x02
	};

	DEFINE_ENUM_OPERATORS(MetadataFlags);

	class Metadata {
	public:
		MetadataFlags Flags;

		std::unordered_map<std::string, GraphicResource> Graphics;
		std::unordered_map<std::string, SoundResource> Sounds;
		Vector2i BoundingBox;
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

		void PreloadMetadataAsync(const std::string& path);
		Metadata* RequestMetadata(const std::string& path);
		GenericGraphicResource* RequestGraphics(const std::string& path);

		std::unique_ptr<Tiles::TileSet> RequestTileSet(const std::string& path, bool applyPalette, Color* customPalette);

		void ApplyPalette(const std::string& path);

		static ContentResolver& Current();

	private:
		static ContentResolver _current;

		/// Deleted copy constructor
		ContentResolver(const ContentResolver&) = delete;
		/// Deleted assignment operator
		ContentResolver& operator=(const ContentResolver&) = delete;

		bool _isLoading;
		uint32_t _palettes[PaletteCount * ColorsPerPalette];
		std::unordered_map<std::string, std::unique_ptr<Metadata>> _cachedMetadata;
		std::unordered_map<std::string, std::unique_ptr<GenericGraphicResource>> _cachedGraphics;
	};
}