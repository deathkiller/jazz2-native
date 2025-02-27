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

namespace Jazz2::Resources
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
		/** @brief Resource flags */
		GenericGraphicResourceFlags Flags;
		/** @brief Diffuse texture */
		std::unique_ptr<Texture> TextureDiffuse;
		//std::unique_ptr<Texture> TextureNormal;
		/** @brief Collision mask */
		std::unique_ptr<uint8_t[]> Mask;
		/** @brief Frame dimensions */
		Vector2i FrameDimensions;
		/** @brief Frame configuration */
		Vector2i FrameConfiguration;
		/** @brief Animation duration (in normalized frames) */
		float AnimDuration;
		/** @brief Frame count */
		std::int32_t FrameCount;
		/** @brief Hotspot */
		Vector2i Hotspot;
		/** @brief Optional coldspot */
		Vector2i Coldspot;
		/** @brief Optional gunspot */
		Vector2i Gunspot;

		GenericGraphicResource() noexcept;
	};

	/** @brief Specific graphic resource (from metadata) */
	struct GraphicResource
	{
		/** @brief Underlying generic resource */
		GenericGraphicResource* Base;
		/** @brief Animation state */
		AnimState State;
		/** @brief Animation duration (in normalized frames) */
		float AnimDuration;
		/** @brief Frame count */
		std::int32_t FrameCount;
		/** @brief Frame offset */
		std::int32_t FrameOffset;
		/** @brief Animation loop mode */
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
		/** @brief Audio buffer */
		AudioBuffer Buffer;
		/** @brief Resource flags */
		GenericSoundResourceFlags Flags;

		GenericSoundResource(std::unique_ptr<Stream> stream, StringView filename) noexcept;
	};

	/** @brief Specific sound resource (from metadata) */
	struct SoundResource
	{
		/** @brief List of underlying generic resources */
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
		/** @brief Internal name */
		String Name;
		/** @brief Display name */
		String DisplayName;
		/** @brief Name of the first level in the episode */
		String FirstLevel;
		/** @brief Name of the previous episode */
		String PreviousEpisode;
		/** @brief Name of the next episode */
		String NextEpisode;
		/** @brief Position in episode selection list */
		std::uint16_t Position;
		/** @brief Texture for title image */
		std::unique_ptr<Texture> TitleImage;
		/** @brief Texture for background image */
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