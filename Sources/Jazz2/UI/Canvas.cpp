#include "Canvas.h"

#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Base/Random.h"

namespace Jazz2::UI
{
	Canvas::Canvas()
		: AnimTime(0.0f), _renderCommandsCount(0), _currentRenderQueue(nullptr)
	{
		setVisitOrderState(SceneNode::VisitOrderState::Disabled);
	}

	void Canvas::OnUpdate(float timeMult)
	{
		SceneNode::OnUpdate(timeMult);

		AnimTime += timeMult * AnimTimeMultiplier;
	}

	bool Canvas::OnDraw(RenderQueue& renderQueue)
	{
		SceneNode::OnDraw(renderQueue);

		_renderCommandsCount = 0;
		_currentRenderQueue = &renderQueue;

		return false;
	}

	void Canvas::DrawRenderCommand(RenderCommand* command)
	{
		_currentRenderQueue->AddCommand(command);
	}

	void Canvas::DrawTexture(const Texture& texture, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& texCoords, const Colorf& color, bool additiveBlending, float angle)
	{
		auto command = RentRenderCommand();
		if (command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite)) {
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			// Required to reset render command properly
			//command->SetTransformation(command->transformation());

			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
		}

		if (additiveBlending) {
			command->GetMaterial().SetBlendingFactors(GL_SRC_ALPHA, GL_ONE);
		} else {
			command->GetMaterial().SetBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

		auto instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatVector(texCoords.Data());
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(size.Data());
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());

		Matrix4x4f worldMatrix = Matrix4x4f::Translation(pos.X, pos.Y, 0.0f);
		if (std::abs(angle) > 0.01f) {
			worldMatrix.Translate(size.X * 0.5f, size.Y * 0.5f, 0.0f);
			worldMatrix.RotateZ(angle);
			worldMatrix.Translate(size.X * -0.5f, size.Y * -0.5f, 0.0f);
		}
		command->SetTransformation(worldMatrix);
		command->SetLayer(z);
		command->GetMaterial().SetTexture(texture);

		_currentRenderQueue->AddCommand(command);
	}

	void Canvas::DrawSolid(Vector2f pos, std::uint16_t z, Vector2f size, const Colorf& color, bool additiveBlending)
	{
		auto command = RentRenderCommand();
		if (command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::SpriteNoTexture)) {
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			// Required to reset render command properly
			//command->SetTransformation(command->transformation());
		}

		if (additiveBlending) {
			command->GetMaterial().SetBlendingFactors(GL_SRC_ALPHA, GL_ONE);
		} else {
			command->GetMaterial().SetBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

		auto instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(size.Data());
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());

		command->SetTransformation(Matrix4x4f::Translation(pos.X, pos.Y, 0.0f));
		command->SetLayer(z);

		_currentRenderQueue->AddCommand(command);
	}

	Vector2f Canvas::ApplyAlignment(Alignment align, Vector2f vec, Vector2f size)
	{
		Vector2f result = vec;

		// Sprites are aligned to center by default
		switch (align & Alignment::HorizontalMask) {
			case Alignment::Center: result.X -= size.X * 0.5f; break;
			case Alignment::Right: result.X -= size.X; break;
		}
		switch (align & Alignment::VerticalMask) {
			case Alignment::Center: result.Y -= size.Y * 0.5f; break;
			case Alignment::Bottom: result.Y -= size.Y; break;
		}

		return result;
	}

	RenderCommand* Canvas::RentRenderCommand()
	{
		if (_renderCommandsCount < _renderCommands.size()) {
			RenderCommand* command = _renderCommands[_renderCommandsCount].get();
			command->SetType(RenderCommand::Type::Sprite);
			_renderCommandsCount++;
			return command;
		} else {
			std::unique_ptr<RenderCommand>& command = _renderCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::Sprite));
			command->GetMaterial().SetBlendingEnabled(true);
			_renderCommandsCount++;
			return command.get();
		}
	}
}