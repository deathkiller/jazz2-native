#include "LightingRenderer.h"
#include "PlayerViewport.h"

#include "../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Rendering
{
	LightingRenderer::LightingRenderer(PlayerViewport* owner)
		: _owner(owner), _renderCommandsCount(0)
	{
		_emittedLightsCache.reserve(32);
		setVisitOrderState(SceneNode::VisitOrderState::Disabled);
	}
		
	bool LightingRenderer::OnDraw(RenderQueue& renderQueue)
	{
		_renderCommandsCount = 0;
		_emittedLightsCache.clear();

		// Collect all active light emitters
		auto actors = _owner->_levelHandler->GetActors();
		std::size_t actorsCount = actors.size();
		for (std::size_t i = 0; i < actorsCount; i++) {
			actors[i]->OnEmitLights(_emittedLightsCache);
		}

		for (auto& light : _emittedLightsCache) {
			LightCommand& lightCommand = RentRenderCommand();
			lightCommand.TexRectUniform->SetFloatValue(light.Pos.X, light.Pos.Y, light.RadiusNear / light.RadiusFar, 0.0f);
			lightCommand.SpriteSizeUniform->SetFloatValue(light.RadiusFar * 2.0f, light.RadiusFar * 2.0f);
			lightCommand.ColorUniform->SetFloatValue(light.Intensity, light.Brightness, 0.0f, 0.0f);
			lightCommand.Command->SetTransformation(Matrix4x4f::Translation(light.Pos.X, light.Pos.Y, 0));

			renderQueue.AddCommand(lightCommand.Command.get());
		}

		return true;
	}

	LightingRenderer::LightCommand& LightingRenderer::RentRenderCommand()
	{
		if (_renderCommandsCount < _renderCommands.size()) {
			LightCommand& lightCommand = _renderCommands[_renderCommandsCount];
			_renderCommandsCount++;
			return lightCommand;
		} else {
			LightCommand& lightCommand = _renderCommands.emplace_back();
			lightCommand.Command = std::make_unique<RenderCommand>(RenderCommand::Type::Lighting);
			_renderCommandsCount++;
			RenderCommand* command = lightCommand.Command.get();
			command->GetMaterial().SetShader(_owner->_levelHandler->_lightingShader);
			command->GetMaterial().SetBlendingEnabled(true);
			command->GetMaterial().SetBlendingFactors(GL_SRC_ALPHA, GL_ONE);
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}

			// The uniform caches stay valid for the command's lifetime, as its shader never changes again
			auto instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
			lightCommand.TexRectUniform = instanceBlock->GetUniform(Material::TexRectUniformName);
			lightCommand.SpriteSizeUniform = instanceBlock->GetUniform(Material::SpriteSizeUniformName);
			lightCommand.ColorUniform = instanceBlock->GetUniform(Material::ColorUniformName);
			return lightCommand;
		}
	}
}