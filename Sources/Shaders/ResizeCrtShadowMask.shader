program ResizeCrtShadowMask;

#include "Include/ResizeCrtVs.inc"

#define hardScan -6.0 /*-8.0*/
#define hardPix -3.0
#define warpX 0.031
#define warpY 0.041
#define maskDark 0.55
#define maskLight 1.5
#define scaleInLinearGamma 1
#define brightboost 1.0
#define hardBloomScan -2.0
#define hardBloomPix -1.5
#define bloomAmount 1.0/12.0 /*1.0/16.0*/
#define shape 2.0

uniform sampler2D uTexture : texture_unit(0);

#ifdef SIMPLE_LINEAR_GAMMA
	float ToLinear1(float c) {
		return c;
	}
	vec3 ToLinear(vec3 c) {
		return c;
	}
	vec3 ToSrgb(vec3 c) {
		return pow(c, 1.0 / 2.2);
	}
#else
	float ToLinear1(float c) {
		if (scaleInLinearGamma==0) return c;
		return(c<=0.04045)?c/12.92:pow((c+0.055)/1.055,2.4);
	}
	vec3 ToLinear(vec3 c) {
		if (scaleInLinearGamma==0) return c;
		return vec3(ToLinear1(c.r),ToLinear1(c.g),ToLinear1(c.b));
	}
	float ToSrgb1(float c) {
		if (scaleInLinearGamma==0) return c;
		return(c<0.0031308?c*12.92:1.055*pow(c,0.41666)-0.055);
	}
	vec3 ToSrgb(vec3 c) {
		if (scaleInLinearGamma==0) return c;
		return vec3(ToSrgb1(c.r),ToSrgb1(c.g),ToSrgb1(c.b));
	}
#endif

// Nearest emulated sample given floating point position and texel offset.
// Also zero's off screen.
vec3 Fetch(vec2 pos, vec2 off, vec2 texture_size) {
	pos=(floor(pos*texture_size.xy+off)+vec2(0.5,0.5))/texture_size.xy;
#ifdef SIMPLE_LINEAR_GAMMA
	return ToLinear(vec3(brightboost) * pow(texture(uTexture,pos.xy).rgb, 2.2));
#else
	return ToLinear(vec3(brightboost) * texture(uTexture,pos.xy).rgb);
#endif
}

// Distance in emulated pixels to nearest texel
vec2 Dist(vec2 pos, vec2 texture_size) {
	pos=pos*texture_size.xy;
	return -((pos-floor(pos))-vec2(0.5, 0.5));
}

// 1D Gaussian
float Gaus(float pos,float scale) {
	return exp2(scale*pow(abs(pos),shape));
}

// 3-tap Gaussian filter along horz line
vec3 Horz3(vec2 pos, float off, vec2 texture_size) {
	vec3 b=Fetch(pos,vec2(-1.0,off),texture_size);
	vec3 c=Fetch(pos,vec2( 0.0,off),texture_size);
	vec3 d=Fetch(pos,vec2( 1.0,off),texture_size);
	float dst=Dist(pos, texture_size).x;
	// Convert distance to weight.
	float scale=hardPix;
	float wb=Gaus(dst-1.0,scale);
	float wc=Gaus(dst+0.0,scale);
	float wd=Gaus(dst+1.0,scale);
	// Return filtered sample.
	return (b*wb+c*wc+d*wd)/(wb+wc+wd);
}

// 5-tap Gaussian filter along horz line
vec3 Horz5(vec2 pos, float off, vec2 texture_size) {
	vec3 a=Fetch(pos,vec2(-2.0,off),texture_size);
	vec3 b=Fetch(pos,vec2(-1.0,off),texture_size);
	vec3 c=Fetch(pos,vec2( 0.0,off),texture_size);
	vec3 d=Fetch(pos,vec2( 1.0,off),texture_size);
	vec3 e=Fetch(pos,vec2( 2.0,off),texture_size);
	float dst=Dist(pos, texture_size).x;
	// Convert distance to weight.
	float scale=hardPix;
	float wa=Gaus(dst-2.0,scale);
	float wb=Gaus(dst-1.0,scale);
	float wc=Gaus(dst+0.0,scale);
	float wd=Gaus(dst+1.0,scale);
	float we=Gaus(dst+2.0,scale);
	// Return filtered sample.
	return (a*wa+b*wb+c*wc+d*wd+e*we)/(wa+wb+wc+wd+we);
}

