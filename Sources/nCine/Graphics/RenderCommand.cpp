#include "RenderCommand.h"
#include "RHI/Rhi.h"
#include "RenderResources.h"
#include "Camera.h"
#include "DrawableNode.h"
#include "../tracy.h"

namespace nCine
{
	RenderCommand::RenderCommand(Type type)
		: _materialSortKey(0), _modelMatrixUniform(nullptr), _instanceBlock(nullptr), _cachedShaderChangeCounter(std::uint32_t(-1)),
			_layer(0), _numInstances(0), _batchSize(0), _transformationCommitted(false), _modelMatrixUniformInBlock(false),
			_modelMatrix(Matrix4x4f::Identity)
#if defined(NCINE_PROFILING)
			, _type(type)
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
		const std::uint32_t lower = _material.GetSortKey();
		_materialSortKey = upper | lower;
	}

	void RenderCommand::Issue()
	{
		ZoneScopedC(0x81A861);

		if (_geometry._numVertices == 0 && _geometry._numIndices == 0) {
			return;
		}

		_material.Bind();
		_material.CommitUniforms();

		RHI::Device::ScissorState scissorState = RHI::Device::GetScissorState();
		if (_scissorRect.W > 0 && _scissorRect.H > 0) {
			RHI::Device::SetScissor(_scissorRect);
		}

		std::uint32_t offset = 0;
#if (defined(RHI_GL_PROFILE_ES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
		// Simulating missing `glDrawElementsBaseVertex()` on OpenGL ES 3.0
		if (_geometry._numIndices > 0) {
			offset = _geometry.GetVboParams().offset + (_geometry._firstVertex * _geometry._numElementsPerVertex * sizeof(float));
		}
#endif
		_material.DefineVertexFormat(_geometry.GetVboParams().object, _geometry.GetIboParams().object, offset);
		_geometry.Bind();
		_geometry.Draw(_numInstances);

		RHI::Device::SetScissorState(scissorState);
	}

	void RenderCommand::SetScissor(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_scissorRect.Set(x, y, width, height);
	}

	void RenderCommand::SetTransformation(const Matrix4x4f& modelMatrix)
	{
		_modelMatrix = modelMatrix;
		_transformationCommitted = false;
	}

	void RenderCommand::RefreshCachedUniforms()
	{
		// The name-based lookups only have to run again after `Material::SetShaderProgram()`,
		// the resulting pointers stay valid because the caches are only rebuilt there
		if (_cachedShaderChangeCounter == _material._shaderChangeCounter) {
			return;
		}

		_instanceBlock = _material.UniformBlock(Material::InstanceBlockName);
		_modelMatrixUniform = (_instanceBlock != nullptr
			? _instanceBlock->GetUniform(Material::ModelMatrixUniformName)
			: _material.Uniform(Material::ModelMatrixUniformName));
		_modelMatrixUniformInBlock = (_instanceBlock != nullptr);
		_cachedShaderChangeCounter = _material._shaderChangeCounter;
	}

	RHI::UniformBlockCache* RenderCommand::GetInstanceBlock()
	{
		if (_material._shaderProgram == nullptr) {
			return nullptr;
		}
		RefreshCachedUniforms();
		return _instanceBlock;
	}

	void RenderCommand::CommitNodeTransformation()
	{
		if (_transformationCommitted) {
			return;
		}

		ZoneScopedC(0x81A861);

		const Camera::ProjectionValues cameraValues = RenderResources::GetCurrentCamera()->GetProjectionValues();
		_modelMatrix[3][2] = CalculateDepth(_layer, cameraValues.nearClip, cameraValues.farClip);

		if (_material._shaderProgram && _material._shaderProgram->GetStatus() == RHI::ShaderProgram::Status::LinkedWithIntrospection) {
			RefreshCachedUniforms();
			if (_modelMatrixUniform) {
				//ZoneScopedNC("Set model matrix", 0x81A861);
				_modelMatrixUniform->SetFloatVector(_modelMatrix.Data());
				if (!_modelMatrixUniformInBlock) {
					// The loose uniform was written through a cached pointer, so the material's
					// uniform manager has to be notified for its commit early-out check
					_material._shaderUniforms.MarkDirty();
				}
			}
		}

		_transformationCommitted = true;
	}

	void RenderCommand::CommitCameraTransformation()
	{
		ZoneScopedC(0x81A861);

		RenderResources::CameraUniformData* cameraUniformData = RenderResources::FindCameraUniformData(_material._shaderProgram);
		if (cameraUniformData == nullptr) {
			RenderResources::CameraUniformData newCameraUniformData;
			newCameraUniformData.shaderUniforms.SetProgram(_material._shaderProgram, Material::ProjectionViewMatrixExcludeString, nullptr);
			if (newCameraUniformData.shaderUniforms.GetUniformCount() == 2) {
				newCameraUniformData.shaderUniforms.SetUniformsDataPointer(RenderResources::GetCameraUniformsBuffer());
				newCameraUniformData.shaderUniforms.GetUniform(Material::ProjectionMatrixUniformName)->SetDirty(true);
				newCameraUniformData.shaderUniforms.GetUniform(Material::ViewMatrixUniformName)->SetDirty(true);
				newCameraUniformData.shaderUniforms.CommitUniforms();

				RenderResources::InsertCameraUniformData(_material._shaderProgram, std::move(newCameraUniformData));
			}
		} else {
			cameraUniformData->shaderUniforms.CommitUniforms();
		}
	}

	void RenderCommand::CommitAll()
	{
		// Copy the vertices and indices stored in host memory to video memory
		// This step is not needed if the command uses a custom VBO or IBO or directly writes into the common one
		_geometry.CommitVertices();
		_geometry.CommitIndices();

		// The model matrix should always be updated before committing uniform blocks
		CommitNodeTransformation();

		// Commits all the uniform blocks of command's shader program
		_material.CommitUniformBlocks();
	}

	float RenderCommand::CalculateDepth(std::uint16_t layer, float nearClip, float farClip)
	{
		// The layer translates to depth, from near to far
		return nearClip + LayerStep + (farClip - nearClip - LayerStep) * layer * LayerStep;
	}
}
