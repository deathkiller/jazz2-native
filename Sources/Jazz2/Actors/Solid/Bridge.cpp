#include "Bridge.h"
#include "../../ContentResolver.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Graphics/RenderQueue.h"

#include <IO/MemoryStream.h>

using namespace Death::IO;

namespace Jazz2::Actors::Solid
{
	Bridge::Bridge()
		: _widths(nullptr), _widthsCount(0), _widthOffset(0), _leftX(0.0f), _leftHeight(0.0f), _rightX(0.0f), _rightHeight(0.0f),
			_syncLeftX(0.0f), _syncLeftHeight(0.0f), _syncRightX(0.0f), _syncRightHeight(0.0f), _sagSyncTimer(0.0f), _sagWasActive(false)
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
		EventParamsReader params(details);
		_bridgeWidth = params.GetUint16(0) * 16;
		_bridgeType = (BridgeType)params.GetUint8(2);
		// Limit _heightFactor here, because with higher _heightFactor (for example in "04_haunted1") it starts to be inaccurate
		_heightFactor = std::round(std::min((float)_bridgeWidth / params.GetUint8(3), 38.0f));

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
				piece.Command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

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

		// The server sees all players and computes the authoritative sag; broadcast it via the actor RPC so
		// clients render the bridge bending under remote players too (a client only sees its local player).
		// Local/single-player sessions take no action here (SendPacket is a no-op) and render from local sag.
		if (_levelHandler->IsServer() && !_levelHandler->IsLocalSession()) {
			bool active = (_leftHeight > 0.001f || _rightHeight > 0.001f);
			if (active || _sagWasActive) {
				_sagSyncTimer -= timeMult;
				if (_sagSyncTimer <= 0.0f) {
					_sagSyncTimer = 3.0f;
					_sagWasActive = active;

					MemoryStream packet(12);
					packet.WriteValue<std::int32_t>((std::int32_t)(_leftX * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(_leftHeight * 256.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(_rightX * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(_rightHeight * 256.0f));
					SendPacket(arrayView(packet.GetBuffer(), packet.GetSize()));
				}
			}
		}

		auto& resolver = ContentResolver::Get();
		if (!resolver.IsHeadless()) {
			// Render each piece at the deeper of the locally-computed sag and the server-synced sag (the latter
			// is zero except on multiplayer clients), so the bridge reflects the weight of all players
			std::int32_t widthCovered = _widths[0] / 2 - _widthOffset;
			for (std::int32_t i = 0; widthCovered <= _bridgeWidth + 4; i++) {
				float drop = std::max(GetDropAt((float)widthCovered, _leftX, _leftHeight, _rightX, _rightHeight),
					GetDropAt((float)widthCovered, _syncLeftX, _syncLeftHeight, _syncRightX, _syncRightHeight));

				BridgePiece& piece = _pieces[i];
				piece.Pos = Vector2f(_pos.X + widthCovered - 16, _pos.Y + drop);

				widthCovered += (_widths[i % _widthsCount] + _widths[(i + 1) % _widthsCount]) / 2;
			}
		}
	}

	float Bridge::GetDropAt(float widthCovered, float leftX, float leftHeight, float rightX, float rightHeight) const
	{
		if (leftHeight <= 0.001f && rightHeight <= 0.001f) {
			return 0.0f;
		}
		if (widthCovered < leftX) {
			return (leftX > 0.0f ? leftHeight * sinf(fPiOver2 * widthCovered / leftX) : leftHeight);
		} else if (rightX < widthCovered) {
			return (_bridgeWidth > rightX ? rightHeight * sinf(fPiOver2 * (_bridgeWidth - widthCovered) / (_bridgeWidth - rightX)) : rightHeight);
		} else {
			return (rightX > leftX ? lerp(leftHeight, rightHeight, (widthCovered - leftX) / (rightX - leftX)) : leftHeight);
		}
	}

	void Bridge::OnPacketReceived(MemoryStream& packet)
	{
		// Authoritative sag from the server (clients only); used together with the locally-computed sag when rendering
		_syncLeftX = packet.ReadValue<std::int32_t>() / 512.0f;
		_syncLeftHeight = packet.ReadValue<std::int16_t>() / 256.0f;
		_syncRightX = packet.ReadValue<std::int32_t>() / 512.0f;
		_syncRightHeight = packet.ReadValue<std::int16_t>() / 256.0f;
	}

	void Bridge::OnUpdateHitbox()
	{
		AABBInner = AABBf(_pos.X - 16.0f, _pos.Y + BaseY, _pos.X + _bridgeWidth + 16.0f, _pos.Y - BaseY);
	}

	bool Bridge::OnDraw(RenderQueue& renderQueue)
	{
		if (_currentAnimation != nullptr) {
			Vector2i texSize = _currentAnimation->Base->TextureDiffuse->GetSize();
			auto& resolver = ContentResolver::Get();
			bool indexed = ((_currentAnimation->Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed);

			for (std::int32_t i = 0; i < _pieces.size(); i++) {
				auto* command = _pieces[i].Command.get();
				resolver.ConfigureSpriteShader(*command, indexed);

				std::int32_t curAnimFrame = _currentAnimation->FrameOffset + (i % _currentAnimation->FrameCount);
				std::int32_t col = curAnimFrame % _currentAnimation->Base->FrameConfiguration.X;
				std::int32_t row = curAnimFrame / _currentAnimation->Base->FrameConfiguration.X;
				float texScaleX = (float(_currentAnimation->Base->FrameDimensions.X) / float(texSize.X));
				float texBiasX = (float(_currentAnimation->Base->FrameDimensions.X * col) / float(texSize.X));
				float texScaleY = (float(_currentAnimation->Base->FrameDimensions.Y) / float(texSize.Y));
				float texBiasY = (float(_currentAnimation->Base->FrameDimensions.Y * row) / float(texSize.Y));

				auto* instanceBlock = command->GetInstanceBlock();
				instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue((float)_currentAnimation->Base->FrameDimensions.X, (float)_currentAnimation->Base->FrameDimensions.Y);
				instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

				auto pos = _pieces[i].Pos;
				command->SetTransformation(Matrix4x4f::Translation(pos.X - _currentAnimation->Base->FrameDimensions.X / 2, pos.Y - _currentAnimation->Base->FrameDimensions.Y / 2, 0.0f));
				command->SetLayer(_renderer.layer());
				resolver.BindSpritePalette(*command, *_currentAnimation->Base->TextureDiffuse.get(), indexed, _currentAnimation->PaletteOffset);

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