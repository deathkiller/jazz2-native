program TileMapMesh;

#include "Include/TileMapVs.inc"
#include "Include/DefaultSpriteFs.inc"

void fixed_function() {
	// A whole visible layer arrives as one vertex stream (the 8-float TileMap::AppendTileQuad
	// contract) - consumed by the backend's tile-mesh pipeline stage, bound here by name
	pipeline tile_map_mesh;
}
