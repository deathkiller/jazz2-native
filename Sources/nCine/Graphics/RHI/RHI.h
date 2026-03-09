#pragma once

// This header selects the active rendering backend at compile time.
// Exactly one backend is compiled per build; the selection is driven by a
// CMake option (NCINE_RHI) which adds the corresponding preprocessor define.
//
// Available backend defines:
//   WITH_RHI_GL    — OpenGL / OpenGL ES (default desktop / mobile)
//   WITH_RHI_SW    — Pure software renderer (low-end / retro / desktop preview)
//   WITH_RHI_DX11  — Direct3D 11 (Windows)
//   WITH_RHI_VK    — Vulkan (future)
//   WITH_RHI_METAL — Metal (Apple, future)
//   WITH_RHI_DC    — Sega Dreamcast / KallistiOS PowerVR2
//   WITH_RHI_N64   — Nintendo 64 / libdragon rdpq
//   WITH_RHI_PS1   — PlayStation 1 / PSn00bSDK GPU
//   WITH_RHI_SAT   — Sega Saturn / libyaul VDP1
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

#if defined(WITH_RHI_GL)
#	include "GL/RHI_GL.h"
#elif defined(WITH_RHI_SW)
#	include "SW/RHI_SW.h"
#elif defined(WITH_RHI_DX11)
#	include "DX11/RHI_DX11.h"
#elif defined(WITH_RHI_VK)
#	include "VK/RHI_VK.h"
#elif defined(WITH_RHI_METAL)
#	include "Metal/RHI_Metal.h"
#elif defined(WITH_RHI_DC)
#	include "DC/RHI_DC.h"
#elif defined(WITH_RHI_N64)
#	include "N64/RHI_N64.h"
#elif defined(WITH_RHI_PS1)
#	include "PS1/RHI_PS1.h"
#elif defined(WITH_RHI_SAT)
#	include "SAT/RHI_SAT.h"
#else
#	error "No graphics API backend selected. Please define one of: WITH_RHI_GL, WITH_RHI_SW, WITH_RHI_DX11, WITH_RHI_VK, WITH_RHI_METAL, WITH_RHI_DC, WITH_RHI_N64, WITH_RHI_PS1, WITH_RHI_SAT"
#endif
