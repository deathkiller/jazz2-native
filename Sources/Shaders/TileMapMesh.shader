program TileMapMesh;

#include "Include/TileMapVs.inc"
#include "Include/DefaultSpriteFs.inc"

void fixed_function() {
	// The whole layer as one vertex stream (8 floats per vertex, see TileMap::AppendTileQuad)
	pipeline tile_map_mesh;
}
