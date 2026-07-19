#pragma once

/**
	@file SwShaderRuntime.h

	C++ runtime library the GLSL-to-C++ transpiler (ShaderCompiler/GlslToCpp) targets.

	The offline transpiler lowers a shader's fragment GLSL into a plain C++ function that the CPU
	software renderer calls once per pixel, so an effect runs in software exactly as its GLSL
	specifies instead of being hand-ported. That generated code is written against the small value
	types and free functions defined here: GLSL-shaped `vec2`/`vec3`/`vec4` (plus minimal integer and
	boolean vectors), the arithmetic operators and the built-in functions the shader corpus uses, a
	`swTexture()` sampler shim over @ref SwTexture and a `packColor()` writer to the rasterizer's RGBA8
	output pixel.

	Swizzle-access convention (chosen so the emitter never needs C++ lvalue swizzles):
	- A single component is a plain field: `.x`/`.y`/`.z`/`.w`, aliased through a union to `.r`/`.g`/`.b`/`.a`
	  and `.s`/`.t`/`.p`/`.q`. Because it is a real member it works as both a read and a write target, so
	  the emitter maps `v.x` and an assignment `v.x = ...` straight through.
	- A multi-component read swizzle (`v.rgb`, `v.xy`) is a const method the emitter calls: `v.rgb()`,
	  `v.xy()`. The base is evaluated once and a fresh vector is returned. Only the common/contiguous
	  swizzles are provided here (see the file tail) — the full permutation set is a
	  mechanical later completion.
	- A multi-component write swizzle (`v.xy = ...`) is NOT supported; the emitter would have to
	  decompose it into per-component field writes. No corpus shader needs it yet.
*/

#if defined(WITH_RHI_SOFTWARE)

#include "SwRaster.h"
#include "SwTexture.h"
#include "../RhiTypes.h"

#include <cmath>
#include <cstdint>

// Anonymous structs inside a union give the .x/.r/.s aliasing; they are a well-supported extension
// that MSVC flags only at /W4. Silence it just around the value types.
#if defined(_MSC_VER)
#	pragma warning(push)
#	pragma warning(disable: 4201)
#endif

namespace nCine::RhiSoftware::sw
{
	struct vec2;
	struct vec3;
	struct vec4;

	/**
		@brief Two-component float vector mirroring GLSL `vec2`

		Components are reachable as `.x`/`.y`, `.r`/`.g` or `.s`/`.t` (union-aliased); multi-component
		read swizzles are const methods (@ref xy() etc.).
	*/
	struct vec2
	{
		union {
			struct { float x, y; };
			struct { float r, g; };
			struct { float s, t; };
		};

		vec2() : x(0.0f), y(0.0f) {}
		explicit vec2(float v) : x(v), y(v) {}
		vec2(float x_, float y_) : x(x_), y(y_) {}
		// Truncating constructors from wider vectors (defined out of line once vec3/vec4 are complete)
		explicit vec2(const vec3& v);
		explicit vec2(const vec4& v);

		vec2& operator+=(const vec2& o) { x += o.x; y += o.y; return *this; }
		vec2& operator-=(const vec2& o) { x -= o.x; y -= o.y; return *this; }
		vec2& operator*=(const vec2& o) { x *= o.x; y *= o.y; return *this; }
		vec2& operator/=(const vec2& o) { x /= o.x; y /= o.y; return *this; }
		vec2& operator*=(float o) { x *= o; y *= o; return *this; }
		vec2& operator/=(float o) { x /= o; y /= o; return *this; }

		vec2 xy() const { return vec2(x, y); }
		vec2 rg() const { return vec2(x, y); }
		vec2 xx() const { return vec2(x, x); }
	};

	/**
		@brief Three-component float vector mirroring GLSL `vec3`

		Components are reachable as `.x`/`.y`/`.z`, `.r`/`.g`/`.b` or `.s`/`.t`/`.p` (union-aliased).
	*/
	struct vec3
	{
		union {
			struct { float x, y, z; };
			struct { float r, g, b; };
			struct { float s, t, p; };
		};

