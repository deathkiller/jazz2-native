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
			GLUniformCache* textureUniform = _renderCommand.material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
			GLUniformCache* lightTexUniform = _renderCommand.material().uniform("uTextureLighting");
			if (lightTexUniform && lightTexUniform->intValue(0) != 1) {
				lightTexUniform->setIntValue(1); // GL_TEXTURE1
			}
			GLUniformCache* blurHalfTexUniform = _renderCommand.material().uniform("uTextureBlurHalf");
			if (blurHalfTexUniform && blurHalfTexUniform->intValue(0) != 2) {
				blurHalfTexUniform->setIntValue(2); // GL_TEXTURE2
			}
			GLUniformCache* blurQuarterTexUniform = _renderCommand.material().uniform("uTextureBlurQuarter");
			if (blurQuarterTexUniform && blurQuarterTexUniform->intValue(0) != 3) {
				blurQuarterTexUniform->setIntValue(3); // GL_TEXTURE3
			}
		}

		if (_renderCommandWithWater.material().setShader(_owner->_levelHandler->_combineWithWaterShader)) {
			_renderCommandWithWater.material().reserveUniformsDataMemory();
			_renderCommandWithWater.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			GLUniformCache* textureUniform = _renderCommandWithWater.material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
			GLUniformCache* lightTexUniform = _renderCommandWithWater.material().uniform("uTextureLighting");
			if (lightTexUniform && lightTexUniform->intValue(0) != 1) {
				lightTexUniform->setIntValue(1); // GL_TEXTURE1
			}
			GLUniformCache* blurHalfTexUniform = _renderCommandWithWater.material().uniform("uTextureBlurHalf");
			if (blurHalfTexUniform && blurHalfTexUniform->intValue(0) != 2) {
				blurHalfTexUniform->setIntValue(2); // GL_TEXTURE2
			}
			GLUniformCache* blurQuarterTexUniform = _renderCommandWithWater.material().uniform("uTextureBlurQuarter");
			if (blurQuarterTexUniform && blurQuarterTexUniform->intValue(0) != 3) {
				blurQuarterTexUniform->setIntValue(3); // GL_TEXTURE3
			}
			GLUniformCache* displacementTexUniform = _renderCommandWithWater.material().uniform("uTextureDisplacement");
			if (displacementTexUniform && displacementTexUniform->intValue(0) != 4) {
				displacementTexUniform->setIntValue(4); // GL_TEXTURE4
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
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(_bounds.W, _bounds.H);
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

		command.material().uniform("uAmbientColor")->setFloatVector(_owner->_ambientLight.Data());
		command.material().uniform("uTime")->setFloatValue(_owner->_levelHandler->_elapsedFrames * 0.0018f);

		if (viewHasWater) {
			command.material().uniform("uWaterLevel")->setFloatValue(viewWaterLevel / _bounds.H);
			command.material().uniform("uCameraPos")->setFloatVector(_owner->_cameraPos.Data());
		}

		renderQueue.addCommand(&command);

		return true;
	}
}