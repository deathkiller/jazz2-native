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
#include <IO/Stream.h>

using namespace Death::Containers;
using namespace Death::IO;
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
		std::unique_ptr<Texture> TextureDiffuse;
		//std::unique_ptr<Texture> TextureNormal;
		std::unique_ptr<uint8_t[]> Mask;
		Vector2i FrameDimensions;
		Vector2i FrameConfiguration;
		float AnimDuration;
		std::int32_t FrameCount;
		Vector2i Hotspot;
		Vector2i Coldspot;
		Vector2i Gunspot;

		GenericGraphicResource() noexcept;
	};

	struct GraphicResource
	{
		GenericGraphicResource* Base;
		AnimState State;
		float AnimDuration;
		std::int32_t FrameCount;
		std::int32_t FrameOffset;
		AnimationLoopMode LoopMode;

		GraphicResource() noexcept;

		bool operator<(const GraphicResource& p) const noexcept;
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

		GenericSoundResource(std::unique_ptr<Stream> stream, const StringView filename) noexcept;
	};

	struct SoundResource
	{
		SmallVector<GenericSoundResource*, 1> Buffers;

		SoundResource() noexcept;
	};

	enum class MetadataFlags {
		None = 0x00,

		Referenced = 0x01,
		AsyncFinalizingRequired = 0x02
	};

	DEFINE_ENUM_OPERATORS(MetadataFlags);

	struct Metadata
	{
		String Path;
		MetadataFlags Flags;
		SmallVector<GraphicResource, 0> Animations;
		HashMap<String, SoundResource> Sounds;
		Vector2i BoundingBox;

		Metadata() noexcept;

		GraphicResource* FindAnimation(AnimState state) noexcept;
	};
	
	struct Episode
	{
		String Name;
		String DisplayName;
		String FirstLevel;
		String PreviousEpisode;
		String NextEpisode;
		std::uint16_t Position;
		std::unique_ptr<Texture> TitleImage;
		std::unique_ptr<Texture> BackgroundImage;

		Episode() noexcept;
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

#if !defined(DISABLE_RESCALE_SHADERS)
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