		vec3() : x(0.0f), y(0.0f), z(0.0f) {}
		explicit vec3(float v) : x(v), y(v), z(v) {}
		vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
		vec3(const vec2& xy_, float z_) : x(xy_.x), y(xy_.y), z(z_) {}
		vec3(float x_, const vec2& yz_) : x(x_), y(yz_.x), z(yz_.y) {}
		explicit vec3(const vec4& v);		// Truncation (defined out of line)

		vec3& operator+=(const vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
		vec3& operator-=(const vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
		vec3& operator*=(const vec3& o) { x *= o.x; y *= o.y; z *= o.z; return *this; }
		vec3& operator/=(const vec3& o) { x /= o.x; y /= o.y; z /= o.z; return *this; }
		vec3& operator*=(float o) { x *= o; y *= o; z *= o; return *this; }
		vec3& operator/=(float o) { x /= o; y /= o; z /= o; return *this; }

		vec2 xy() const { return vec2(x, y); }
		vec2 rg() const { return vec2(x, y); }
		vec2 xx() const { return vec2(x, x); }
		vec3 xyz() const { return vec3(x, y, z); }
		vec3 rgb() const { return vec3(x, y, z); }
	};

	/**
		@brief Four-component float vector mirroring GLSL `vec4`

		Components are reachable as `.x`/`.y`/`.z`/`.w`, `.r`/`.g`/`.b`/`.a` or `.s`/`.t`/`.p`/`.q`
		(union-aliased).
	*/
	struct vec4
	{
		union {
			struct { float x, y, z, w; };
			struct { float r, g, b, a; };
			struct { float s, t, p, q; };
		};

		vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
		explicit vec4(float v) : x(v), y(v), z(v), w(v) {}
		vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
		vec4(const vec3& xyz_, float w_) : x(xyz_.x), y(xyz_.y), z(xyz_.z), w(w_) {}
		vec4(float x_, const vec3& yzw_) : x(x_), y(yzw_.x), z(yzw_.y), w(yzw_.z) {}
		vec4(const vec2& xy_, const vec2& zw_) : x(xy_.x), y(xy_.y), z(zw_.x), w(zw_.y) {}
		vec4(const vec2& xy_, float z_, float w_) : x(xy_.x), y(xy_.y), z(z_), w(w_) {}
		vec4(float x_, float y_, const vec2& zw_) : x(x_), y(y_), z(zw_.x), w(zw_.y) {}

		vec4& operator+=(const vec4& o) { x += o.x; y += o.y; z += o.z; w += o.w; return *this; }
		vec4& operator-=(const vec4& o) { x -= o.x; y -= o.y; z -= o.z; w -= o.w; return *this; }
		vec4& operator*=(const vec4& o) { x *= o.x; y *= o.y; z *= o.z; w *= o.w; return *this; }
		vec4& operator/=(const vec4& o) { x /= o.x; y /= o.y; z /= o.z; w /= o.w; return *this; }
		vec4& operator*=(float o) { x *= o; y *= o; z *= o; w *= o; return *this; }
		vec4& operator/=(float o) { x /= o; y /= o; z /= o; w /= o; return *this; }

		vec2 xy() const { return vec2(x, y); }
		vec2 rg() const { return vec2(x, y); }
		vec2 xx() const { return vec2(x, x); }
		vec2 zw() const { return vec2(z, w); }
		vec2 ba() const { return vec2(z, w); }
		vec3 xyz() const { return vec3(x, y, z); }
		vec3 rgb() const { return vec3(x, y, z); }
		vec3 yzw() const { return vec3(y, z, w); }
		vec3 gba() const { return vec3(y, z, w); }
		vec4 xyzw() const { return vec4(x, y, z, w); }
		vec4 rgba() const { return vec4(x, y, z, w); }
	};

	inline vec2::vec2(const vec3& v) : x(v.x), y(v.y) {}
	inline vec2::vec2(const vec4& v) : x(v.x), y(v.y) {}
	inline vec3::vec3(const vec4& v) : x(v.x), y(v.y), z(v.z) {}

	// --- Arithmetic operators (component-wise, with scalar broadcast on either side) ---------------

#define SW_DEFINE_VEC2_OP(OP) \
	inline vec2 operator OP(const vec2& a, const vec2& b) { return vec2(a.x OP b.x, a.y OP b.y); } \
	inline vec2 operator OP(const vec2& a, float b) { return vec2(a.x OP b, a.y OP b); } \
	inline vec2 operator OP(float a, const vec2& b) { return vec2(a OP b.x, a OP b.y); }
#define SW_DEFINE_VEC3_OP(OP) \
	inline vec3 operator OP(const vec3& a, const vec3& b) { return vec3(a.x OP b.x, a.y OP b.y, a.z OP b.z); } \
	inline vec3 operator OP(const vec3& a, float b) { return vec3(a.x OP b, a.y OP b, a.z OP b); } \
	inline vec3 operator OP(float a, const vec3& b) { return vec3(a OP b.x, a OP b.y, a OP b.z); }
#define SW_DEFINE_VEC4_OP(OP) \
	inline vec4 operator OP(const vec4& a, const vec4& b) { return vec4(a.x OP b.x, a.y OP b.y, a.z OP b.z, a.w OP b.w); } \
	inline vec4 operator OP(const vec4& a, float b) { return vec4(a.x OP b, a.y OP b, a.z OP b, a.w OP b); } \
	inline vec4 operator OP(float a, const vec4& b) { return vec4(a OP b.x, a OP b.y, a OP b.z, a OP b.w); }

	SW_DEFINE_VEC2_OP(+) SW_DEFINE_VEC2_OP(-) SW_DEFINE_VEC2_OP(*) SW_DEFINE_VEC2_OP(/)
	SW_DEFINE_VEC3_OP(+) SW_DEFINE_VEC3_OP(-) SW_DEFINE_VEC3_OP(*) SW_DEFINE_VEC3_OP(/)
	SW_DEFINE_VEC4_OP(+) SW_DEFINE_VEC4_OP(-) SW_DEFINE_VEC4_OP(*) SW_DEFINE_VEC4_OP(/)

#undef SW_DEFINE_VEC2_OP
#undef SW_DEFINE_VEC3_OP
#undef SW_DEFINE_VEC4_OP

	inline vec2 operator-(const vec2& v) { return vec2(-v.x, -v.y); }
	inline vec3 operator-(const vec3& v) { return vec3(-v.x, -v.y, -v.z); }
	inline vec4 operator-(const vec4& v) { return vec4(-v.x, -v.y, -v.z, -v.w); }

	// --- Minimal integer / boolean vectors (types only; the corpus does not compute with them) -----

	struct ivec2 { union { struct { std::int32_t x, y; }; struct { std::int32_t r, g; }; };
		ivec2() : x(0), y(0) {} explicit ivec2(std::int32_t v) : x(v), y(v) {} ivec2(std::int32_t x_, std::int32_t y_) : x(x_), y(y_) {} };
	struct ivec3 { union { struct { std::int32_t x, y, z; }; struct { std::int32_t r, g, b; }; };
		ivec3() : x(0), y(0), z(0) {} explicit ivec3(std::int32_t v) : x(v), y(v), z(v) {} ivec3(std::int32_t x_, std::int32_t y_, std::int32_t z_) : x(x_), y(y_), z(z_) {} };
	struct ivec4 { union { struct { std::int32_t x, y, z, w; }; struct { std::int32_t r, g, b, a; }; };
		ivec4() : x(0), y(0), z(0), w(0) {} explicit ivec4(std::int32_t v) : x(v), y(v), z(v), w(v) {} ivec4(std::int32_t x_, std::int32_t y_, std::int32_t z_, std::int32_t w_) : x(x_), y(y_), z(z_), w(w_) {} };

	struct bvec2 { union { struct { bool x, y; }; struct { bool r, g; }; };
		bvec2() : x(false), y(false) {} explicit bvec2(bool v) : x(v), y(v) {} bvec2(bool x_, bool y_) : x(x_), y(y_) {} };
	struct bvec3 { union { struct { bool x, y, z; }; struct { bool r, g, b; }; };
		bvec3() : x(false), y(false), z(false) {} explicit bvec3(bool v) : x(v), y(v), z(v) {} bvec3(bool x_, bool y_, bool z_) : x(x_), y(y_), z(z_) {} };
	struct bvec4 { union { struct { bool x, y, z, w; }; struct { bool r, g, b, a; }; };
		bvec4() : x(false), y(false), z(false), w(false) {} explicit bvec4(bool v) : x(v), y(v), z(v), w(v) {} bvec4(bool x_, bool y_, bool z_, bool w_) : x(x_), y(y_), z(z_), w(w_) {} };

	// --- Built-in functions -------------------------------------------------------------------------
	//
	// Scalar overloads first, then component-wise vector overloads. GLSL semantics are matched exactly
	// (e.g. mod(x, y) = x - y * floor(x / y), and the geometric functions operate on the whole vector).

	inline float radians(float d) { return d * 0.01745329251994329577f; }
	inline float degrees(float r) { return r * 57.2957795130823208768f; }
	inline float sin(float x) { return std::sin(x); }
	inline float cos(float x) { return std::cos(x); }
	inline float tan(float x) { return std::tan(x); }
	inline float asin(float x) { return std::asin(x); }
	inline float acos(float x) { return std::acos(x); }
	inline float atan(float x) { return std::atan(x); }
	inline float atan(float y, float x) { return std::atan2(y, x); }
	inline float pow(float x, float y) { return std::pow(x, y); }
	inline float exp2(float x) { return std::exp2(x); }
	inline float log2(float x) { return std::log2(x); }
	inline float sqrt(float x) { return std::sqrt(x); }
	inline float inversesqrt(float x) { return 1.0f / std::sqrt(x); }
	inline float abs(float x) { return std::fabs(x); }
	inline float sign(float x) { return (x > 0.0f) ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }
	inline float floor(float x) { return std::floor(x); }
	inline float ceil(float x) { return std::ceil(x); }
	inline float round(float x) { return std::floor(x + 0.5f); }		// GLSL round is unspecified at .5; match the shader idiom floor(x + 0.5)
	inline float fract(float x) { return x - std::floor(x); }
	inline float mod(float x, float y) { return x - y * std::floor(x / y); }
	inline float min(float x, float y) { return (x < y) ? x : y; }
	inline float max(float x, float y) { return (x > y) ? x : y; }
	inline float clamp(float x, float lo, float hi) { return (x < lo) ? lo : (x > hi ? hi : x); }
	inline float mix(float a, float b, float t) { return a * (1.0f - t) + b * t; }
	inline float step(float edge, float x) { return (x < edge) ? 0.0f : 1.0f; }
	inline float smoothstep(float e0, float e1, float x) {
		float t = clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// Component-wise unary vector wrappers over the scalar built-ins
#define SW_UNARY(func) \
	inline vec2 func(const vec2& v) { return vec2(func(v.x), func(v.y)); } \
	inline vec3 func(const vec3& v) { return vec3(func(v.x), func(v.y), func(v.z)); } \
	inline vec4 func(const vec4& v) { return vec4(func(v.x), func(v.y), func(v.z), func(v.w)); }
	SW_UNARY(sin) SW_UNARY(cos) SW_UNARY(tan) SW_UNARY(asin) SW_UNARY(acos) SW_UNARY(atan)
	SW_UNARY(exp2) SW_UNARY(log2) SW_UNARY(sqrt) SW_UNARY(inversesqrt) SW_UNARY(abs) SW_UNARY(sign)
	SW_UNARY(floor) SW_UNARY(ceil) SW_UNARY(round) SW_UNARY(fract) SW_UNARY(radians) SW_UNARY(degrees)
#undef SW_UNARY

	// Component-wise binary vector wrappers (vec/vec and vec/scalar), matching GLSL's genType rules
#define SW_BINARY(func) \
	inline vec2 func(const vec2& a, const vec2& b) { return vec2(func(a.x, b.x), func(a.y, b.y)); } \
	inline vec3 func(const vec3& a, const vec3& b) { return vec3(func(a.x, b.x), func(a.y, b.y), func(a.z, b.z)); } \
	inline vec4 func(const vec4& a, const vec4& b) { return vec4(func(a.x, b.x), func(a.y, b.y), func(a.z, b.z), func(a.w, b.w)); } \
	inline vec2 func(const vec2& a, float b) { return vec2(func(a.x, b), func(a.y, b)); } \
	inline vec3 func(const vec3& a, float b) { return vec3(func(a.x, b), func(a.y, b), func(a.z, b)); } \
	inline vec4 func(const vec4& a, float b) { return vec4(func(a.x, b), func(a.y, b), func(a.z, b), func(a.w, b)); }
	SW_BINARY(pow) SW_BINARY(mod) SW_BINARY(min) SW_BINARY(max)
#undef SW_BINARY

	// step(edge, x): GLSL also broadcasts a scalar edge over a vector x
	inline vec2 step(const vec2& e, const vec2& x) { return vec2(step(e.x, x.x), step(e.y, x.y)); }
	inline vec3 step(const vec3& e, const vec3& x) { return vec3(step(e.x, x.x), step(e.y, x.y), step(e.z, x.z)); }
	inline vec4 step(const vec4& e, const vec4& x) { return vec4(step(e.x, x.x), step(e.y, x.y), step(e.z, x.z), step(e.w, x.w)); }
	inline vec2 step(float e, const vec2& x) { return vec2(step(e, x.x), step(e, x.y)); }
	inline vec3 step(float e, const vec3& x) { return vec3(step(e, x.x), step(e, x.y), step(e, x.z)); }
	inline vec4 step(float e, const vec4& x) { return vec4(step(e, x.x), step(e, x.y), step(e, x.z), step(e, x.w)); }

	// clamp(v, float, float) and clamp(v, v, v)
	inline vec2 clamp(const vec2& v, float lo, float hi) { return vec2(clamp(v.x, lo, hi), clamp(v.y, lo, hi)); }
	inline vec3 clamp(const vec3& v, float lo, float hi) { return vec3(clamp(v.x, lo, hi), clamp(v.y, lo, hi), clamp(v.z, lo, hi)); }
	inline vec4 clamp(const vec4& v, float lo, float hi) { return vec4(clamp(v.x, lo, hi), clamp(v.y, lo, hi), clamp(v.z, lo, hi), clamp(v.w, lo, hi)); }
	inline vec2 clamp(const vec2& v, const vec2& lo, const vec2& hi) { return vec2(clamp(v.x, lo.x, hi.x), clamp(v.y, lo.y, hi.y)); }
	inline vec3 clamp(const vec3& v, const vec3& lo, const vec3& hi) { return vec3(clamp(v.x, lo.x, hi.x), clamp(v.y, lo.y, hi.y), clamp(v.z, lo.z, hi.z)); }
	inline vec4 clamp(const vec4& v, const vec4& lo, const vec4& hi) { return vec4(clamp(v.x, lo.x, hi.x), clamp(v.y, lo.y, hi.y), clamp(v.z, lo.z, hi.z), clamp(v.w, lo.w, hi.w)); }

	// mix(v, v, float) and mix(v, v, v)
	inline vec2 mix(const vec2& a, const vec2& b, float t) { return vec2(mix(a.x, b.x, t), mix(a.y, b.y, t)); }
	inline vec3 mix(const vec3& a, const vec3& b, float t) { return vec3(mix(a.x, b.x, t), mix(a.y, b.y, t), mix(a.z, b.z, t)); }
	inline vec4 mix(const vec4& a, const vec4& b, float t) { return vec4(mix(a.x, b.x, t), mix(a.y, b.y, t), mix(a.z, b.z, t), mix(a.w, b.w, t)); }
	inline vec2 mix(const vec2& a, const vec2& b, const vec2& t) { return vec2(mix(a.x, b.x, t.x), mix(a.y, b.y, t.y)); }
	inline vec3 mix(const vec3& a, const vec3& b, const vec3& t) { return vec3(mix(a.x, b.x, t.x), mix(a.y, b.y, t.y), mix(a.z, b.z, t.z)); }
	inline vec4 mix(const vec4& a, const vec4& b, const vec4& t) { return vec4(mix(a.x, b.x, t.x), mix(a.y, b.y, t.y), mix(a.z, b.z, t.z), mix(a.w, b.w, t.w)); }

	// smoothstep(float, float, v) and smoothstep(v, v, v)
	inline vec2 smoothstep(float e0, float e1, const vec2& x) { return vec2(smoothstep(e0, e1, x.x), smoothstep(e0, e1, x.y)); }
	inline vec3 smoothstep(float e0, float e1, const vec3& x) { return vec3(smoothstep(e0, e1, x.x), smoothstep(e0, e1, x.y), smoothstep(e0, e1, x.z)); }
	inline vec4 smoothstep(float e0, float e1, const vec4& x) { return vec4(smoothstep(e0, e1, x.x), smoothstep(e0, e1, x.y), smoothstep(e0, e1, x.z), smoothstep(e0, e1, x.w)); }

	// Geometric functions
	inline float dot(const vec2& a, const vec2& b) { return a.x * b.x + a.y * b.y; }
	inline float dot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline float dot(const vec4& a, const vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
	inline vec3 cross(const vec3& a, const vec3& b) { return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
	inline float length(const vec2& v) { return std::sqrt(dot(v, v)); }
	inline float length(const vec3& v) { return std::sqrt(dot(v, v)); }
	inline float length(const vec4& v) { return std::sqrt(dot(v, v)); }
	inline float distance(const vec2& a, const vec2& b) { return length(a - b); }
	inline float distance(const vec3& a, const vec3& b) { return length(a - b); }
	inline float distance(const vec4& a, const vec4& b) { return length(a - b); }
	inline vec2 normalize(const vec2& v) { return v * (1.0f / length(v)); }
	inline vec3 normalize(const vec3& v) { return v * (1.0f / length(v)); }
	inline vec4 normalize(const vec4& v) { return v * (1.0f / length(v)); }
	inline vec2 reflect(const vec2& i, const vec2& n) { return i - n * (2.0f * dot(n, i)); }
	inline vec3 reflect(const vec3& i, const vec3& n) { return i - n * (2.0f * dot(n, i)); }
	inline vec4 reflect(const vec4& i, const vec4& n) { return i - n * (2.0f * dot(n, i)); }

	// --- Screen-space derivatives (approximated) ----------------------------------------------------
	//
	// The per-pixel CPU path shades one pixel at a time with no 2x2 quad, so true screen-space
	// derivatives are unavailable. Shaders use them only to widen an anti-aliasing edge (the aastep/fwidth
	// idiom: smoothstep(t - fw, t + fw, v)), so a small constant is returned instead of 0 - that keeps the
	// edge soft but finite (avoiding a divide-by-zero in smoothstep) while letting derivative-based effects
	// (e.g. the frozen-enemy mask outline) run at approximate fidelity. The magnitude (~1/50 in value space)
	// is a rough stand-in for the per-pixel rate of change of a quantity that spans a typical sprite; the
	// exact value only affects edge softness.
	inline float dFdx(float) { return 0.02f; }
	inline float dFdy(float) { return 0.02f; }
	inline vec2 dFdx(const vec2&) { return vec2(0.02f); }
	inline vec2 dFdy(const vec2&) { return vec2(0.02f); }
	inline vec3 dFdx(const vec3&) { return vec3(0.02f); }
	inline vec3 dFdy(const vec3&) { return vec3(0.02f); }
	inline vec4 dFdx(const vec4&) { return vec4(0.02f); }
	inline vec4 dFdy(const vec4&) { return vec4(0.02f); }
	inline float fwidth(float) { return 0.04f; }		// abs(dFdx) + abs(dFdy)
	inline vec2 fwidth(const vec2&) { return vec2(0.04f); }
	inline vec3 fwidth(const vec3&) { return vec3(0.04f); }
	inline vec4 fwidth(const vec4&) { return vec4(0.04f); }

	// --- Sampler shim -------------------------------------------------------------------------------

	namespace detail
	{
		/** Wraps a signed integer texel coordinate into `[0, dim)`, mirroring SwRaster's WrapTexelCoord */
		inline std::int32_t wrapTexel(std::int32_t idx, std::int32_t dim, nCine::SamplerWrapping mode)
		{
			// Fast path: an in-range coordinate maps to itself under every wrap mode, so the vast
			// majority of samples skip the mode switch (and Repeat's integer division) entirely
			if (static_cast<std::uint32_t>(idx) < static_cast<std::uint32_t>(dim)) {
				return idx;
			}
			switch (mode) {
				case nCine::SamplerWrapping::Repeat:
					idx %= dim;
					if (idx < 0) idx += dim;
					return idx;
				case nCine::SamplerWrapping::MirroredRepeat: {
					std::int32_t period = dim * 2;
					idx %= period;
					if (idx < 0) idx += period;
					if (idx >= dim) idx = period - 1 - idx;
					return idx;
				}
				default:	// ClampToEdge (and Unknown)
					return (idx < 0) ? 0 : (idx >= dim ? dim - 1 : idx);
			}
		}

		/**
			@brief Floor-to-integer without the `std::floor` library call

			Bit-identical to `static_cast<std::int32_t>(std::floor(f))` for every value on which that
			conversion is defined (any `f` whose floor fits in `int32`): truncate toward zero, then step
			down one when truncation rounded up (negative non-integers). The `INT32_MIN` guard keeps the
			already-out-of-range garbage inputs (a domain the old expression handled as UB) from
			overflowing the adjustment.
		*/
		inline std::int32_t floorToInt(float f)
		{
			const std::int32_t i = static_cast<std::int32_t>(f);
			return (static_cast<float>(i) > f && i != INT32_MIN) ? i - 1 : i;
		}
	}

	/**
	 * @brief Samples a bound texture, the software counterpart of GLSL `texture(sampler, uv)`
	 *
	 * Reads the texture bound to @p unit of @p in with nearest filtering, honoring the texture's wrap
	 * mode, and returns its texel as normalized `0..1` RGBA. Since @ref SwTexture promotes R8/RG8/RGB8
	 * stores to RGBA8, every store is treated as four bytes. An unbound unit or empty store yields
	 * transparent black.
	 */
	inline vec4 swTexture(const FragmentShaderInput& in, int unit, const vec2& uv)
	{
		if (unit < 0 || unit >= static_cast<int>(MaxTextureUnits)) {
			return vec4(0.0f);
		}
		const SwTexture* tex = in.textures[unit];
		if (tex == nullptr) {
			return vec4(0.0f);
		}
		const std::uint8_t* pixels = tex->GetPixels(0);
		std::int32_t width = tex->GetWidth();
		std::int32_t height = tex->GetHeight();
		if (pixels == nullptr || width <= 0 || height <= 0) {
			return vec4(0.0f);
		}
		std::int32_t stride = tex->GetStrideBytes();
		std::int32_t tx = detail::wrapTexel(detail::floorToInt(uv.x * static_cast<float>(width)), width, tex->GetWrapS());
		std::int32_t ty = detail::wrapTexel(detail::floorToInt(uv.y * static_cast<float>(height)), height, tex->GetWrapT());
		const std::uint8_t* texel = pixels + static_cast<std::size_t>(ty) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(tx) * 4;

		// Honor the texture's sampling swizzle, exactly as GLSL texture() does. It is identity for normal
		// textures, so a four-way compare short-circuits the per-channel switch on the hot path; only the
		// palette-index RG8 textures take the generic remap below (their swizzle maps the sampled `.a` to
		// the green channel - the packed per-pixel alpha - the same source the hand-ported palette effect
		// reads. Without this the promoted RGBA8 store's opaque byte-3 would drop that alpha).
		//
		// NOTE: the `/ 255.0f` byte normalization is kept verbatim (no reciprocal multiply, no lookup
		// table): the Debug (/fp:precise) build compiles it as a true division while the Release
		// (/fp:fast) build already rewrites it into `* (1/255)` on its own, and the two differ in the
		// last bit for 126 of the 256 byte values - so any manual substitution would break bit-parity
		// with the current output in one of the two float models.
		const nCine::SwizzleChannel* swizzle = tex->GetSwizzle();
		if (swizzle[0] == nCine::SwizzleChannel::Red && swizzle[1] == nCine::SwizzleChannel::Green &&
		    swizzle[2] == nCine::SwizzleChannel::Blue && swizzle[3] == nCine::SwizzleChannel::Alpha) {
			return vec4(texel[0] / 255.0f, texel[1] / 255.0f, texel[2] / 255.0f, texel[3] / 255.0f);
		}
		auto pickChannel = [texel](nCine::SwizzleChannel channel) -> float {
			switch (channel) {
				case nCine::SwizzleChannel::Red:	return texel[0] / 255.0f;
				case nCine::SwizzleChannel::Green:	return texel[1] / 255.0f;
				case nCine::SwizzleChannel::Blue:	return texel[2] / 255.0f;
				case nCine::SwizzleChannel::Alpha:	return texel[3] / 255.0f;
				case nCine::SwizzleChannel::Zero:	return 0.0f;
				case nCine::SwizzleChannel::One:	return 1.0f;
			}
			return 0.0f;
		};
		return vec4(pickChannel(swizzle[0]), pickChannel(swizzle[1]), pickChannel(swizzle[2]), pickChannel(swizzle[3]));
	}

	/**
	 * @brief Reads the primary texel the rasterizer already sampled, the fast equivalent of
	 *        `swTexture(in, unit, vec2(in.u, in.v))`
	 *
	 * The transpiler emits this for the exact `texture(uTexture, vTexCoords)` pattern - the sampler the
	 * device keys the gather on (`FFState::textureUnit` is set from the same reflected `uTexture` unit
	 * baked into @p unit here), sampled at the unmodified interpolated texcoord. For that pattern the
	 * rasterizer's gather phase has already fetched the texel of this pixel into `in.rgba` (raw store
	 * bytes, before the sampling swizzle), so the fragment reads it back - applying the unit's swizzle -
	 * instead of re-deriving the texel address and re-fetching through the full sampler. For a
	 * bilinear-filtered texture the gather holds the FILTERED sample - exactly what GL's `texture()`
	 * returns there - so reading it back is what keeps `texture(uTexture, vTexCoords)` bilinear on the
	 * software backend (the generic @ref swTexture samples nearest).
	 *
	 * Falls back to the generic @ref swTexture only when `in.rgba` is not guaranteed to hold that
	 * sample: an unbound unit or empty store (the gather substitutes opaque white where `swTexture`
	 * yields transparent black).
	 */
	inline vec4 swTexturePrimary(const FragmentShaderInput& in, int unit)
	{
		const SwTexture* tex = (unit >= 0 && unit < static_cast<int>(MaxTextureUnits) ? in.textures[unit] : nullptr);
		if (tex != nullptr && tex->GetPixels(0) != nullptr) {
			const std::int32_t width = tex->GetWidth();
			const std::int32_t height = tex->GetHeight();
			if (width > 0 && height > 0) {
				const std::uint8_t* texel = in.rgba;
				// Same swizzle handling (and the same verbatim `/ 255.0f`) as swTexture above
				const nCine::SwizzleChannel* swizzle = tex->GetSwizzle();
				if (swizzle[0] == nCine::SwizzleChannel::Red && swizzle[1] == nCine::SwizzleChannel::Green &&
				    swizzle[2] == nCine::SwizzleChannel::Blue && swizzle[3] == nCine::SwizzleChannel::Alpha) {
					return vec4(texel[0] / 255.0f, texel[1] / 255.0f, texel[2] / 255.0f, texel[3] / 255.0f);
				}
				auto pickChannel = [texel](nCine::SwizzleChannel channel) -> float {
					switch (channel) {
						case nCine::SwizzleChannel::Red:	return texel[0] / 255.0f;
						case nCine::SwizzleChannel::Green:	return texel[1] / 255.0f;
						case nCine::SwizzleChannel::Blue:	return texel[2] / 255.0f;
						case nCine::SwizzleChannel::Alpha:	return texel[3] / 255.0f;
						case nCine::SwizzleChannel::Zero:	return 0.0f;
						case nCine::SwizzleChannel::One:	return 1.0f;
					}
					return 0.0f;
				};
				return vec4(pickChannel(swizzle[0]), pickChannel(swizzle[1]), pickChannel(swizzle[2]), pickChannel(swizzle[3]));
			}
		}
		return swTexture(in, unit, vec2(in.u, in.v));
	}

	/**
	 * @brief Writes a straight-alpha `0..1` color to the rasterizer's 4-byte RGBA output pixel
	 *
	 * Each channel is clamped to `[0, 1]` and rounded to the nearest of 256 levels, matching the byte
	 * quantization the fixed-function paths perform.
	 */
	inline void packColor(const vec4& c, std::uint8_t* rgba)
	{
		auto quantize = [](float v) -> std::uint8_t {
			v = (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
			std::int32_t i = static_cast<std::int32_t>(v * 255.0f + 0.5f);
			return static_cast<std::uint8_t>((i < 0) ? 0 : (i > 255 ? 255 : i));
		};
		rgba[0] = quantize(c.r);
		rgba[1] = quantize(c.g);
		rgba[2] = quantize(c.b);
		rgba[3] = quantize(c.a);
	}
}

#if defined(_MSC_VER)
#	pragma warning(pop)
#endif

#endif
