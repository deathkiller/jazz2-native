program TileMapMeshXRay;
variant USE_PALETTE;

// Every global reaches both stages, InstanceBlock included, and a named uniform block whose members
// disagree on precision between them does not link - so the fragment prologue keeps the vertex default
precision highp;

// The animated tiles of an x-ray layer (see Tiles::LayerRendererType::XRay): each one carries both of
// its frames, and the coverage of the actors behind the layer decides per pixel which one is shown. The
// 12-float vertex is the tile-layer mesh layout with the second frame's texture coordinates and the two
// frames' weights added - a weight of zero is the empty tile, which is how a frame cuts the actor out of
// the layer or shows the tile only over it.

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
	float palOffset;
};

attribute vec2 aPosition;
attribute vec2 aTexCoords;
attribute vec2 aTexCoords1;
attribute vec2 aWeights;
attribute vec4 aColor;

varying vec2 vTexCoords;
varying vec2 vTexCoords1;
varying vec2 vWeights;
varying vec4 vColor;
varying highp float vPaletteOffset;

uniform sampler2D uTexture : texture_unit(0);
#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif
uniform sampler2D uTextureMask : texture_unit(2);
// One over the size of the target being drawn into, which the mask has too
uniform vec2 uTargetSizeInv;

vec4 tileSample(vec2 uv) {
	vec4 src = texture(uTexture, uv);
#if USE_PALETTE
	highp float palIndex = floor(vPaletteOffset + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 color = texture(uTexturePalette, vec2(palX, palY));
	return vec4(color.rgb, color.a * src.a);
#else
	return src;
#endif
}

void vertex() {
	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * vec4(aPosition.x, aPosition.y, 0.0, 1.0);
	vTexCoords = aTexCoords;
	vTexCoords1 = aTexCoords1;
	vWeights = aWeights;
	vColor = aColor * color;
	vPaletteOffset = palOffset;
}

void fragment() {
	vec4 frame0 = tileSample(vTexCoords);
	vec4 frame1 = tileSample(vTexCoords1);
	// Mixed premultiplied, so a partly covered pixel blends the two frames instead of darkening towards the
	// black of the empty one
	frame0 = vec4(frame0.rgb * frame0.a, frame0.a) * vWeights.x;
	frame1 = vec4(frame1.rgb * frame1.a, frame1.a) * vWeights.y;
	// The mask and the target are the same size, so the fragment's own position is its texel in the mask
	float mask = texture(uTextureMask, gl_FragCoord.xy * uTargetSizeInv).r;
	vec4 tile = mix(frame0, frame1, mask);
	COLOR = vec4(tile.rgb * vColor.rgb, tile.a) * vColor.a;
}
