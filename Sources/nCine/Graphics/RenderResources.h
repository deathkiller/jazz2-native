#pragma once

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
			float position[2];
		};

		/** @brief Vertex format for vertices with positions and texture coordinates */
		struct VertexFormatPos2Tex2
		{
			float position[2];
			float texcoords[2];
		};

		/** @brief Vertex format for vertices with positions and draw indices */
		struct VertexFormatPos2Index
		{
			float position[2];
			std::int32_t drawindex;
		};

		/** @brief Vertex format for vertices with positions, texture coordinates and draw indices */
		struct VertexFormatPos2Tex2Index
		{
			float position[2];
			float texcoords[2];
			std::int32_t drawindex;
		};

		/** @brief Per-shader camera uniform data and the frames its matrices were last updated */
		struct CameraUniformData
		{
			CameraUniformData()
				: camera(nullptr), updateFrameProjectionMatrix(0), updateFrameViewMatrix(0) {}

			RHI::ShaderUniforms shaderUniforms;
			Camera* camera;
			std::uint32_t updateFrameProjectionMatrix;
			std::uint32_t updateFrameViewMatrix;
		};

		static inline BinaryShaderCache& GetBinaryShaderCache() {
			return *_binaryShaderCache;
		}
		static inline RenderBuffersManager& GetBuffersManager() {
			return *_buffersManager;
		}
		static inline RenderVaoPool& GetVaoPool() {
			return *_vaoPool;
		}
		static inline RenderCommandPool& GetRenderCommandPool() {
			return *_renderCommandPool;
		}
		static inline RenderBatcher& GetRenderBatcher() {
			return *_renderBatcher;
		}

#if defined(RHI_GL_PROFILE_ES2)
		/** @brief OpenGL|ES 2.0 only: shared static VBO of the 4 quad corners feeding the aQuadCorner attribute (replaces gl_VertexID) */
		static inline RHI::Buffer* GetQuadCornerVbo() {
			return _quadCornerVbo.get();
		}
#endif

		/** @brief Number of distinct corners a quad contributes to an indexed mesh */
		static constexpr std::uint32_t VerticesPerQuad = 4;
		/** @brief Number of indices that draw one quad as two triangles */
		static constexpr std::uint32_t IndicesPerQuad = 6;

		/**
			@brief Returns how many quads one indexed draw can describe, for a mesh of @p floatsPerVertex floats per vertex

			A quad occupies four interleaved vertices in the shared array buffer and six 16-bit indices in the
			shared element one, so the smaller of those two limits - and of what a 16-bit index can address at
			all - is how far a single draw reaches. A quad mesh larger than that has to be split into several
			commands on whole-quad boundaries.
		*/
		static std::uint32_t GetMaxQuadsPerDraw(std::uint32_t floatsPerVertex);

		/**
			@brief Returns the host-side index array that draws quads as two triangles each

			The pattern is the same for every quad mesh - `0, 1, 2, 0, 2, 3` per quad, counted from the first
			vertex of the command - so one array backs all of them instead of every renderer building its own.
			A quad mesh writes only its four distinct corners and points its geometry here, which is a third
			less vertex data to copy and a third fewer vertex shader invocations than the six-vertex stream the
			same triangles need without indices.

			The array is allocated once, long enough for the longest draw @ref GetMaxQuadsPerDraw() can report,
			so the returned pointer stays valid as long as the rendering resources do - a render queue may hold
			it until its draw phase without another mesh moving it in between.
		*/
		static const std::uint16_t* GetQuadIndices();

		static RHI::ShaderProgram* GetShaderProgram(Material::ShaderProgramType shaderProgramType);

		static RHI::ShaderProgram* GetBatchedShader(const RHI::ShaderProgram* shader);
		static bool RegisterBatchedShader(const RHI::ShaderProgram* shader, RHI::ShaderProgram* batchedShader);
		static bool UnregisterBatchedShader(const RHI::ShaderProgram* shader);

		static inline std::uint8_t* GetCameraUniformsBuffer() {
			return _cameraUniformsBuffer;
		}
		static CameraUniformData* FindCameraUniformData(RHI::ShaderProgram* shaderProgram);
		static void InsertCameraUniformData(RHI::ShaderProgram* shaderProgram, CameraUniformData&& cameraUniformData);
		static bool RemoveCameraUniformData(RHI::ShaderProgram* shaderProgram);

		static inline const Camera* GetCurrentCamera() {
			return _currentCamera;
		}
		static inline const Viewport* GetCurrentViewport() {
			return _currentViewport;
		}

		static void SetDefaultAttributesParameters(RHI::ShaderProgram& shaderProgram);

	private:
		static std::unique_ptr<BinaryShaderCache> _binaryShaderCache;
		static std::unique_ptr<RenderBuffersManager> _buffersManager;
		static std::unique_ptr<RenderVaoPool> _vaoPool;
		static std::unique_ptr<RenderCommandPool> _renderCommandPool;
		static std::unique_ptr<RenderBatcher> _renderBatcher;

#if defined(RHI_GL_PROFILE_ES2)
		// Static 4-corner (TRIANGLE_STRIP order) VBO for the single-quad ES2 sprite/full-screen path
		static std::unique_ptr<RHI::Buffer> _quadCornerVbo;
#endif

		// Two-triangles-per-quad index pattern shared by every quad mesh (see GetQuadIndices())
		static std::unique_ptr<std::uint16_t[]> _quadIndices;

		static constexpr std::uint32_t DefaultShaderProgramsCount = std::uint32_t(Material::ShaderProgramType::Custom);
		static std::unique_ptr<RHI::ShaderProgram> _defaultShaderPrograms[DefaultShaderProgramsCount];
		static HashMap<const RHI::ShaderProgram*, RHI::ShaderProgram*> _batchedShaders;

		static constexpr std::uint32_t UniformsBufferSize = 128; // two 4x4 float matrices
		static std::uint8_t _cameraUniformsBuffer[UniformsBufferSize];
		static HashMap<RHI::ShaderProgram*, CameraUniformData> _cameraUniformDataMap;

		static Camera* _currentCamera;
		static std::unique_ptr<Camera> _defaultCamera;
		static Viewport* _currentViewport;

		static std::uint32_t GetMaxQuadsForIndices();

		static void SetCurrentCamera(Camera* camera);
		static void UpdateCameraUniforms();
		static void SetCurrentViewport(Viewport* viewport);

		static void CreateMinimal();
		static void Create();
		static void Dispose();

		static void RegisterDefaultBatchedShaders();
	};
}
