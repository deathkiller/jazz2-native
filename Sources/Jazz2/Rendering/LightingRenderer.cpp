#include "LightingRenderer.h"
#include "PlayerViewport.h"
#include "../ContentResolver.h"

#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/RenderResources.h"

#include <algorithm>

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

#if defined(DEATH_TARGET_VITA)
		// Lighting uses additive blending, so its order is irrelevant. Cull lights which cannot touch this
		// viewport and retain the strongest visible ones to avoid hundreds of ES2 draw calls per frame.
		const Rectf cullingRect = RenderResources::GetCurrentViewport()->GetCullingRect();
		std::size_t visibleLights = 0;
		for (const auto& light : _emittedLightsCache) {
			if (light.Pos.X + light.RadiusFar < cullingRect.X || light.Pos.X - light.RadiusFar > cullingRect.X + cullingRect.W ||
				light.Pos.Y + light.RadiusFar < cullingRect.Y || light.Pos.Y - light.RadiusFar > cullingRect.Y + cullingRect.H) {
				continue;
			}
			_emittedLightsCache[visibleLights++] = light;
		}
		_emittedLightsCache.resize(visibleLights);

		constexpr std::size_t MaxVisibleLights = 64;
		if (_emittedLightsCache.size() > MaxVisibleLights) {
			std::sort(_emittedLightsCache.begin(), _emittedLightsCache.end(), [](const LightEmitter& a, const LightEmitter& b) {
				return a.Intensity * a.RadiusFar > b.Intensity * b.RadiusFar;
			});
			_emittedLightsCache.resize(MaxVisibleLights);
		}

		if (!_emittedLightsCache.empty()) {
			if (_meshBatchCommand == nullptr) {
				_meshBatchCommand = std::make_unique<RenderCommand>(RenderCommand::Type::Lighting);
				_meshBatchCommand->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::LightingMeshBatch));
				_meshBatchCommand->GetMaterial().SetBlendingEnabled(true);
				_meshBatchCommand->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::One);
				_meshBatchCommand->GetMaterial().ReserveUniformsDataMemory();
				_meshBatchCommand->GetGeometry().SetElementsPerVertex(8);
			}

			constexpr float Corners[6][2] = {
				{ -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f },
				{ -1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f }
			};
			_meshBatchVertices.clear();
			_meshBatchVertices.reserve(_emittedLightsCache.size() * 6 * 8);
			for (const auto& light : _emittedLightsCache) {
				if (light.RadiusFar <= 0.0f) {
					continue;
				}
				for (const auto& corner : Corners) {
					_meshBatchVertices.push_back(corner[0]);
					_meshBatchVertices.push_back(corner[1]);
					_meshBatchVertices.push_back(light.Pos.X);
					_meshBatchVertices.push_back(light.Pos.Y);
					_meshBatchVertices.push_back(light.RadiusFar);
					_meshBatchVertices.push_back(light.RadiusNear / light.RadiusFar);
					_meshBatchVertices.push_back(light.Intensity);
					_meshBatchVertices.push_back(light.Brightness);
				}
			}

			if (!_meshBatchVertices.empty()) {
				auto& geometry = _meshBatchCommand->GetGeometry();
				const std::int32_t vertexCount = (std::int32_t)(_meshBatchVertices.size() / 8);
				geometry.SetVertexCount(vertexCount);
				geometry.SetHostVertexPointer(_meshBatchVertices.data());
				geometry.SetDrawParameters(PrimitiveType::Triangles, 0, vertexCount);
				_meshBatchCommand->SetTransformation(Matrix4x4f::Translation(0.0f, 0.0f, 0.0f));
				renderQueue.AddCommand(_meshBatchCommand.get());
			}
		}
#else
		for (auto& light : _emittedLightsCache) {
			LightCommand& lightCommand = RentRenderCommand();
			lightCommand.TexRectUniform->SetFloatValue(light.Pos.X, light.Pos.Y, light.RadiusNear / light.RadiusFar, 0.0f);
			lightCommand.SpriteSizeUniform->SetFloatValue(light.RadiusFar * 2.0f, light.RadiusFar * 2.0f);
			lightCommand.ColorUniform->SetFloatValue(light.Intensity, light.Brightness, 0.0f, 0.0f);
			lightCommand.Command->SetTransformation(Matrix4x4f::Translation(light.Pos.X, light.Pos.Y, 0));

			renderQueue.AddCommand(lightCommand.Command.get());
		}
#endif

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
