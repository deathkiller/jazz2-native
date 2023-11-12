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

		AnimTime += timeMult * 0.014f;
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
		_currentRenderQueue->addCommand(command);
	}

	void Canvas::DrawTexture(const Texture& texture, const Vector2f& pos, uint16_t z, const Vector2f& size, const Vector4f& texCoords, const Colorf& color, bool additiveBlending, float angle)
	{
		auto command = RentRenderCommand();
		if (command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE)) {
			command->material().reserveUniformsDataMemory();
			command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			// Required to reset render command properly
			command->setTransformation(command->transformation());

			GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
		}

		if (additiveBlending) {
			command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE);
		} else {
			command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

		auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatVector(texCoords.Data());
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatVector(size.Data());
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(color.Data());

		command->setTransformation(Matrix4x4f::Translation(pos.X, pos.Y, 0.0f).RotateZ(angle));
		command->setLayer(z);
		command->material().setTexture(texture);

		_currentRenderQueue->addCommand(command);
	}

	void Canvas::DrawSolid(const Vector2f& pos, uint16_t z, const Vector2f& size, const Colorf& color, bool additiveBlending)
	{
		auto command = RentRenderCommand();
		if (command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE_NO_TEXTURE)) {
			command->material().reserveUniformsDataMemory();
			command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			// Required to reset render command properly
			command->setTransformation(command->transformation());
		}

		if (additiveBlending) {
			command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE);
		} else {
			command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

		auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatVector(size.Data());
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(color.Data());

		command->setTransformation(Matrix4x4f::Translation(pos.X, pos.Y, 0.0f));
		command->setLayer(z);

		_currentRenderQueue->addCommand(command);
	}

	Vector2f Canvas::ApplyAlignment(Alignment align, const Vector2f& vec, const Vector2f& size)
	{
		Vector2f result = vec;

		// Sprites are aligned to center by default
		switch (align & Alignment::HorizontalMask) {
			case Alignment::Left: result.X += size.X * 0.5f; break;
			case Alignment::Right: result.X -= size.X * 0.5f; break;
		}
		switch (align & Alignment::VerticalMask) {
			case Alignment::Top: result.Y -= size.Y * 0.5f; break;
			case Alignment::Bottom: result.Y += size.Y * 0.5f; break;
		}

		return result;
	}

	RenderCommand* Canvas::RentRenderCommand()
	{
		if (_renderCommandsCount < _renderCommands.size()) {
			RenderCommand* command = _renderCommands[_renderCommandsCount].get();
			command->setType(RenderCommand::CommandTypes::Sprite);
			_renderCommandsCount++;
			return command;
		} else {
			std::unique_ptr<RenderCommand>& command = _renderCommands.emplace_back(std::make_unique<RenderCommand>());
			command->setType(RenderCommand::CommandTypes::Sprite);
			command->material().setBlendingEnabled(true);
			_renderCommandsCount++;
			return command.get();
		}
	}
}