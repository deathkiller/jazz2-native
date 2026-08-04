#pragma once

#include "RhiTypes.h"
#include "RhiFwd.h"

// Definitions of the classes aliased in `RhiFwd.h` for the selected backend

#if defined(WITH_RHI_GL)

#include "GL/GLDevice.h"
#include "GL/GLTexture.h"
#include "GL/GLBufferObject.h"
#include "GL/GLShader.h"
#include "GL/GLShaderProgram.h"
#include "GL/GLShaderUniforms.h"
#include "GL/GLShaderUniformBlocks.h"
#include "GL/GLUniform.h"
#include "GL/GLUniformBlock.h"
#include "GL/GLUniformCache.h"
#include "GL/GLUniformBlockCache.h"
#include "GL/GLAttribute.h"
#include "GL/GLFramebuffer.h"
#include "GL/GLRenderbuffer.h"
#include "GL/GLRenderTarget.h"
#include "GL/GLVertexArrayObject.h"
#include "GL/GLVertexFormat.h"
#include "GL/GLBlending.h"
#include "GL/GLDepthTest.h"
#include "GL/GLCullFace.h"
#include "GL/GLScissorTest.h"
#include "GL/GLClearColor.h"
#include "GL/GLViewport.h"
#include "GL/GLDebug.h"
#include "GL/GLRhiCapabilities.h"

#elif defined(WITH_RHI_SOFTWARE)

#include "Software/SwDebug.h"
#include "Software/SwShader.h"
#include "Software/SwBuffer.h"
#include "Software/SwTexture.h"
#include "Software/SwVertexFormat.h"
#include "Software/SwShaderTypes.h"
#include "Software/SwUniformCache.h"
#include "Software/SwShaderUniforms.h"
#include "Software/SwShaderProgram.h"
#include "Software/SwRenderTarget.h"
#include "Software/SwRaster.h"
#include "Software/SwDevice.h"
#include "Software/SwRhiCapabilities.h"

#elif defined(WITH_RHI_GX)

#include "GX/GxDebug.h"
#include "GX/GxShader.h"
#include "GX/GxBuffer.h"
#include "GX/GxTexture.h"
#include "GX/GxVertexFormat.h"
#include "GX/GxShaderTypes.h"
#include "GX/GxUniformCache.h"
#include "GX/GxShaderUniforms.h"
#include "GX/GxShaderProgram.h"
#include "GX/GxRenderTarget.h"
#include "GX/GxDevice.h"
#include "GX/GxRhiCapabilities.h"

#elif defined(WITH_RHI_PVR)

#include "PVR/PvrDebug.h"
#include "PVR/PvrShader.h"
#include "PVR/PvrBuffer.h"
#include "PVR/PvrTexture.h"
#include "PVR/PvrVertexFormat.h"
#include "PVR/PvrShaderTypes.h"
#include "PVR/PvrUniformCache.h"
#include "PVR/PvrShaderUniforms.h"
#include "PVR/PvrShaderProgram.h"
#include "PVR/PvrRenderTarget.h"
#include "PVR/PvrDevice.h"
#include "PVR/PvrRhiCapabilities.h"

#elif defined(WITH_RHI_GU)

#include "GU/GuDebug.h"
#include "GU/GuShader.h"
#include "GU/GuBuffer.h"
#include "GU/GuTexture.h"
#include "GU/GuVertexFormat.h"
#include "GU/GuShaderTypes.h"
#include "GU/GuUniformCache.h"
#include "GU/GuShaderUniforms.h"
#include "GU/GuShaderProgram.h"
#include "GU/GuRenderTarget.h"
#include "GU/GuDevice.h"
#include "GU/GuRhiCapabilities.h"

#elif defined(WITH_RHI_GXM)

#include "GXM/GxmDebug.h"
#include "GXM/GxmShader.h"
#include "GXM/GxmMemory.h"
#include "GXM/GxmBufferObject.h"
#include "GXM/GxmTexture.h"
#include "GXM/GxmVertexFormat.h"
#include "GXM/GxmShaderTypes.h"
#include "GXM/GxmUniformCache.h"
#include "GXM/GxmShaderUniforms.h"
#include "GXM/GxmShaderProgram.h"
#include "GXM/GxmRenderTarget.h"
#include "GXM/GxmDevice.h"
#include "GXM/GxmRhiCapabilities.h"

#elif defined(WITH_RHI_D3D11)

#include "D3D11/D3D11Debug.h"
#include "D3D11/D3D11Shader.h"
#include "D3D11/D3D11BufferObject.h"
#include "D3D11/D3D11Texture.h"
#include "D3D11/D3D11VertexFormat.h"
#include "D3D11/D3D11ShaderTypes.h"
#include "D3D11/D3D11UniformCache.h"
#include "D3D11/D3D11ShaderUniforms.h"
#include "D3D11/D3D11ShaderProgram.h"
#include "D3D11/D3D11RenderTarget.h"
#include "D3D11/D3D11Device.h"
#include "D3D11/D3D11RhiCapabilities.h"

#elif defined(WITH_RHI_VULKAN)

#include "Vulkan/VulkanDebug.h"
#include "Vulkan/VulkanShader.h"
#include "Vulkan/VulkanBufferObject.h"
#include "Vulkan/VulkanTexture.h"
#include "Vulkan/VulkanVertexFormat.h"
#include "Vulkan/VulkanShaderTypes.h"
#include "Vulkan/VulkanUniformCache.h"
#include "Vulkan/VulkanShaderUniforms.h"
#include "Vulkan/VulkanShaderProgram.h"
#include "Vulkan/VulkanRenderTarget.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanRhiCapabilities.h"

#endif
