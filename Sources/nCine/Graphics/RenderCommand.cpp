#include "RenderCommand.h"
#include "RenderResources.h"
#include "Camera.h"
#include "DrawableNode.h"
#include "../tracy.h"

#if !defined(RHI_CAP_SHADERS)
#include <cstring>
namespace
{
	/// Column-major 4×4 matrix multiply: result = a * b.
	void MatMul4(float* result, const float* a, const float* b)
	{
		for (std::int32_t col = 0; col < 4; col++) {
			for (std::int32_t row = 0; row < 4; row++) {
				float sum = 0.0f;
				for (std::int32_t k = 0; k < 4; k++) {
					sum += a[k * 4 + row] * b[col * 4 + k];
				}
				result[col * 4 + row] = sum;
			}
		}
	}
}
#endif

namespace nCine
{
	RenderCommand::RenderCommand(Type type)
		: materialSortKey_(0), layer_(0), numInstances_(0), batchSize_(0), transformationCommitted_(false), modelMatrix_(Matrix4x4f::Identity)
#if defined(NCINE_PROFILING)
			, type_(type)
#endif
	{
	}

	RenderCommand::RenderCommand()
		: RenderCommand(Type::Unspecified)
	{
	}

	void RenderCommand::CalculateMaterialSortKey()
	{
		const std::uint64_t upper = std::uint64_t(GetLayerSortKey()) << 32;
		const std::uint32_t lower = material_.GetSortKey();
		materialSortKey_ = upper | lower;
	}

	void RenderCommand::Issue()
	{
		ZoneScopedC(0x81A861);

		if (geometry_.numVertices_ == 0 && geometry_.numIndices_ == 0) {
			return;
		}

		material_.Bind();
		material_.CommitUniforms();

		RHI::ScissorState scissorTestState = RHI::GetScissorState();
		if (scissorRect_.W > 0 && scissorRect_.H > 0) {
			RHI::SetScissorTest(true, scissorRect_.X, scissorRect_.Y, scissorRect_.W, scissorRect_.H);
		}

		std::uint32_t offset = 0;
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
		// Simulating missing `glDrawElementsBaseVertex()` on OpenGL ES 3.0
		if (geometry_.numIndices_ > 0) {
			offset = geometry_.GetVboParams().offset + (geometry_.firstVertex_ * geometry_.numElementsPerVertex_ * sizeof(float));
		}
#endif
		material_.DefineVertexFormat(geometry_.GetVboParams().object, geometry_.GetIboParams().object, offset);
		geometry_.Bind();

#if !defined(RHI_CAP_SHADERS)
		{
			// Build a DrawContext for the software rasterizer. Combine stored model matrix
			// with camera projection+view to produce the final MVP.
			const Camera* cam = RenderResources::GetCurrentCamera();
			const float* proj = cam->GetProjection().Data();
			const float* view = cam->GetView().Data();
			const RHI::FFState& matFF = material_.GetFFState();

			float vm[16], mvp[16];
			MatMul4(vm, view, matFF.mvpMatrix);
			MatMul4(mvp, proj, vm);

			RHI::DrawContext ctx;
			ctx.ff = matFF;
			std::memcpy(ctx.ff.mvpMatrix, mvp, 16 * sizeof(float));
			for (std::uint32_t i = 0; i < RHI::MaxTextureUnits; i++) {
				ctx.textures[i] = const_cast<RHI::Texture*>(material_.GetTexture(i));
			}
			ctx.ff.hasTexture = (ctx.textures[0] != nullptr);
			ctx.ff.textureUnit = 0;
			// Procedural sprites: vertices synthesised inside FetchVertex from FFState.
			// VBO-based meshes may set vertexFormat via DefineVertexFormat in the future.
			ctx.vertexFormat = nullptr;
			ctx.vboByteOffset = 0;
			ctx.blendingEnabled = material_.IsBlendingEnabled();
			ctx.blendSrc = material_.GetSrcBlendingFactor();
			ctx.blendDst = material_.GetDestBlendingFactor();
			const RHI::ScissorState sc = RHI::GetScissorState();
			ctx.scissorEnabled = sc.enabled;
			ctx.scissorRect.Set(sc.x, sc.y, sc.w, sc.h);
			RHI::SetDrawContext(ctx);
		}
#endif

		geometry_.Draw(numInstances_);

#if !defined(RHI_CAP_SHADERS)
		RHI::ClearDrawContext();
#endif

		RHI::SetScissorState(scissorTestState);
	}

	void RenderCommand::SetScissor(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		scissorRect_.Set(x, y, width, height);
	}

	void RenderCommand::SetTransformation(const Matrix4x4f& modelMatrix)
	{
		modelMatrix_ = modelMatrix;
		transformationCommitted_ = false;
	}

	void RenderCommand::CommitNodeTransformation()
	{
		if (transformationCommitted_) {
			return;
		}

		ZoneScopedC(0x81A861);

		const Camera::ProjectionValues cameraValues = RenderResources::GetCurrentCamera()->GetProjectionValues();
		modelMatrix_[3][2] = CalculateDepth(layer_, cameraValues.nearClip, cameraValues.farClip);

		material_.SetModelMatrixUniform(modelMatrix_.Data());

		transformationCommitted_ = true;
	}

	void RenderCommand::CommitCameraTransformation()
	{
		ZoneScopedC(0x81A861);

		RenderResources::CameraUniformData* cameraUniformData = RenderResources::FindCameraUniformData(material_.shaderProgram_);
		if (cameraUniformData == nullptr) {
			RenderResources::CameraUniformData newCameraUniformData;
			newCameraUniformData.shaderUniforms.SetProgram(material_.shaderProgram_, Material::ProjectionViewMatrixExcludeString, nullptr);
			if (newCameraUniformData.shaderUniforms.GetUniformCount() == 2) {
				newCameraUniformData.shaderUniforms.SetUniformsDataPointer(RenderResources::GetCameraUniformsBuffer());
				newCameraUniformData.shaderUniforms.GetUniform(Material::ProjectionMatrixUniformName)->SetDirty(true);
				newCameraUniformData.shaderUniforms.GetUniform(Material::ViewMatrixUniformName)->SetDirty(true);
				newCameraUniformData.shaderUniforms.CommitUniforms();

				RenderResources::InsertCameraUniformData(material_.shaderProgram_, std::move(newCameraUniformData));
			}
		} else {
			cameraUniformData->shaderUniforms.CommitUniforms();
		}
	}

	void RenderCommand::CommitAll()
	{
		// Copy the vertices and indices stored in host memory to video memory
		// This step is not needed if the command uses a custom VBO or IBO or directly writes into the common one
		geometry_.CommitVertices();
		geometry_.CommitIndices();

		// The model matrix should always be updated before committing uniform blocks
		CommitNodeTransformation();

		// Commits all the uniform blocks of command's shader program
		material_.CommitUniformBlocks();
	}

	float RenderCommand::CalculateDepth(std::uint16_t layer, float nearClip, float farClip)
	{
		// The layer translates to depth, from near to far
		return nearClip + LayerStep + (farClip - nearClip - LayerStep) * layer * LayerStep;
	}
}
