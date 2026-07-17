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

#endif
