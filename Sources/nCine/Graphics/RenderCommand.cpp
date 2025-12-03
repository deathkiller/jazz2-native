#include "RenderCommand.h"
#include "GL/GLShaderProgram.h"
#include "GL/GLScissorTest.h"
#include "RenderResources.h"
#include "Camera.h"
#include "DrawableNode.h"
#include "../tracy.h"

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

		GLScissorTest::State scissorTestState = GLScissorTest::GetState();
		if (scissorRect_.W > 0 && scissorRect_.H > 0) {
			GLScissorTest::Enable(scissorRect_);
		}

		std::uint32_t offset = 0;
#if defined(WITH_OPENGL2) || (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
		// Simulating missing `glDrawElementsBaseVertex()` on OpenGL ES 3.0 and OpenGL 2.x
		if (geometry_.numIndices_ > 0) {
			offset = geometry_.GetVboParams().offset + (geometry_.firstVertex_ * geometry_.numElementsPerVertex_ * sizeof(GLfloat));
		}
#endif
		material_.DefineVertexFormat(geometry_.GetVboParams().object, geometry_.GetIboParams().object, offset);
		geometry_.Bind();
		geometry_.Draw(numInstances_);

		GLScissorTest::SetState(scissorTestState);
	}

	void RenderCommand::SetScissor(GLint x, GLint y, GLsizei width, GLsizei height)
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

		if (material_.shaderProgram_ && material_.shaderProgram_->GetStatus() == GLShaderProgram::Status::LinkedWithIntrospection) {
			GLUniformBlockCache* instanceBlock = material_.UniformBlock(Material::InstanceBlockName);
			GLUniformCache* matrixUniform = instanceBlock
				? instanceBlock->GetUniform(Material::ModelMatrixUniformName)
				: material_.Uniform(Material::ModelMatrixUniformName);
			if (matrixUniform) {
				//ZoneScopedNC("Set model matrix", 0x81A861);
				matrixUniform->SetFloatVector(modelMatrix_.Data());
			}
		}

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
