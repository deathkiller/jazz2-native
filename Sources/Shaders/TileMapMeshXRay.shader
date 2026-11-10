program TileMapMeshXRay;
variant USE_PALETTE;

// Animated tiles of an x-ray layer (see Tiles::LayerRendererType::XRay): each carries both frames, and coverage of
// the actors behind the layer picks one per pixel. The 12-float vertex adds the second frame's texture coordinates
// and both frames' weights; a weight of 0 means the empty tile.

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

varying highp vec2 vTexCoords;
varying highp vec2 vTexCoords1;
varying vec2 vWeights;
varying vec4 vColor;
varying highp float vPaletteOffset;

uniform sampler2D uTexture : texture_unit(0);
#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif
uniform sampler2D uTextureMask : texture_unit(2);
// One over the target size, same as the mask size
uniform highp vec2 uTargetSizeInv;

// Texel to tile color; the caller samples so the texture coordinates keep their precision
vec4 tileSample(vec4 src) {
#if USE_PALETTE
	highp float palIndex = floor(vPaletteOffset + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 paletteColor = texture(uTexturePalette, vec2(palX, palY));
	return vec4(paletteColor.rgb, paletteColor.a * src.a);
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
	vec4 frame0 = tileSample(texture(uTexture, vTexCoords));
	vec4 frame1 = tileSample(texture(uTexture, vTexCoords1));
	// Premultiplied, so partial coverage blends the frames instead of darkening toward the empty one
	frame0 = vec4(frame0.rgb * frame0.a, frame0.a) * vWeights.x;
	frame1 = vec4(frame1.rgb * frame1.a, frame1.a) * vWeights.y;
	// Mask matches the target size, so the fragment position is its mask texel
	float mask = texture(uTextureMask, gl_FragCoord.xy * uTargetSizeInv).r;
	vec4 tile = mix(frame0, frame1, mask);
	COLOR = vec4(tile.rgb * vColor.rgb, tile.a) * vColor.a;
}
