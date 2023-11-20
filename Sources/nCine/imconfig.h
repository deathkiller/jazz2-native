#pragma once

#include "../nCine/Primitives/Vector2.h"
#include "../nCine/Primitives/Vector4.h"
#include "../nCine/Primitives/Colorf.h"

#define IMGUI_API

#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_DISABLE_OBSOLETE_KEYIO
#define IMGUI_DISABLE_DEMO_WINDOWS
#define IMGUI_DISABLE_DEBUG_TOOLS // (this was called IMGUI_DISABLE_METRICS_WINDOW before 1.88).

#define IM_VEC2_CLASS_EXTRA \
	constexpr ImVec2(const nCine::Vector2f &f) : x(f.X), y(f.Y) {} \
	operator nCine::Vector2f() const { return nCine::Vector2f(x, y); }

#define IM_VEC4_CLASS_EXTRA \
	constexpr ImVec4(const nCine::Vector4f &f) : x(f.X), y(f.Y), z(f.Z), w(f.W) {} \
	operator nCine::Vector4f() const { return nCine::Vector4f(x, y, z, w); } \
	\
	ImVec4(const nCine::Colorf &c) : x(c.R), y(c.G), z(c.B), w(c.A) {} \
	operator nCine::Colorf() const { return nCine::Colorf(x, y, z, w); }
