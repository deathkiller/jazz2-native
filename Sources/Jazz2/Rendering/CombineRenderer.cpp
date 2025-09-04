#include "CombineRenderer.h"
#include "PlayerViewport.h"
#include "../PreferencesCache.h"

#include "../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Rendering
{
	CombineRenderer::CombineRenderer(PlayerViewport* owner)
		: _owner(owner)
	{
		setVisitOrderState(SceneNode::VisitOrderState::Disabled);
	}
		
	void CombineRenderer::Initialize(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_bounds = Rectf(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));

		if (_renderCommand.material().SetShader(_owner->_levelHandler->_combineShader)) {
			_renderCommand.material().ReserveUniformsDataMemory();
			_renderCommand.geometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			auto* textureUniform = _renderCommand.material().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
			auto* lightTexUniform = _renderCommand.material().Uniform("uTextureLighting");
			if (lightTexUniform && lightTexUniform->GetIntValue(0) != 1) {
				lightTexUniform->SetIntValue(1); // GL_TEXTURE1
			}
			auto* blurHalfTexUniform = _renderCommand.material().Uniform("uTextureBlurHalf");
			if (blurHalfTexUniform && blurHalfTexUniform->GetIntValue(0) != 2) {
				blurHalfTexUniform->SetIntValue(2); // GL_TEXTURE2
			}
			auto* blurQuarterTexUniform = _renderCommand.material().Uniform("uTextureBlurQuarter");
			if (blurQuarterTexUniform && blurQuarterTexUniform->GetIntValue(0) != 3) {
				blurQuarterTexUniform->SetIntValue(3); // GL_TEXTURE3
			}
		}

		if (_renderCommandWithWater.material().SetShader(_owner->_levelHandler->_combineWithWaterShader)) {
			_renderCommandWithWater.material().ReserveUniformsDataMemory();
			_renderCommandWithWater.geometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			auto* textureUniform = _renderCommandWithWater.material().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
			auto* lightTexUniform = _renderCommandWithWater.material().Uniform("uTextureLighting");
			if (lightTexUniform && lightTexUniform->GetIntValue(0) != 1) {
				lightTexUniform->SetIntValue(1); // GL_TEXTURE1
			}
			auto* blurHalfTexUniform = _renderCommandWithWater.material().Uniform("uTextureBlurHalf");
			if (blurHalfTexUniform && blurHalfTexUniform->GetIntValue(0) != 2) {
				blurHalfTexUniform->SetIntValue(2); // GL_TEXTURE2
			}
			auto* blurQuarterTexUniform = _renderCommandWithWater.material().Uniform("uTextureBlurQuarter");
			if (blurQuarterTexUniform && blurQuarterTexUniform->GetIntValue(0) != 3) {
				blurQuarterTexUniform->SetIntValue(3); // GL_TEXTURE3
			}
			auto* noiseTexUniform = _renderCommandWithWater.material().Uniform("uTextureNoise");
			if (noiseTexUniform && noiseTexUniform->GetIntValue(0) != 4) {
				noiseTexUniform->SetIntValue(4); // GL_TEXTURE4
			}
		}

		_renderCommand.setTransformation(Matrix4x4f::Translation((float)x, (float)y, 0.0f));
		_renderCommandWithWater.setTransformation(Matrix4x4f::Translation((float)x, (float)y, 0.0f));
	}

	Rectf CombineRenderer::GetBounds() const
	{
		return _bounds;
	}

	bool CombineRenderer::OnDraw(RenderQueue& renderQueue)
	{
		float viewWaterLevel = _owner->_levelHandler->_waterLevel - _owner->_cameraPos.Y + _bounds.H * 0.5f;
		bool viewHasWater = (viewWaterLevel < _bounds.H);
		auto& command = (viewHasWater ? _renderCommandWithWater : _renderCommand);

		command.material().SetTexture(0, *_owner->_viewTexture);
		command.material().SetTexture(1, *_owner->_lightingBuffer);
		command.material().SetTexture(2, *_owner->_blurPass2.GetTarget());
		command.material().SetTexture(3, *_owner->_blurPass4.GetTarget());
		if (viewHasWater && !PreferencesCache::LowWaterQuality) {
			command.material().SetTexture(4, *_owner->_levelHandler->_noiseTexture);
		}

		auto* instanceBlock = command.material().UniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(_bounds.W, _bounds.H);
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

		command.material().Uniform("uAmbientColor")->SetFloatVector(_owner->_ambientLight.Data());
		command.material().Uniform("uTime")->SetFloatValue(_owner->_levelHandler->_elapsedFrames * 0.0018f);

		if (viewHasWater) {
			command.material().Uniform("uWaterLevel")->SetFloatValue(viewWaterLevel / _bounds.H);
			command.material().Uniform("uCameraPos")->SetFloatVector(_owner->_cameraPos.Data());
		}

		renderQueue.addCommand(&command);

		return true;
	}
}