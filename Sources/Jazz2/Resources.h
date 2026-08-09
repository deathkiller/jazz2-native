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
	/**
		@brief Flags for @ref GenericGraphicResource
		
		Per-resource state of a shared graphic. @ref GenericGraphicResourceFlags::Referenced keeps the resource alive
		(it must not be released while in use), and @ref GenericGraphicResourceFlags::Indexed marks a diffuse texture
		that stores raw palette indices in the red channel, so it must be drawn through the @ref
		PrecompiledShader::PaletteRemap shader and a palette texture. Supports a bitwise combination of its member
		values.
	*/
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

	/**
		@brief Placement of a single frame in a tightly packed sprite sheet

		@ref X, @ref Y, @ref W and @ref H are the frame's area in the sheet. The offsets say where that area
		begins inside the frame's logical cell, which is what keeps the animation aligned: trimming away the
		transparent margin moves the content, and the offset moves the hotspot with it.
	*/
	struct FrameRect
	{
		std::uint16_t X, Y, W, H;
		std::int16_t OffsetX, OffsetY;
	};

	/**
		@brief Shared graphic resource
		
		Loaded, cached representation of a sprite sheet: the diffuse texture (optionally indexed), an optional
		collision mask, the frame grid (dimensions and configuration), frame count and animation duration, plus the
		hotspot, coldspot and gunspot offsets. Owned by @ref ContentResolver and referenced by the per-animation
		@ref GraphicResource entries that map an @ref AnimState onto a slice of these frames.
	*/
	/** @brief Alpha above which a sprite pixel counts as solid when its collision mask is built */
	static constexpr std::uint8_t MaskAlphaThreshold = 40;

	/** @brief Returns whether the pixel at the given flat index is solid in a @ref GenericGraphicResource::Mask */
	DEATH_ALWAYS_INLINE bool IsMaskPixelSolid(const std::uint8_t* mask, std::int32_t index)
	{
		return (mask[index >> 3] & (std::uint8_t(1) << (index & 7))) != 0;
	}

	struct GenericGraphicResource
	{
		/** @brief Resource flags */
		GenericGraphicResourceFlags Flags;
		/** @brief Diffuse texture */
		std::unique_ptr<Texture> TextureDiffuse;
		//std::unique_ptr<Texture> TextureNormal;
		/**
			@brief Collision mask, one **bit** per pixel (set = solid), rows packed continuously

			Per-pixel collision only ever asks whether a pixel is solid, so the mask stores a single bit
			instead of the source alpha - at one byte per pixel the masks of a large level's sprite sheets
			ran to several megabytes, which is a sizeable share of a console's whole heap. Index it with
			@ref IsMaskPixelSolid() rather than by hand.
		*/
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
		/**
			@brief Where each frame sits in the sheet, empty when the frames form a regular grid

			Sprite sheets can be packed two ways. The regular grid gives every frame a cell of @ref
			FrameDimensions, which wastes the difference between each frame's own extent and the largest one
			in the animation - on a sheet padded to power-of-two dimensions that easily doubles its size.
			When the frames are packed tightly instead, each one keeps only the space its opaque pixels need
			and this table says where it ended up; @ref GetFrameRect() and @ref GetFrameAnchor() hide the
			difference from everything that draws a frame.
		*/
		SmallVector<FrameRect, 0> FrameRects;
		/** @brief Distance between two rows of @ref Mask, in bits (the sheet width in pixels) */
		std::int32_t MaskStride;

		/** @brief Creates a new instance */
		GenericGraphicResource() noexcept;

		/** @brief Returns the distance between two rows of @ref Mask, in bits (the sheet width in pixels) */
		inline std::int32_t GetMaskStride() const {
			return (MaskStride > 0 ? MaskStride : FrameConfiguration.X * FrameDimensions.X);
		}
		/** @brief Returns the area the given frame occupies in the sheet, in pixels */
		inline Recti GetFrameRect(std::int32_t frame) const {
			if (!FrameRects.empty()) {
				const FrameRect& rect = FrameRects[frame < (std::int32_t)FrameRects.size() ? frame : 0];
				return Recti(rect.X, rect.Y, rect.W, rect.H);
			}
			const std::int32_t columns = (FrameConfiguration.X > 0 ? FrameConfiguration.X : 1);
			return Recti((frame % columns) * FrameDimensions.X, (frame / columns) * FrameDimensions.Y,
				FrameDimensions.X, FrameDimensions.Y);
		}
		/**
			@brief Returns where the given frame's area begins inside its logical cell

			Anything that positions a frame by aligning @ref FrameDimensions should add this, so trimming the
			transparent margin away does not move the frame on screen.
		*/
		inline Vector2i GetFrameOffset(std::int32_t frame) const {
			if (!FrameRects.empty()) {
				const FrameRect& rect = FrameRects[frame < (std::int32_t)FrameRects.size() ? frame : 0];
				return Vector2i(rect.OffsetX, rect.OffsetY);
			}
			return Vector2i(0, 0);
		}
		/**
			@brief Returns the hotspot of the given frame, relative to the frame's own area

			Flipping mirrors the texture inside the quad that the frame is drawn on, so a flipped frame needs
			its hotspot mirrored within that same area. For a regular grid the area is the whole cell, which
			is why mirroring within @ref FrameDimensions is right there; for a packed frame the area is only
			the space its pixels occupy, and mirroring within the cell would displace it by the trimmed
			margin. Both cases fall out of the same expression.
		*/
		inline Vector2i GetFrameAnchor(std::int32_t frame, bool flippedX = false, bool flippedY = false) const {
			const Recti rect = GetFrameRect(frame);
			const Vector2i offset = GetFrameOffset(frame);
			return Vector2i(flippedX ? (offset.X + rect.W - Hotspot.X) : (Hotspot.X - offset.X),
				flippedY ? (offset.Y + rect.H - Hotspot.Y) : (Hotspot.Y - offset.Y));
		}
	};

	/**
		@brief Specific graphic resource (from metadata)
		
		One animation entry parsed from an object's metadata. It points at a shared @ref GenericGraphicResource and
		describes the sub-range of its frames to play --- frame offset, frame count, animation duration and loop mode
		--- the @ref AnimState it represents, and the palette offset used when the sprite is indexed. Stored in a
		@ref Metadata and looked up by animation state at runtime.
	*/
	struct GraphicResource
	{
		/** @brief Value of @ref DeferredIndex of an entry that has no graphics to load (anymore), and the number of deferred animations a single metadata can describe */
		static constexpr std::uint16_t NotDeferred = UINT16_MAX;

		/** @brief Underlying generic resource, `nullptr` until a deferred entry is resolved */
		GenericGraphicResource* Base;
		/** @brief Animation state */
		AnimState State;
		/** @brief Animation duration (in normalized frames), valid only once @ref Base is loaded */
		float AnimDuration;
		/** @brief Frame count, valid only once @ref Base is loaded */
		std::int32_t FrameCount;
		/** @brief Frame offset */
		std::int32_t FrameOffset;
		/** @brief Animation loop mode */
		AnimationLoopMode LoopMode;
		/**
		 * @brief Flat palette offset for an indexed sprite (from the metadata `PaletteOffset`)
		 *
		 * Selects which palette region the @ref PrecompiledShader::PaletteRemap shader samples at draw time
		 * (e.g., the gem gradient rows).
		 */
		std::uint16_t PaletteOffset;
		/**
		 * @brief Index into @ref Metadata::DeferredAnimations describing how to load @ref Base, or @ref NotDeferred
		 *
		 * Three states are encoded together with @ref Base: a loaded entry has `Base != nullptr`, a deferred one
		 * has `Base == nullptr` and a valid index, and one whose graphics could not be loaded has `Base == nullptr`
		 * and @ref NotDeferred --- which is what keeps a missing asset from being retried on every single lookup.
		 *
		 * Sits next to @ref PaletteOffset, and is only 16 bits wide, so that both fit in the padding the struct
		 * has anyway (a level holds one of these per animation state of every object type it spawns).
		 */
		std::uint16_t DeferredIndex;

		/** @brief Creates a new instance */
		GraphicResource() noexcept;

		/** @brief Compares two resources by animation state */
		bool operator<(const GraphicResource& p) const noexcept;
	};

	/**
		@brief Description of a graphic resource that is loaded on first use

		Everything @ref ContentResolver needs to load the sheet behind a @ref GraphicResource that was declared
		deferred (see @ref Metadata::DeferredAnimations), i.e., the part of a metadata entry that is otherwise
		consumed during parsing and then thrown away. One description is shared by every animation state
		declared on the same metadata entry.
	*/
	struct DeferredGraphicResource
	{
		/** @brief Relative path to the graphics asset */
		String Path;
		/** @brief Animation duration override (in normalized frames), used only if @ref HasAnimDuration */
		float AnimDuration;
		/** @brief Frame count override, used only if @ref HasFrameCount */
		std::int32_t FrameCount;
		/** @brief Whether the sheet keeps raw palette indices instead of baked colors */
		bool KeepIndexed;
		/** @brief Whether the metadata entry specified its own animation duration */
		bool HasAnimDuration;
		/** @brief Whether the metadata entry specified its own frame count */
		bool HasFrameCount;
	};

	/**
		@brief Flags for @ref GenericSoundResource
		
		Per-resource state of a shared sound. @ref GenericSoundResourceFlags::Referenced keeps the resource alive so
		it is not released while still in use. Supports a bitwise combination of its member values.
	*/
	enum class GenericSoundResourceFlags
	{
		None = 0x00,						/**< None */

		Referenced = 0x01					/**< The resource is referenced and should not be released */
	};

	DEATH_ENUM_FLAGS(GenericSoundResourceFlags);

	/**
		@brief Shared sound resource
		
		Loaded, cached audio buffer for a single sound file, decoded on construction from a stream (the file name is
		used to detect the format), together with its resource flags. Owned by @ref ContentResolver and referenced by
		the @ref SoundResource entries that group the buffers belonging to one named metadata sound.
	*/
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

	/**
		@brief Specific sound resource (from metadata)
		
		A named sound declared in an object's metadata, holding the list of shared @ref GenericSoundResource buffers
		it can use. When the sound has several variants, one of the buffers is chosen at playback time. Stored in a
		@ref Metadata keyed by the sound's name.
	*/
	struct SoundResource
	{
		/** @brief List of underlying generic resources */
		SmallVector<GenericSoundResource*, 1> Buffers;

		/** @brief Creates a new instance */
		SoundResource() noexcept;
	};

	/**
		@brief Flags for @ref Metadata
		
		Per-resource state of a loaded metadata. @ref MetadataFlags::Referenced keeps it alive so it is not released
		while still in use, and @ref MetadataFlags::AsyncFinalizingRequired marks metadata whose linked assets still
		need asynchronous finalization before it is fully ready. Supports a bitwise combination of its member values.
	*/
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

		@section metadata-deferred Deferred animations

		A metadata file that sets `"Deferred": true` (either for the whole file or on a single animation entry)
		is parsed as usual, but none of its sheets are read until an animation is actually looked up --- the
		descriptions needed to load them are kept in @ref DeferredAnimations and @ref FindAnimation() resolves
		an entry the first time it is asked for. This is what a set of assets that are *declared* together but
		*used* apart wants: the UI metadata describes every gamepad button label, touch button and menu icon in
		the game, while a given run of the game only ever draws the labels of one gamepad type. Everything else
		then costs a few dozen bytes of description instead of a decoded sheet and a texture, and the ordinary
		mark-and-sweep of the resolver still releases whatever was loaded once the metadata itself goes away.

		Deferral is opt-in because it trades a load that happens at a known time (a loading screen) for one that
		happens at first draw, which is right for UI and wrong for an actor that must animate without hitching.
		A deferred metadata cannot derive its @ref BoundingBox from its first sheet either, so one that needs a
		bounding box has to declare it explicitly.
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
		/** @brief Descriptions of the animations that are loaded on first use (see @ref metadata-deferred) */
		SmallVector<DeferredGraphicResource, 0> DeferredAnimations;
		/** @brief Sounds */
		HashMap<String, SoundResource> Sounds;
		/** @brief Bounding box */
		Vector2i BoundingBox;

		/** @brief Creates a new instance */
		Metadata() noexcept;

		/**
		 * @brief Finds specified animation state
		 *
		 * Loads the graphics of a deferred animation (see @ref metadata-deferred) if this is the first time it
		 * is asked for, and returns `nullptr` if they cannot be loaded.
		 */
		GraphicResource* FindAnimation(AnimState state);
	};
	
	/**
		@brief Describes an episode
		
		Metadata for a group of levels presented as one episode: its internal and display names, the first level to
		play, the previous and next episode names that chain episodes together, the position in the episode-selection
		list, and optional title and background image textures. Loaded by @ref ContentResolver and used to build the
		episode selection UI and drive episode-to-episode progression.
	*/
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

	/**
		@brief Font type
		
		Selects one of the engine's built-in bitmap fonts --- a small and a medium size --- used when requesting a
		font from @ref ContentResolver. @ref FontType::Count is the number of supported font types and sizes the
		font cache.
	*/
	enum class FontType
	{
		Small,			/**< Small */
		Medium,			/**< Medium */

		Count			/**< Count of supported font types */
	};

	/**
		@brief Precompiled shader
		
		Identifies one of the engine's built-in shader programs, compiled up front by @ref ContentResolver and
		requested by this value. It covers the lighting and blur/downsample/combine post-processing passes, the
		textured background variants, the per-sprite effect shaders (colorized, tinted, outline, white/frozen masks)
		and their batched, palette and tile-map mesh variants, the optional upscaling/CRT rescalers, plus
		antialiasing, screen transition and touch-control shaders. @ref PrecompiledShader::Count is the number of
		precompiled shaders. Some entries are compiled only when the matching build option is enabled.
	*/
	enum class PrecompiledShader
	{
		LightingMesh,						/**< Light aggregation shader, one draw for all lights of a viewport */

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