#include "Bridge.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Actors::Solid
{
	Bridge::Bridge()
		:
		_widths(nullptr),
		_widthsCount(0),
		_widthOffset(0),
		_foundHeight(0.0f),
		_foundX(0.0f)
	{
	}

	Bridge::~Bridge()
	{
		auto& players = _levelHandler->GetPlayers();
		for (auto& player : players) {
			if (player->GetCarryingObject() == this) {
				player->SetCarryingObject(nullptr);
			}
		}
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
		_bridgeWidth = *(uint16_t*)&details.Params[0] * 16;
		_bridgeType = (BridgeType)details.Params[2];
		// Limit _heightFactor here, because with higher _heightFactor (for example in "04_haunted1") it starts to be inaccurate
		_heightFactor = std::round(std::min((float)_bridgeWidth / details.Params[3], 38.0f));

		_pos.Y -= 6.0f;

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		switch (_bridgeType) {
			default:
			case BridgeType::Rope: async_await RequestMetadataAsync("Bridge/Rope"_s); _widths = PieceWidthsRope; _widthsCount = countof(PieceWidthsRope); break;
			case BridgeType::Stone: async_await RequestMetadataAsync("Bridge/Stone"_s); _widths = PieceWidthsStone; _widthsCount = countof(PieceWidthsStone); break;
			case BridgeType::Vine: async_await RequestMetadataAsync("Bridge/Vine"_s); _widths = PieceWidthsVine; _widthsCount = countof(PieceWidthsVine); _widthOffset = 8; break;
			case BridgeType::StoneRed: async_await RequestMetadataAsync("Bridge/StoneRed"_s); _widths = PieceWidthsStoneRed; _widthsCount = countof(PieceWidthsStoneRed); break;
			case BridgeType::Log: async_await RequestMetadataAsync("Bridge/Log"_s); _widths = PieceWidthsLog; _widthsCount = countof(PieceWidthsLog); break;
			case BridgeType::Gem: async_await RequestMetadataAsync("Bridge/Gem"_s); _widths = PieceWidthsGem; _widthsCount = countof(PieceWidthsGem); break;
			case BridgeType::Lab: async_await RequestMetadataAsync("Bridge/Lab"_s); _widths = PieceWidthsLab; _widthsCount = countof(PieceWidthsLab); _widthOffset = 12; break;
		}

		SetAnimation(AnimState::Default);

		int widthCovered = _widths[0] / 2 - _widthOffset;
		for (int i = 0; widthCovered <= _bridgeWidth + 4; i++) {
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

			widthCovered += (_widths[i % _widthsCount] + _widths[(i + 1) % _widthsCount]) / 2;
		}

		async_return true;
	}

	void Bridge::OnUpdate(float timeMult)
	{
		int hits = 0;
		Player* foundPlayers[4];
		float foundDiffs[4];

		auto& players = _levelHandler->GetPlayers();
		for (auto& player : players) {
			auto diff = player->GetPos() - _pos;
			if (diff.X >= -20.0f && diff.X <= _bridgeWidth + 20.0f && diff.Y > -27.0f && diff.Y < _heightFactor && player->GetSpeed().Y >= 0.0f) {
				foundDiffs[hits] = diff.X;
				foundPlayers[hits] = player;
				hits++;
			} else if (player->GetCarryingObject() == this) {
				player->SetCarryingObject(nullptr);
			}
		}

		if (hits >= 1) {
			float fase = fPi * std::clamp(foundDiffs[0] / _bridgeWidth, 0.0f, 1.0f);
			float height = _heightFactor * sinf(fase);
			_foundX = foundDiffs[0];
			_foundHeight = lerp(_foundHeight, height, timeMult * 0.1f);

			SetState(ActorState::IsSolidObject, true);
			OnUpdateHitbox();

			auto player = foundPlayers[0];
			auto playerPos = player->GetPos();
			player->SetCarryingObject(this);
			player->MoveInstantly(Vector2f(playerPos.X, _pos.Y + playerPos.Y - player->AABBInner.B + BaseY + _foundHeight), MoveType::Absolute | MoveType::Force);
		} else {
			_foundHeight = lerp(_foundHeight, 0.0f, timeMult * 0.1f);

			SetState(ActorState::IsSolidObject, false);
			OnUpdateHitbox();
		}

		if (_foundHeight <= 0.001f) {
			// Render straight bridge
			int widthCovered = _widths[0] / 2 - _widthOffset;
			for (int i = 0; widthCovered <= _bridgeWidth + 4; i++) {
				BridgePiece& piece = _pieces[i];
				piece.Pos = Vector2f(_pos.X + widthCovered - 16, _pos.Y);
				widthCovered += (_widths[i % _widthsCount] + _widths[(i + 1) % _widthsCount]) / 2;
			}
		} else {
			// Render deformed bridge
			int widthCovered = _widths[0] / 2 - _widthOffset;
			for (int i = 0; widthCovered <= _bridgeWidth + 4; i++) {
				float drop;
				if (widthCovered < _foundX) {
					drop = _foundHeight * sinf(fPiOver2 * widthCovered / _foundX);
				} else {
					drop = _foundHeight * sinf(fPiOver2 * (_bridgeWidth - widthCovered) / (_bridgeWidth - _foundX));
				}

				BridgePiece& piece = _pieces[i];
				piece.Pos.Y = _pos.Y + drop;

				widthCovered += (_widths[i % _widthsCount] + _widths[(i + 1) % _widthsCount]) / 2;
			}
		}
	}

	void Bridge::OnUpdateHitbox()
	{
		AABBInner = AABBf(_pos.X - 16.0f, _pos.Y + BaseY + _foundHeight, _pos.X + _bridgeWidth + 16.0f, _pos.Y + BaseY + 12.0f + _foundHeight);
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
				float texBiasY = (float(_currentAnimation->Base->FrameDimensions.Y * row) / float(texSize.Y));

				auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
				instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue((float)_currentAnimation->Base->FrameDimensions.X, (float)_currentAnimation->Base->FrameDimensions.Y);
				instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

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