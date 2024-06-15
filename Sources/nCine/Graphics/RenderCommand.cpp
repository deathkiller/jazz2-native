#include "RenderCommand.h"
#include "GL/GLShaderProgram.h"
#include "GL/GLScissorTest.h"
#include "RenderResources.h"
#include "Camera.h"
#include "DrawableNode.h"
#include "../tracy.h"

namespace nCine
{
	RenderCommand::RenderCommand(Type profilingType)
		: materialSortKey_(0), layer_(0), numInstances_(0), batchSize_(0), transformationCommitted_(false), modelMatrix_(Matrix4x4f::Identity)
#if defined(NCINE_PROFILING)
			, profilingType_(profilingType)
#endif
	{
	}

	RenderCommand::RenderCommand()
		: RenderCommand(Type::Unspecified)
	{
	}

	void RenderCommand::calculateMaterialSortKey()
	{
		const uint64_t upper = static_cast<uint64_t>(layerSortKey()) << 32;
		const uint32_t lower = material_.sortKey();
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

		GLScissorTest::State scissorTestState = GLScissorTest::state();
		if (scissorRect_.W > 0 && scissorRect_.H > 0) {
			GLScissorTest::enable(scissorRect_);
		}

		unsigned int offset = 0;
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
		// Simulating missing `glDrawElementsBaseVertex()` on OpenGL ES 3.0
		if (geometry_.numIndices_ > 0) {
			offset = geometry_.vboParams().offset + (geometry_.firstVertex_ * geometry_.numElementsPerVertex_ * sizeof(GLfloat));
		}
#endif
		material_.defineVertexFormat(geometry_.vboParams().object, geometry_.iboParams().object, offset);
		geometry_.bind();
		geometry_.draw(numInstances_);

		GLScissorTest::setState(scissorTestState);
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

		const Camera::ProjectionValues cameraValues = RenderResources::currentCamera()->projectionValues();
		modelMatrix_[3][2] = calculateDepth(layer_, cameraValues.nearClip, cameraValues.farClip);

		if (material_.shaderProgram_ && material_.shaderProgram_->status() == GLShaderProgram::Status::LinkedWithIntrospection) {
			GLUniformBlockCache* instanceBlock = material_.uniformBlock(Material::InstanceBlockName);
			GLUniformCache* matrixUniform = instanceBlock
				? instanceBlock->uniform(Material::ModelMatrixUniformName)
				: material_.uniform(Material::ModelMatrixUniformName);
			if (matrixUniform) {
				//ZoneScopedNC("Set model matrix", 0x81A861);
				matrixUniform->setFloatVector(modelMatrix_.Data());
			}
		}

		transformationCommitted_ = true;
	}

	void RenderCommand::commitCameraTransformation()
	{
		ZoneScopedC(0x81A861);

		RenderResources::CameraUniformData* cameraUniformData = RenderResources::findCameraUniformData(material_.shaderProgram_);
		if (cameraUniformData == nullptr) {
			RenderResources::CameraUniformData newCameraUniformData;
			newCameraUniformData.shaderUniforms.setProgram(material_.shaderProgram_, Material::ProjectionViewMatrixExcludeString, nullptr);
			if (newCameraUniformData.shaderUniforms.numUniforms() == 2) {
				newCameraUniformData.shaderUniforms.setUniformsDataPointer(RenderResources::cameraUniformsBuffer());
				newCameraUniformData.shaderUniforms.uniform(Material::ProjectionMatrixUniformName)->setDirty(true);
				newCameraUniformData.shaderUniforms.uniform(Material::ViewMatrixUniformName)->setDirty(true);
				newCameraUniformData.shaderUniforms.commitUniforms();

				RenderResources::insertCameraUniformData(material_.shaderProgram_, std::move(newCameraUniformData));
			}
		} else {
			cameraUniformData->shaderUniforms.commitUniforms();
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

	float RenderCommand::calculateDepth(uint16_t layer, float nearClip, float farClip)
	{
		// The layer translates to depth, from near to far
		return nearClip + LayerStep + (farClip - nearClip - LayerStep) * layer * LayerStep;
	}
}
