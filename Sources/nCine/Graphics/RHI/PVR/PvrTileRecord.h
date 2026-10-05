#pragma once

#include <cstdint>

namespace nCine::RHI::PVR
{
	/**
		@brief One tile of a tile-layer mesh in the packed form the PVR dispatch reads

		A tile layer reaches the PVR backend as a mesh of the `TileMapMesh` effect. Drawn with
		@ref PrimitiveType::Points, its vertex buffer holds one of these per tile instead of vertices: where the
		tile goes, and which texels of the atlas chunk it shows. At 16 bytes it is an eighth of the four 8-float
		corners a tile took before, plus the six indices they were addressed through - bytes the tile map wrote,
		the commit copied into the streaming buffer and the dispatch read back, for some thousand tiles a frame
		at 640x480. The dispatch synthesizes the corners itself, which also spares it resolving the indices and
		recognizing that two triangles form a quad.

		Every tile is @ref Size pixels and texels square, mapped 1:1 before the view's own scale. The mesh is
		produced only for this backend's tile layers (see `TILEMAP_PACKED_TILE_RECORDS`); the debris sharing
		the effect keeps its four real corners, because its spin is folded into them.
	*/
	struct TileRecord
	{
		/** @brief Width and height of every tile, in pixels and in texels */
		static constexpr std::int32_t Size = 32;
		/** @brief Horizontal flip, see @ref Flags */
		static constexpr std::uint8_t FlipX = 0x01;
		/** @brief Vertical flip, see @ref Flags */
		static constexpr std::uint8_t FlipY = 0x02;

		/** @brief Top-left corner of the tile, in the space the command's transformation maps */
		float X, Y;
		/** @brief Top-left texel of the tile in the atlas chunk the mesh samples */
		std::uint16_t TexX, TexY;
		/** @brief Alpha of the tile (it modulates the layer colour) */
		std::uint8_t Alpha;
		/** @brief Combination of @ref FlipX and @ref FlipY */
		std::uint8_t Flags;
		std::uint16_t Reserved;
	};

	static_assert(sizeof(TileRecord) == 16, "The records are laid out four floats apart");
}
