#pragma once

#include "../Common.h"
#include "AnimationLoopMode.h"
#include "AnimState.h"
#include "../nCine/Audio/AudioBuffer.h"
#include "../nCine/Base/HashMap.h"
#include "../nCine/Graphics/Texture.h"
#include "../nCine/Primitives/Vector2.h"

#include <memory>

#include <Containers/SmallVector.h>
#include <Containers/String.h>

#define ALLOW_RESCALE_SHADERS

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2
{
	enum class GenericGraphicResourceFlags
	{
		None = 0x00,

		Referenced = 0x01
	};

	DEFINE_ENUM_OPERATORS(GenericGraphicResourceFlags);

	struct GenericGraphicResource
	{
		GenericGraphicResourceFlags Flags;
		//GenericGraphicResourceAsyncFinalize AsyncFinalize;

		std::unique_ptr<Texture> TextureDiffuse;
		std::unique_ptr<Texture> TextureNormal;
		std::unique_ptr<uint8_t[]> Mask;
		Vector2i FrameDimensions;
		Vector2i FrameConfiguration;
		float AnimDuration;
		std::int32_t FrameCount;
		Vector2i Hotspot;
		Vector2i Coldspot;
		Vector2i Gunspot;

		GenericGraphicResource();
	};

	struct GraphicResource
	{
		GenericGraphicResource* Base;
		//GraphicResourceAsyncFinalize AsyncFinalize;

		SmallVector<AnimState, 4> State;
		//std::unique_ptr<Material> Material;
		float AnimDuration;
		std::int32_t FrameCount;
		std::int32_t FrameOffset;
		AnimationLoopMode LoopMode;

		GraphicResource();

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

	enum class GenericSoundResourceFlags
	{
		None = 0x00,

		Referenced = 0x01
	};

	DEFINE_ENUM_OPERATORS(GenericSoundResourceFlags);

	struct GenericSoundResource
	{
		AudioBuffer Buffer;
		GenericSoundResourceFlags Flags;

		GenericSoundResource(const StringView& path);
	};

	struct SoundResource
	{
		SmallVector<GenericSoundResource*, 1> Buffers;

		SoundResource();
	};

	enum class MetadataFlags {
		None = 0x00,

		Referenced = 0x01,
		AsyncFinalizingRequired = 0x02
	};

	DEFINE_ENUM_OPERATORS(MetadataFlags);

	struct Metadata
	{
		MetadataFlags Flags;
		HashMap<String, GraphicResource> Animations;
		HashMap<String, SoundResource> Sounds;
		Vector2i BoundingBox;

		Metadata();
	};
	
	struct Episode
	{
		String Name;
		String DisplayName;
		String FirstLevel;
		String PreviousEpisode;
		String NextEpisode;
		std::uint16_t Position;

		Episode();
	};

	enum class FontType
	{
		Small,
		Medium,

		Count
	};

	enum class PrecompiledShader
	{
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
}