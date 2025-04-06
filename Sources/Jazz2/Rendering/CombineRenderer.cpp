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

		if (_renderCommand.material().setShader(_owner->_levelHandler->_combineShader)) {
			_renderCommand.material().reserveUniformsDataMemory();
			_renderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			auto* textureUniform = _renderCommand.material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
			auto* lightTexUniform = _renderCommand.material().uniform("uTextureLighting");
			if (lightTexUniform && lightTexUniform->GetIntValue(0) != 1) {
				lightTexUniform->SetIntValue(1); // GL_TEXTURE1
			}
			auto* blurHalfTexUniform = _renderCommand.material().uniform("uTextureBlurHalf");
			if (blurHalfTexUniform && blurHalfTexUniform->GetIntValue(0) != 2) {
				blurHalfTexUniform->SetIntValue(2); // GL_TEXTURE2
			}
			auto* blurQuarterTexUniform = _renderCommand.material().uniform("uTextureBlurQuarter");
			if (blurQuarterTexUniform && blurQuarterTexUniform->GetIntValue(0) != 3) {
				blurQuarterTexUniform->SetIntValue(3); // GL_TEXTURE3
			}
		}

		if (_renderCommandWithWater.material().setShader(_owner->_levelHandler->_combineWithWaterShader)) {
			_renderCommandWithWater.material().reserveUniformsDataMemory();
			_renderCommandWithWater.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			auto* textureUniform = _renderCommandWithWater.material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
			auto* lightTexUniform = _renderCommandWithWater.material().uniform("uTextureLighting");
			if (lightTexUniform && lightTexUniform->GetIntValue(0) != 1) {
				lightTexUniform->SetIntValue(1); // GL_TEXTURE1
			}
			auto* blurHalfTexUniform = _renderCommandWithWater.material().uniform("uTextureBlurHalf");
			if (blurHalfTexUniform && blurHalfTexUniform->GetIntValue(0) != 2) {
				blurHalfTexUniform->SetIntValue(2); // GL_TEXTURE2
			}
			auto* blurQuarterTexUniform = _renderCommandWithWater.material().uniform("uTextureBlurQuarter");
			if (blurQuarterTexUniform && blurQuarterTexUniform->GetIntValue(0) != 3) {
				blurQuarterTexUniform->SetIntValue(3); // GL_TEXTURE3
			}
			auto* displacementTexUniform = _renderCommandWithWater.material().uniform("uTextureDisplacement");
			if (displacementTexUniform && displacementTexUniform->GetIntValue(0) != 4) {
				displacementTexUniform->SetIntValue(4); // GL_TEXTURE4
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

		command.material().setTexture(0, *_owner->_viewTexture);
		command.material().setTexture(1, *_owner->_lightingBuffer);
		command.material().setTexture(2, *_owner->_blurPass2.GetTarget());
		command.material().setTexture(3, *_owner->_blurPass4.GetTarget());
		if (viewHasWater && !PreferencesCache::LowWaterQuality) {
			command.material().setTexture(4, *_owner->_levelHandler->_noiseTexture);
		}

		auto* instanceBlock = command.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(_bounds.W, _bounds.H);
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

		command.material().uniform("uAmbientColor")->SetFloatVector(_owner->_ambientLight.Data());
		command.material().uniform("uTime")->SetFloatValue(_owner->_levelHandler->_elapsedFrames * 0.0018f);

		if (viewHasWater) {
			command.material().uniform("uWaterLevel")->SetFloatValue(viewWaterLevel / _bounds.H);
			command.material().uniform("uCameraPos")->SetFloatVector(_owner->_cameraPos.Data());
		}

		renderQueue.addCommand(&command);

		return true;
	}
}