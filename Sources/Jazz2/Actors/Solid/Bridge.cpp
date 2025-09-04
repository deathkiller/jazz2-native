#include "Bridge.h"
#include "../../ContentResolver.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Actors::Solid
{
	Bridge::Bridge()
		: _widths(nullptr), _widthsCount(0), _widthOffset(0), _leftX(0.0f), _leftHeight(0.0f), _rightX(0.0f), _rightHeight(0.0f)
	{
	}

	Bridge::~Bridge()
	{
		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			player->CancelCarryingObject(this);
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
			case BridgeType::Rope: async_await RequestMetadataAsync("Bridge/Rope"_s); _widths = PieceWidthsRope; _widthsCount = std::int32_t(arraySize(PieceWidthsRope)); break;
			case BridgeType::Stone: async_await RequestMetadataAsync("Bridge/Stone"_s); _widths = PieceWidthsStone; _widthsCount = std::int32_t(arraySize(PieceWidthsStone)); break;
			case BridgeType::Vine: async_await RequestMetadataAsync("Bridge/Vine"_s); _widths = PieceWidthsVine; _widthsCount = std::int32_t(arraySize(PieceWidthsVine)); _widthOffset = 8; break;
			case BridgeType::StoneRed: async_await RequestMetadataAsync("Bridge/StoneRed"_s); _widths = PieceWidthsStoneRed; _widthsCount = std::int32_t(arraySize(PieceWidthsStoneRed)); break;
			case BridgeType::Log: async_await RequestMetadataAsync("Bridge/Log"_s); _widths = PieceWidthsLog; _widthsCount = std::int32_t(arraySize(PieceWidthsLog)); break;
			case BridgeType::Gem: async_await RequestMetadataAsync("Bridge/Gem"_s); _widths = PieceWidthsGem; _widthsCount = std::int32_t(arraySize(PieceWidthsGem)); break;
			case BridgeType::Lab: async_await RequestMetadataAsync("Bridge/Lab"_s); _widths = PieceWidthsLab; _widthsCount = std::int32_t(arraySize(PieceWidthsLab)); _widthOffset = 12; break;
		}

		SetAnimation(AnimState::Default);

		auto& resolver = ContentResolver::Get();
		if (!resolver.IsHeadless()) {
			std::int32_t widthCovered = _widths[0] / 2 - _widthOffset;
			for (std::int32_t i = 0; widthCovered <= _bridgeWidth + 4; i++) {
				BridgePiece& piece = _pieces.emplace_back();
				piece.Pos = Vector2f(_pos.X + widthCovered - 16, _pos.Y);
				piece.Command = std::make_unique<RenderCommand>(RenderCommand::Type::Sprite);
				piece.Command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite);
				piece.Command->GetMaterial().SetBlendingEnabled(true);
				piece.Command->GetMaterial().ReserveUniformsDataMemory();
				piece.Command->GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

				auto* textureUniform = piece.Command->GetMaterial().Uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->GetIntValue(0) != 0) {
					textureUniform->SetIntValue(0); // GL_TEXTURE0
				}

				widthCovered += (_widths[i % _widthsCount] + _widths[(i + 1) % _widthsCount]) / 2;
			}
		}

		async_return true;
	}

	void Bridge::OnUpdate(float timeMult)
	{
		std::int32_t n = 0;
		Player* foundPlayers[8];
		float foundX[8];

		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			auto diff = player->GetPos() - _pos;
			if (n < arraySize(foundPlayers) && diff.X >= -20.0f && diff.X <= _bridgeWidth + 20.0f &&
				diff.Y > -27.0f && diff.Y < _heightFactor && player->GetSpeed().Y >= 0.0f) {
				foundX[n] = diff.X;
				foundPlayers[n] = player;
				n++;
			} else {
				player->CancelCarryingObject(this);
			}
		}

		if (n >= 2) {
			float left = std::numeric_limits<float>::max();
			float right = std::numeric_limits<float>::min();

			for (std::int32_t i = 0; i < n; i++) {
				if (left > foundX[i]) {
					left = foundX[i];
				}
				if (right < foundX[i]) {
					right = foundX[i];
				}
			}

			_leftHeight = (std::abs(_leftX - left) < 64.0f ? lerpByTime(_leftHeight, GetSectionHeight(left), 0.1f, timeMult) : 0.0f);
			_rightHeight = (std::abs(_rightX - right) < 64.0f ? lerpByTime(_rightHeight, GetSectionHeight(right), 0.1f, timeMult) : 0.0f);
			_leftX = left;
			_rightX = right;

			for (std::int32_t i = 0; i < n; i++) {
				float height;
				if (foundX[i] <= left) {
					height = _leftHeight;
				} else if (right <= foundX[i]) {
					height = _rightHeight;
				} else {
					height = lerp(_leftHeight, _rightHeight, (foundX[i] - left) / (right - left));
				}

				auto* player = foundPlayers[i];
				auto playerPos = player->GetPos();
				player->UpdateCarryingObject(this);
				player->MoveInstantly(Vector2f(playerPos.X, _pos.Y + playerPos.Y - player->AABBInner.B + BaseY + height),
					MoveType::Absolute | MoveType::Force);
			}
		} else if (n == 1) {
			_leftHeight = _rightHeight = lerpByTime(std::max(_leftHeight, _rightHeight), GetSectionHeight(foundX[0]), 0.1f, timeMult);
			_leftX = _rightX = foundX[0];

			for (std::int32_t i = 0; i < n; i++) {
				auto* player = foundPlayers[i];
				auto playerPos = player->GetPos();
				player->UpdateCarryingObject(this);
				player->MoveInstantly(Vector2f(playerPos.X, _pos.Y + playerPos.Y - player->AABBInner.B + BaseY + _leftHeight),
					MoveType::Absolute | MoveType::Force);
			}
		} else {
			_leftHeight = lerpByTime(_leftHeight, 0.0f, 0.1f, timeMult);
			_rightHeight = lerpByTime(_rightHeight, 0.0f, 0.1f, timeMult);
		}

		auto& resolver = ContentResolver::Get();
		if (!resolver.IsHeadless()) {
			if (_leftHeight <= 0.001f && _rightHeight <= 0.001f) {
				// Render straight bridge
				std::int32_t widthCovered = _widths[0] / 2 - _widthOffset;
				for (std::int32_t i = 0; widthCovered <= _bridgeWidth + 4; i++) {
					BridgePiece& piece = _pieces[i];
					piece.Pos = Vector2f(_pos.X + widthCovered - 16, _pos.Y);
					widthCovered += (_widths[i % _widthsCount] + _widths[(i + 1) % _widthsCount]) / 2;
				}
			} else {
				// Render deformed bridge
				std::int32_t widthCovered = _widths[0] / 2 - _widthOffset;
				for (std::int32_t i = 0; widthCovered <= _bridgeWidth + 4; i++) {
					float drop;
					if (widthCovered < _leftX) {
						drop = _leftHeight * sinf(fPiOver2 * widthCovered / _leftX);
					} else if (_rightX < widthCovered) {
						drop = _rightHeight * sinf(fPiOver2 * (_bridgeWidth - widthCovered) / (_bridgeWidth - _rightX));
					} else {
						drop = lerp(_leftHeight, _rightHeight, (widthCovered - _leftX) / (_rightX - _leftX));
					}

					BridgePiece& piece = _pieces[i];
					piece.Pos.Y = _pos.Y + drop;

					widthCovered += (_widths[i % _widthsCount] + _widths[(i + 1) % _widthsCount]) / 2;
				}
			}
		}
	}

	void Bridge::OnUpdateHitbox()
	{
		AABBInner = AABBf(_pos.X - 16.0f, _pos.Y + BaseY, _pos.X + _bridgeWidth + 16.0f, _pos.Y - BaseY);
	}

	bool Bridge::OnDraw(RenderQueue& renderQueue)
	{
		if (_currentAnimation != nullptr) {
			Vector2i texSize = _currentAnimation->Base->TextureDiffuse->GetSize();

			for (std::int32_t i = 0; i < _pieces.size(); i++) {
				auto* command = _pieces[i].Command.get();

				std::int32_t curAnimFrame = _currentAnimation->FrameOffset + (i % _currentAnimation->FrameCount);
				std::int32_t col = curAnimFrame % _currentAnimation->Base->FrameConfiguration.X;
				std::int32_t row = curAnimFrame / _currentAnimation->Base->FrameConfiguration.X;
				float texScaleX = (float(_currentAnimation->Base->FrameDimensions.X) / float(texSize.X));
				float texBiasX = (float(_currentAnimation->Base->FrameDimensions.X * col) / float(texSize.X));
				float texScaleY = (float(_currentAnimation->Base->FrameDimensions.Y) / float(texSize.Y));
				float texBiasY = (float(_currentAnimation->Base->FrameDimensions.Y * row) / float(texSize.Y));

				auto* instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
				instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue((float)_currentAnimation->Base->FrameDimensions.X, (float)_currentAnimation->Base->FrameDimensions.Y);
				instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

				auto pos = _pieces[i].Pos;
				command->SetTransformation(Matrix4x4f::Translation(pos.X - _currentAnimation->Base->FrameDimensions.X / 2, pos.Y - _currentAnimation->Base->FrameDimensions.Y / 2, 0.0f));
				command->SetLayer(_renderer.layer());
				command->GetMaterial().SetTexture(*_currentAnimation->Base->TextureDiffuse.get());

				renderQueue.AddCommand(command);
			}
		}

		return true;
	}

	float Bridge::GetSectionHeight(float x) const
	{
		float fase = fPi * std::clamp(x / _bridgeWidth, 0.0f, 1.0f);
		return _heightFactor * sinf(fase);
	}
}