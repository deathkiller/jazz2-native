program LightingMeshBatch;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

attribute vec2 aLightCorner;
attribute vec2 aLightCenter;
attribute vec2 aLightParams;
attribute vec2 aLightColor;

varying vec4 vTexCoords;
varying vec4 vColor;

void vertex() {
	vec2 worldPosition = aLightCenter + aLightCorner * aLightParams.x;
	gl_Position = uProjectionMatrix * uViewMatrix * vec4(worldPosition, 0.0, 1.0);
	vTexCoords = vec4(aLightCenter, aLightParams.y, 0.0);
	vColor = vec4(aLightColor, aLightCorner);
}

#include "Include/LightingFs.inc"
