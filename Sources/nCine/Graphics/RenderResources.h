#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../CommonHeaders.h"
#endif

#include "Material.h"
#include "../Primitives/Matrix4x4.h"
#include "RHI/Rhi.h"
#include "../Base/HashMap.h"

#include <memory>

namespace nCine
{
	class BinaryShaderCache;
	class RenderBuffersManager;
	class RenderVaoPool;
	class RenderCommandPool;
	class RenderBatcher;
	class Camera;
	class Viewport;

	/**
		@brief Creates and holds the OpenGL rendering resources shared by the whole application
		
		Owns the global singletons of the render pipeline (binary shader cache, buffers manager,
		VAO pool, render command pool, batcher), the built-in default shader programs and the
		default camera. Lifetime is driven by @ref Application; it cannot be instantiated.
	*/
	class RenderResources
	{
		/** @brief The `Application` class needs to create and dispose the resources */
		friend class Application;
		/** @brief The `Viewport` class needs to set the current camera */
		friend class Viewport;
		/** @brief The `ScreenViewport` class needs to change the projection of the default camera */
		friend class ScreenViewport;

	public:
		RenderResources() = delete;
		~RenderResources() = delete;

		/** @brief Vertex format for vertices with positions only */
		struct VertexFormatPos2
		{
			GLfloat position[2];
		};

		/** @brief Vertex format for vertices with positions and texture coordinates */
		struct VertexFormatPos2Tex2
		{
			GLfloat position[2];
			GLfloat texcoords[2];
		};

		/** @brief Vertex format for vertices with positions and draw indices */
		struct VertexFormatPos2Index
		{
			GLfloat position[2];
			std::int32_t drawindex;
		};

		/** @brief Vertex format for vertices with positions, texture coordinates and draw indices */
		struct VertexFormatPos2Tex2Index
		{
			GLfloat position[2];
			GLfloat texcoords[2];
			std::int32_t drawindex;
		};

		/** @brief Per-shader camera uniform data and the frames its matrices were last updated */
		struct CameraUniformData
		{
			CameraUniformData()
				: camera(nullptr), updateFrameProjectionMatrix(0), updateFrameViewMatrix(0) {}

			Rhi::ShaderUniforms shaderUniforms;
			Camera* camera;
			std::uint32_t updateFrameProjectionMatrix;
			std::uint32_t updateFrameViewMatrix;
		};

		static inline BinaryShaderCache& GetBinaryShaderCache() {
			return *binaryShaderCache_;
		}
		static inline RenderBuffersManager& GetBuffersManager() {
			return *buffersManager_;
		}
		static inline RenderVaoPool& GetVaoPool() {
			return *vaoPool_;
		}
		static inline RenderCommandPool& GetRenderCommandPool() {
			return *renderCommandPool_;
		}
		static inline RenderBatcher& GetRenderBatcher() {
			return *renderBatcher_;
		}

		static Rhi::ShaderProgram* GetShaderProgram(Material::ShaderProgramType shaderProgramType);

		static Rhi::ShaderProgram* GetBatchedShader(const Rhi::ShaderProgram* shader);
		static bool RegisterBatchedShader(const Rhi::ShaderProgram* shader, Rhi::ShaderProgram* batchedShader);
		static bool UnregisterBatchedShader(const Rhi::ShaderProgram* shader);

		static inline std::uint8_t* GetCameraUniformsBuffer() {
			return cameraUniformsBuffer_;
		}
		static CameraUniformData* FindCameraUniformData(Rhi::ShaderProgram* shaderProgram);
		static void InsertCameraUniformData(Rhi::ShaderProgram* shaderProgram, CameraUniformData&& cameraUniformData);
		static bool RemoveCameraUniformData(Rhi::ShaderProgram* shaderProgram);

		static inline const Camera* GetCurrentCamera() {
			return currentCamera_;
		}
		static inline const Viewport* GetCurrentViewport() {
			return currentViewport_;
		}

		static void SetDefaultAttributesParameters(Rhi::ShaderProgram& shaderProgram);

	private:
		static std::unique_ptr<BinaryShaderCache> binaryShaderCache_;
		static std::unique_ptr<RenderBuffersManager> buffersManager_;
		static std::unique_ptr<RenderVaoPool> vaoPool_;
		static std::unique_ptr<RenderCommandPool> renderCommandPool_;
		static std::unique_ptr<RenderBatcher> renderBatcher_;

		static constexpr std::uint32_t DefaultShaderProgramsCount = std::uint32_t(Material::ShaderProgramType::Custom);
		static std::unique_ptr<Rhi::ShaderProgram> defaultShaderPrograms_[DefaultShaderProgramsCount];
		static HashMap<const Rhi::ShaderProgram*, Rhi::ShaderProgram*> batchedShaders_;

		static constexpr std::uint32_t UniformsBufferSize = 128; // two 4x4 float matrices
		static std::uint8_t cameraUniformsBuffer_[UniformsBufferSize];
		static HashMap<Rhi::ShaderProgram*, CameraUniformData> cameraUniformDataMap_;

		static Camera* currentCamera_;
		static std::unique_ptr<Camera> defaultCamera_;
		static Viewport* currentViewport_;

		static void SetCurrentCamera(Camera* camera);
		static void UpdateCameraUniforms();
		static void SetCurrentViewport(Viewport* viewport);

		static void CreateMinimal();
		static void Create();
		static void Dispose();

		static void RegisterDefaultBatchedShaders();
	};
}
