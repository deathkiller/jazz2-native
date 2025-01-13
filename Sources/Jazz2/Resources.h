#pragma once

#include "../Main.h"
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
	/** @brief Flags for @ref GenericGraphicResource, supports a bitwise combination of its member values */
	enum class GenericGraphicResourceFlags
	{
		None = 0x00,

		Referenced = 0x01
	};

	DEATH_ENUM_FLAGS(GenericGraphicResourceFlags);

	/** @brief Shared graphic resource */
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

	/** @brief Specific graphic resource (from metadata) */
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

	/** @brief Flags for @ref GenericSoundResource, supports a bitwise combination of its member values */
	enum class GenericSoundResourceFlags
	{
		None = 0x00,

		Referenced = 0x01
	};

	DEATH_ENUM_FLAGS(GenericSoundResourceFlags);

	/** @brief Shared sound resource */
	struct GenericSoundResource
	{
		AudioBuffer Buffer;
		GenericSoundResourceFlags Flags;

		GenericSoundResource(std::unique_ptr<Stream> stream, StringView filename) noexcept;
	};

	/** @brief Specific sound resource (from metadata) */
	struct SoundResource
	{
		SmallVector<GenericSoundResource*, 1> Buffers;

		SoundResource() noexcept;
	};

	/** @brief Flags for @ref Metadata, supports a bitwise combination of its member values */
	enum class MetadataFlags {
		None = 0x00,

		Referenced = 0x01,
		AsyncFinalizingRequired = 0x02
	};

	DEATH_ENUM_FLAGS(MetadataFlags);

	/** @brief Contains assets for specific object type */
	struct Metadata
	{
		/** @brief Metadata path */
		String Path;
		/** @brief Metadata flags */
		MetadataFlags Flags;
		/** @brief Animations */
		SmallVector<GraphicResource, 0> Animations;
#if defined(WITH_AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Sounds */
		HashMap<String, SoundResource> Sounds;
#endif
		/** @brief Bounding box */
		Vector2i BoundingBox;

		Metadata() noexcept;

		/** @brief Finds specified animation state */
		GraphicResource* FindAnimation(AnimState state) noexcept;
	};
	
	/** @brief Describes an episode */
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

	/** @brief Font type */
	enum class FontType
	{
		Small,			/**< Small */
		Medium,			/**< Medium */

		Count			/**< Count of supported font types */
	};

	/** @brief Precompiled shader */
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
		TexturedBackgroundDither,
		TexturedBackgroundCircle,
		TexturedBackgroundCircleDither,

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