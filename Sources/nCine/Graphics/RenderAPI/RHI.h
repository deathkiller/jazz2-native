#pragma once

// This header selects the active rendering backend at compile time.
// Exactly one backend is compiled per build; the selection is driven by a
// CMake option (NCINE_RHI) which adds the corresponding preprocessor define.
//
// Available backend defines:
//   RHI_BACKEND_GL    — OpenGL / OpenGL ES (default desktop / mobile)
//   RHI_BACKEND_SW    — Pure software renderer (low-end / retro / desktop preview)
//   RHI_BACKEND_DX11  — Direct3D 11 (Windows)
//   RHI_BACKEND_VK    — Vulkan (future)
//   RHI_BACKEND_METAL — Metal (Apple, future)
//   RHI_BACKEND_DC    — Sega Dreamcast / KallistiOS PowerVR2
//   RHI_BACKEND_N64   — Nintendo 64 / libdragon rdpq
//   RHI_BACKEND_PS1   — PlayStation 1 / PSn00bSDK GPU
//   RHI_BACKEND_SAT   — Sega Saturn / libyaul VDP1
//
// Each backend header must:
//   1. Define capability flags (RHI_CAP_SHADERS, RHI_CAP_FRAMEBUFFERS, …)
//   2. Provide concrete types inside the GAPI namespace:
//        Buffer, Texture, Program, ShaderUniforms, ShaderUniformBlocks,
//        UniformCache, UniformBlockCache, VertexFormat, Framebuffer,
//        Renderbuffer  (optional, only when RHI_CAP_FRAMEBUFFERS)
//   3. Provide free functions inside the GAPI namespace:
//        Draw, DrawIndexed, (instanced variants if RHI_CAP_INSTANCING)
//        render-state helpers (SetBlending, SetDepthTest, …)
//        buffer / texture factory helpers

#include "RenderTypes.h"

#if defined(RHI_BACKEND_GL)
#	include "GL/RHI_GL.h"
#elif defined(RHI_BACKEND_SW)
#	include "SW/RHI_SW.h"
#elif defined(RHI_BACKEND_DX11)
#	include "DX11/RHI_DX11.h"
#elif defined(RHI_BACKEND_VK)
#	include "VK/RHI_VK.h"
#elif defined(RHI_BACKEND_METAL)
#	include "Metal/RHI_Metal.h"
#elif defined(RHI_BACKEND_DC)
#	include "DC/RHI_DC.h"
#elif defined(RHI_BACKEND_N64)
#	include "N64/RHI_N64.h"
#elif defined(RHI_BACKEND_PS1)
#	include "PS1/RHI_PS1.h"
#elif defined(RHI_BACKEND_SAT)
#	include "SAT/RHI_SAT.h"
#else
#	error "No graphics API backend selected. Please define one of: RHI_BACKEND_GL, RHI_BACKEND_SW, RHI_BACKEND_DX11, RHI_BACKEND_VK, RHI_BACKEND_METAL, RHI_BACKEND_DC, RHI_BACKEND_N64, RHI_BACKEND_PS1, RHI_BACKEND_SAT"
#endif
