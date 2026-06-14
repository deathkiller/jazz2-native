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
		None = 0x00,						/**< None */

		Referenced = 0x01,					/**< The resource is referenced and should not be released */
		
		/**
		 * @brief Diffuse texture stores raw palette indices instead of baked colors
		 *
		 * The index is in the red channel, so the texture must be rendered with the @ref
		 * PrecompiledShader::PaletteRemap shader and a palette texture.
		 */
		Indexed = 0x02
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

		/** @brief Creates a new instance */
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
		/**
		 * @brief Flat palette offset for an indexed sprite (from the metadata `PaletteOffset`)
		 *
		 * Selects which palette region the @ref PrecompiledShader::PaletteRemap shader samples at draw time (e.g. the
		 * gem gradient rows).
		 */
		std::uint16_t PaletteOffset;

		/** @brief Creates a new instance */
		GraphicResource() noexcept;

		/** @brief Compares two resources by animation state */
		bool operator<(const GraphicResource& p) const noexcept;
	};

	/** @brief Flags for @ref GenericSoundResource, supports a bitwise combination of its member values */
	enum class GenericSoundResourceFlags
	{
		None = 0x00,						/**< None */

		Referenced = 0x01					/**< The resource is referenced and should not be released */
	};

	DEATH_ENUM_FLAGS(GenericSoundResourceFlags);

	/** @brief Shared sound resource */
	struct GenericSoundResource
	{
		/** @brief Audio buffer */
		AudioBuffer Buffer;
		/** @brief Resource flags */
		GenericSoundResourceFlags Flags;

		/**
		 * @brief Creates a new instance from a stream
		 *
		 * @param stream	Stream containing the sound data
		 * @param filename	File name used to detect the audio format
		 */
		GenericSoundResource(std::unique_ptr<Stream> stream, StringView filename) noexcept;
	};

	/** @brief Specific sound resource (from metadata) */
	struct SoundResource
	{
		/** @brief List of underlying generic resources */
		SmallVector<GenericSoundResource*, 1> Buffers;

		/** @brief Creates a new instance */
		SoundResource() noexcept;
	};

	/** @brief Flags for @ref Metadata, supports a bitwise combination of its member values */
	enum class MetadataFlags {
		None = 0x00,						/**< None */

		Referenced = 0x01,					/**< The metadata is referenced and should not be released */
		AsyncFinalizingRequired = 0x02		/**< The metadata still requires asynchronous finalization */
	};

	DEATH_ENUM_FLAGS(MetadataFlags);

	/**
		@brief Contains assets for specific object type
		
		Loaded from a metadata file and cached by @ref ContentResolver, it groups the animations and sounds used by
		one object type together with its bounding box, and resolves an @ref AnimState to the matching
		@ref GraphicResource at runtime.
	*/
	struct Metadata
	{
		/** @brief Metadata path */
		String Path;
		/** @brief Key this metadata is cached under (usually equals @ref Path, but indexed variants use a distinct key); the cache stores a reference to this string, so it must not be modified after insertion */
		String CacheKey;
		/** @brief Metadata flags */
		MetadataFlags Flags;
		/** @brief Animations */
		SmallVector<GraphicResource, 0> Animations;
		/** @brief Sounds */
		HashMap<String, SoundResource> Sounds;
		/** @brief Bounding box */
		Vector2i BoundingBox;

		/** @brief Creates a new instance */
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

		/** @brief Creates a new instance */
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
		Lighting,							/**< Lighting */
		BatchedLighting,					/**< Batched variant of @ref Lighting */

		Blur,								/**< Blur */
		Downsample,							/**< Downsample */
		Combine,							/**< Combine render passes */
		CombineWithWater,					/**< Combine render passes with water effect */
		CombineWithWaterLow,				/**< Combine render passes with low quality water effect */

		TexturedBackground,					/**< Textured background */
		TexturedBackgroundDither,			/**< Textured background with dithering */
		TexturedBackgroundCircle,			/**< Circular textured background */
		TexturedBackgroundCircleDither,		/**< Circular textured background with dithering */

		Colorized,							/**< Colorized */
		BatchedColorized,					/**< Batched variant of @ref Colorized */
		Tinted,								/**< Tinted */
		BatchedTinted,						/**< Batched variant of @ref Tinted */
		Outline,							/**< Outline */
		BatchedOutline,						/**< Batched variant of @ref Outline */
		WhiteMask,							/**< White mask */
		BatchedWhiteMask,					/**< Batched variant of @ref WhiteMask */
		PartialWhiteMask,					/**< Partial white mask */
		BatchedPartialWhiteMask,			/**< Batched variant of @ref PartialWhiteMask */
		FrozenMask,							/**< Frozen mask */
		BatchedFrozenMask,					/**< Batched variant of @ref FrozenMask */
		PaletteRemap,						/**< Remaps indexed sprites through a palette texture */
		BatchedPaletteRemap,				/**< Batched variant of @ref PaletteRemap */
		OutlinePalette,						/**< Palette variant of @ref Outline */
		BatchedOutlinePalette,				/**< Batched variant of @ref OutlinePalette */
		WhiteMaskPalette,					/**< Palette variant of @ref WhiteMask */
		BatchedWhiteMaskPalette,			/**< Batched variant of @ref WhiteMaskPalette */
		PartialWhiteMaskPalette,			/**< Palette variant of @ref PartialWhiteMask */
		BatchedPartialWhiteMaskPalette,		/**< Batched variant of @ref PartialWhiteMaskPalette */
		FrozenMaskPalette,					/**< Palette variant of @ref FrozenMask */
		BatchedFrozenMaskPalette,			/**< Batched variant of @ref FrozenMaskPalette */
		TintedPalette,						/**< Palette variant of @ref Tinted */
		BatchedTintedPalette,				/**< Batched variant of @ref TintedPalette */
		ShieldFire,							/**< Fire shield effect */
		BatchedShieldFire,					/**< Batched variant of @ref ShieldFire */
		ShieldLightning,					/**< Lightning shield effect */
		BatchedShieldLightning,				/**< Batched variant of @ref ShieldLightning */

#if defined(TILEMAP_USE_SINGLE_DRAW)
		// Whole-layer tile mesh: one draw call per tile layer instead of one per visible tile. Reads per-vertex
		// position/texcoords/alpha; `TileMapMeshPalette` additionally recolors indexed tilesets via the palette texture.
		TileMapMesh,						/**< Tile-map aggregation shader */
		TileMapMeshPalette,					/**< Batched variant of @ref TileMapMesh */
#endif

#if !defined(DISABLE_RESCALE_SHADERS)
		ResizeHQ2x,							/**< HQ2× upscaling */
		Resize3xBrz,						/**< 3×BRZ upscaling */
		ResizeCrtScanlines,					/**< CRT scanlines upscaling */
		ResizeCrtShadowMask,				/**< CRT shadow mask upscaling */
		ResizeCrtApertureGrille,			/**< CRT aperture grille upscaling */
		ResizeMonochrome,					/**< Monochrome upscaling */
		ResizeSabr,							/**< SABR upscaling */
		ResizeCleanEdge,					/**< CleanEdge upscaling */
#endif
		Antialiasing,						/**< Antialiasing */
		Transition,							/**< Screen transition effect */
		TouchCircle,						/**< Touch control circle */

		Count								/**< Number of precompiled shaders */
	};
}