// 7-tap Gaussian filter along horz line
vec3 Horz7(vec2 pos, float off, vec2 texture_size) {
	vec3 a=Fetch(pos,vec2(-3.0,off),texture_size);
	vec3 b=Fetch(pos,vec2(-2.0,off),texture_size);
	vec3 c=Fetch(pos,vec2(-1.0,off),texture_size);
	vec3 d=Fetch(pos,vec2( 0.0,off),texture_size);
	vec3 e=Fetch(pos,vec2( 1.0,off),texture_size);
	vec3 f=Fetch(pos,vec2( 2.0,off),texture_size);
	vec3 g=Fetch(pos,vec2( 3.0,off),texture_size);
	float dst=Dist(pos, texture_size).x;
	// Convert distance to weight.
	float scale=hardBloomPix;
	float wa=Gaus(dst-3.0,scale);
	float wb=Gaus(dst-2.0,scale);
	float wc=Gaus(dst-1.0,scale);
	float wd=Gaus(dst+0.0,scale);
	float we=Gaus(dst+1.0,scale);
	float wf=Gaus(dst+2.0,scale);
	float wg=Gaus(dst+3.0,scale);
	// Return filtered sample.
	return (a*wa+b*wb+c*wc+d*wd+e*we+f*wf+g*wg)/(wa+wb+wc+wd+we+wf+wg);
}

// Return scanline weight
float Scan(vec2 pos,float off, vec2 texture_size) {
	float dst=Dist(pos, texture_size).y;
	return Gaus(dst+off,hardScan);
}

  // Return scanline weight for bloom
float BloomScan(vec2 pos,float off, vec2 texture_size) {
	float dst=Dist(pos, texture_size).y;
	return Gaus(dst+off,hardBloomScan);
}

// Allow nearest three lines to effect pixel
vec3 Tri(vec2 pos, vec2 texture_size){
	vec3 a=Horz3(pos,-1.0, texture_size);
	vec3 b=Horz5(pos, 0.0, texture_size);
	vec3 c=Horz3(pos, 1.0, texture_size);
	float wa=Scan(pos,-1.0, texture_size);
	float wb=Scan(pos, 0.0, texture_size);
	float wc=Scan(pos, 1.0, texture_size);
	return a*wa+b*wb+c*wc;
}

// Small bloom
vec3 Bloom(vec2 pos, vec2 texture_size) {
	vec3 a=Horz5(pos,-2.0, texture_size);
	vec3 b=Horz7(pos,-1.0, texture_size);
	vec3 c=Horz7(pos, 0.0, texture_size);
	vec3 d=Horz7(pos, 1.0, texture_size);
	vec3 e=Horz5(pos, 2.0, texture_size);
	float wa=BloomScan(pos,-2.0, texture_size);
	float wb=BloomScan(pos,-1.0, texture_size);
	float wc=BloomScan(pos, 0.0, texture_size);
	float wd=BloomScan(pos, 1.0, texture_size);
	float we=BloomScan(pos, 2.0, texture_size);
	return a*wa+b*wb+c*wc+d*wd+e*we;
}

// Distortion of scanlines, and end of screen alpha
vec2 Warp(vec2 pos) {
	pos=pos*2.0-1.0;
	pos*=vec2(1.0+(pos.y*pos.y)*warpX,1.0+(pos.x*pos.x)*warpY);
	return pos*0.5+0.5;
}

// Shadow mask
vec3 Mask(vec2 pos) {
	vec3 mask = vec3(maskDark,maskDark,maskDark);

	pos.xy=floor(pos.xy*vec2(1.0,0.5));
	pos.x+=pos.y*3.0;
	pos.x=fract(pos.x/6.0);

	if(pos.x<0.333)mask.r=maskLight;
	else if(pos.x<0.666)mask.g=maskLight;
	else mask.b=maskLight;

	return mask;
}

vec4 crt_lottes(vec2 texture_size, vec2 video_size, vec2 output_size, vec2 tex) {
	vec2 pos=Warp(tex.xy*(texture_size.xy/video_size.xy))*(video_size.xy/texture_size.xy);
	vec3 outColor = Tri(pos, texture_size);

	outColor.rgb+=Bloom(pos, texture_size)*bloomAmount;
	outColor.rgb*=Mask(floor(tex.xy*(texture_size.xy/video_size.xy)*output_size.xy)+vec2(0.5,0.5));

	return vec4(ToSrgb(outColor.rgb),1.0);
}

void fragment() {
	COLOR = crt_lottes(vTexSize, vTexSize, vViewSize, vTexCoords);
}
