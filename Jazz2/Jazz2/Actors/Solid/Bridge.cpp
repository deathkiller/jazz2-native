#include "Bridge.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Actors::Solid
{
	Bridge::Bridge()
	{
	}

	void Bridge::Preload(const ActorActivationDetails& details)
	{
		BridgeType bridgeType = (BridgeType)details.Params[2];
		switch (bridgeType) {
			default:
			case BridgeType::Rope: PreloadMetadataAsync("Bridge/Rope"_s); break;
			case BridgeType::Stone: PreloadMetadataAsync("Bridge/Stone"_s); break;
			case BridgeType::Vine: PreloadMetadataAsync("Bridge/Vine"_s); break;
			case BridgeType::StoneRed: PreloadMetadataAsync("Bridge/StoneRed"_s); break;
			case BridgeType::Log: PreloadMetadataAsync("Bridge/Log"_s); break;
			case BridgeType::Gem: PreloadMetadataAsync("Bridge/Gem"_s); break;
			case BridgeType::Lab: PreloadMetadataAsync("Bridge/Lab"_s); break;
		}
	}

	Task<bool> Bridge::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_bridgeWidth = *(uint16_t*)&details.Params[0];
		_bridgeType = (BridgeType)details.Params[2];

		int toughness = details.Params[3];
		_heightFactor = sqrtf((16 - toughness) * _bridgeWidth) * 4.0f;

		_originalY = _pos.Y - 6;

		SetState(ActorState::SkipPerPixelCollisions | ActorState::IsSolidObject, true);
		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		const int* widths;
		int widthsCount;
		int widthOffset = 0;
		switch (_bridgeType) {
			default:
			case BridgeType::Rope: co_await RequestMetadataAsync("Bridge/Rope"_s); widths = PieceWidthsRope; widthsCount = _countof(PieceWidthsRope); break;
			case BridgeType::Stone: co_await RequestMetadataAsync("Bridge/Stone"_s); widths = PieceWidthsStone; widthsCount = _countof(PieceWidthsStone); break;
			case BridgeType::Vine: co_await RequestMetadataAsync("Bridge/Vine"_s); widths = PieceWidthsVine; widthsCount = _countof(PieceWidthsVine); widthOffset = 8; break;
			case BridgeType::StoneRed: co_await RequestMetadataAsync("Bridge/StoneRed"_s); widths = PieceWidthsStoneRed; widthsCount = _countof(PieceWidthsStoneRed); break;
			case BridgeType::Log: co_await RequestMetadataAsync("Bridge/Log"_s); widths = PieceWidthsLog; widthsCount = _countof(PieceWidthsLog); break;
			case BridgeType::Gem: co_await RequestMetadataAsync("Bridge/Gem"_s); widths = PieceWidthsGem; widthsCount = _countof(PieceWidthsGem); break;
			case BridgeType::Lab: co_await RequestMetadataAsync("Bridge/Lab"_s); widths = PieceWidthsLab; widthsCount = _countof(PieceWidthsLab); widthOffset = 12; break;
		}

		SetAnimation("Piece"_s);

		int widthCovered = widths[0] / 2 - widthOffset;
		for (int i = 0; (widthCovered <= _bridgeWidth * 16 + 8) || (i * 16 < _bridgeWidth); i++) {
			BridgePiece& piece = _pieces.emplace_back();
			piece.Pos = Vector2f(_pos.X + widthCovered - 16, _pos.Y);
			piece.Command = std::make_unique<RenderCommand>();
			piece.Command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE);
			piece.Command->material().setBlendingEnabled(true);
			piece.Command->material().reserveUniformsDataMemory();
			piece.Command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

			GLUniformCache* textureUniform = piece.Command->material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}

			widthCovered += (widths[i % widthsCount] + widths[(i + 1) % widthsCount]) / 2;
		}

		co_return true;
	}

	void Bridge::OnUpdate(float timeMult)
	{
		// TODO
	}

	void Bridge::OnUpdateHitbox()
	{
		AABBInner = AABBf(_pos.X - 16, _pos.Y - 6, _pos.X - 16 + _bridgeWidth * 16, _pos.Y + 16);
	}

	bool Bridge::OnDraw(RenderQueue& renderQueue)
	{
		if (_currentAnimation != nullptr) {
			Vector2i texSize = _currentAnimation->Base->TextureDiffuse->size();

			for (int i = 0; i < _pieces.size(); i++) {
				auto command = _pieces[i].Command.get();

				int curAnimFrame = _currentAnimation->FrameOffset + (i % _currentAnimation->FrameCount);
				int col = curAnimFrame % _currentAnimation->Base->FrameConfiguration.X;
				int row = curAnimFrame / _currentAnimation->Base->FrameConfiguration.X;
				float texScaleX = (float(_currentAnimation->Base->FrameDimensions.X) / float(texSize.X));
				float texBiasX = (float(_currentAnimation->Base->FrameDimensions.X * col) / float(texSize.X));
				float texScaleY = (float(_currentAnimation->Base->FrameDimensions.Y) / float(texSize.Y));
				float texBiasY = (float(_currentAnimation->Base->FrameDimensions.Y * row) / float(texSize.X));

				auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
				instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(_currentAnimation->Base->FrameDimensions.X, _currentAnimation->Base->FrameDimensions.Y);
				instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

				auto& pos = _pieces[i].Pos;
				command->setTransformation(Matrix4x4f::Translation(pos.X, pos.Y, 0.0f));
				command->setLayer(_renderer.layer());
				command->material().setTexture(*_currentAnimation->Base->TextureDiffuse.get());

				renderQueue.addCommand(command);
			}
		}

		return true;
	}
}