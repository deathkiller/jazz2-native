#pragma once

#include <cstdint>

namespace nCine::RHI::RDP
{
	/**
		@brief One tile of a tile-layer mesh in the packed form the RDP dispatch reads

		A tile layer reaches the RDP backend as a mesh of the `TileMapMesh` effect. Drawn with
		@ref PrimitiveType::Points, its vertex buffer holds one of these per tile instead of vertices: where the
		tile goes, and which texels of the atlas chunk it shows - as the integers the RDP takes them as, so the
		dispatch neither scales normalized coordinates back up nor rounds them onto the texel grid. At 16 bytes
		it is a quarter of the two 8-float corners a tile took before, which matters on a CPU where every cache
		line a frame reads costs ~44 cycles.

		Every tile is @ref Size pixels and texels square, mapped 1:1 before the view's own scale. The mesh is
		produced only for this backend (the tile map's grouped emission and its N64 layer cache); every other
		backend is given ordinary vertices.
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
