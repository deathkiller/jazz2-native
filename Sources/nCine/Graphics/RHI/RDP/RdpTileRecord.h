#pragma once

#include <cstddef>
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

	/**
		@brief One quad of any size, with its own colour and optionally turned, in the packed form the RDP dispatch reads

		The particles of a level (see `Jazz2::Tiles::TileMap::DestructibleDebris`) are drawn as a mesh of these
		with @ref PrimitiveType::Points, 32 bytes each instead of 128 bytes of vertices and indices. @ref Flags
		of every record carry @ref IsQuad, which tells the mesh from one of @ref TileRecord.

		The quad covers @ref TexW x @ref TexH texels, from the corner at (@ref X, @ref Y) along the edge
		(@ref Ex, @ref Ey) and @ref FLen far perpendicular to it (a quarter turn clockwise, Y down). An edge
		with no vertical part is an upright quad, drawn as a rectangle, a turned one takes two triangles.
	*/
	struct QuadRecord
	{
		/** @brief Marks a record as a quad, see @ref Flags */
		static constexpr std::uint8_t IsQuad = 0x80;
		/** @brief Steps per pixel of @ref Ex, @ref Ey and @ref FLen */
		static constexpr std::int32_t EdgeScale = 64;
		/** @brief Steps per texel of @ref TexW and @ref TexH */
		static constexpr std::int32_t TexelScale = 32;

		/** @brief Corner the texture rectangle starts at, in the space the command's transformation maps */
		float X, Y;
		/** @brief Top-left texel of the texture rectangle */
		std::uint16_t TexX, TexY;
		/** @brief Alpha of the quad (it modulates the colour of the mesh) */
		std::uint8_t Alpha;
		/** @brief Always @ref IsQuad, at the offset of @ref TileRecord::Flags */
		std::uint8_t Flags;
		/** @brief Length of the edge along the vertical axis of the texture, in 1 / @ref EdgeScale of a pixel */
		std::uint16_t FLen;
		/** @brief Edge along the horizontal axis of the texture, in 1 / @ref EdgeScale of a pixel */
		std::int16_t Ex, Ey;
		/** @brief Size of the texture rectangle, in 1 / @ref TexelScale of a texel */
		std::uint16_t TexW, TexH;
		/** @brief Colour of the quad (it modulates the colour of the mesh) */
		std::uint8_t TintR, TintG, TintB;
		std::uint8_t Reserved[5];
	};

	static_assert(sizeof(QuadRecord) == 2 * sizeof(TileRecord), "A quad takes the room of two tile records");
	static_assert(offsetof(QuadRecord, Flags) == offsetof(TileRecord, Flags), "The flags tell the two records apart");
}
