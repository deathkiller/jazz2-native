#include "LightingRenderer.h"
#include "PlayerViewport.h"

#include "../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Rendering
{
	LightingRenderer::LightingRenderer(PlayerViewport* owner)
		: _owner(owner)
#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
			, _renderCommandsCount(0)
#endif
	{
		_emittedLightsCache.reserve(32);
		setVisitOrderState(SceneNode::VisitOrderState::Disabled);
	}

	bool LightingRenderer::OnDraw(RenderQueue& renderQueue)
	{
#if !defined(RHI_CAP_SHADERS) || !defined(RHI_CAP_FRAMEBUFFERS)
		// Dynamic lighting is composited by a full-screen shader pass into an off-screen buffer, which the
		// combine shader later applies; on backends without cheap programmable shaders (the software renderer)
		// there is no combine pass, so the scene is drawn without dynamic lighting
		return true;
#else
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
#endif
	}

#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
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
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::One);
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

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
#endif
}