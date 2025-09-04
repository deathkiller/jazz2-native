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

	void RenderCommand::calculateMaterialSortKey()
	{
		const std::uint64_t upper = std::uint64_t(layerSortKey()) << 32;
		const std::uint32_t lower = material_.sortKey();
		materialSortKey_ = upper | lower;
	}

	void RenderCommand::issue()
	{
		ZoneScopedC(0x81A861);

		if (geometry_.numVertices_ == 0 && geometry_.numIndices_ == 0) {
			return;
		}

		material_.bind();
		material_.commitUniforms();

		GLScissorTest::State scissorTestState = GLScissorTest::GetState();
		if (scissorRect_.W > 0 && scissorRect_.H > 0) {
			GLScissorTest::Enable(scissorRect_);
		}

		std::uint32_t offset = 0;
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
		// Simulating missing `glDrawElementsBaseVertex()` on OpenGL ES 3.0
		if (geometry_.numIndices_ > 0) {
			offset = geometry_.vboParams().offset + (geometry_.firstVertex_ * geometry_.numElementsPerVertex_ * sizeof(GLfloat));
		}
#endif
		material_.defineVertexFormat(geometry_.vboParams().object, geometry_.iboParams().object, offset);
		geometry_.bind();
		geometry_.draw(numInstances_);

		GLScissorTest::SetState(scissorTestState);
	}

	void RenderCommand::setScissor(GLint x, GLint y, GLsizei width, GLsizei height)
	{
		scissorRect_.Set(x, y, width, height);
	}

	void RenderCommand::setTransformation(const Matrix4x4f& modelMatrix)
	{
		modelMatrix_ = modelMatrix;
		transformationCommitted_ = false;
	}

	void RenderCommand::commitNodeTransformation()
	{
		if (transformationCommitted_) {
			return;
		}

		ZoneScopedC(0x81A861);

		const Camera::ProjectionValues cameraValues = RenderResources::GetCurrentCamera()->GetProjectionValues();
		modelMatrix_[3][2] = calculateDepth(layer_, cameraValues.nearClip, cameraValues.farClip);

		if (material_.shaderProgram_ && material_.shaderProgram_->GetStatus() == GLShaderProgram::Status::LinkedWithIntrospection) {
			GLUniformBlockCache* instanceBlock = material_.uniformBlock(Material::InstanceBlockName);
			GLUniformCache* matrixUniform = instanceBlock
				? instanceBlock->GetUniform(Material::ModelMatrixUniformName)
				: material_.uniform(Material::ModelMatrixUniformName);
			if (matrixUniform) {
				//ZoneScopedNC("Set model matrix", 0x81A861);
				matrixUniform->SetFloatVector(modelMatrix_.Data());
			}
		}

		transformationCommitted_ = true;
	}

	void RenderCommand::commitCameraTransformation()
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

	void RenderCommand::commitAll()
	{
		// Copy the vertices and indices stored in host memory to video memory
		// This step is not needed if the command uses a custom VBO or IBO or directly writes into the common one
		geometry_.commitVertices();
		geometry_.commitIndices();

		// The model matrix should always be updated before committing uniform blocks
		commitNodeTransformation();

		// Commits all the uniform blocks of command's shader program
		material_.commitUniformBlocks();
	}

	float RenderCommand::calculateDepth(std::uint16_t layer, float nearClip, float farClip)
	{
		// The layer translates to depth, from near to far
		return nearClip + LayerStep + (farClip - nearClip - LayerStep) * layer * LayerStep;
	}
}
