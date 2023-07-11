#include "MovingPlatform.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Actors::Solid
{
	MovingPlatform::MovingPlatform()
	{
	}

	MovingPlatform::~MovingPlatform()
	{
		auto& players = _levelHandler->GetPlayers();
		for (auto& player : players) {
			if (player->GetCarryingObject() == this) {
				player->SetCarryingObject(nullptr);
			}
		}
	}

	void MovingPlatform::Preload(const ActorActivationDetails& details)
	{
		PlatformType platformType = (PlatformType)details.Params[0];
		switch (platformType) {
			default:
			case PlatformType::CarrotusFruit: PreloadMetadataAsync("MovingPlatform/CarrotusFruit"_s); break;
			case PlatformType::Ball: PreloadMetadataAsync("MovingPlatform/Ball"_s); break;
			case PlatformType::CarrotusGrass: PreloadMetadataAsync("MovingPlatform/CarrotusGrass"_s); break;
			case PlatformType::Lab: PreloadMetadataAsync("MovingPlatform/Lab"_s); break;
			case PlatformType::Sonic: PreloadMetadataAsync("MovingPlatform/Sonic"_s); break;
			case PlatformType::Spike: PreloadMetadataAsync("MovingPlatform/Spike"_s); break;
			case PlatformType::SpikeBall: PreloadMetadataAsync("MovingPlatform/SpikeBall"_s); break;
		}
	}

	Task<bool> MovingPlatform::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_type = (PlatformType)details.Params[0];

		uint8_t length = details.Params[3];
		_speed = *(int8_t*)&details.Params[2] * 0.0072f;
		uint8_t sync = details.Params[1];
		_isSwing = details.Params[4] != 0;
		_phase = sync * fPiOver2 - _speed * _levelHandler->ElapsedFrames();

		_originPos = _pos;
		_lastPos = _originPos;

		IsOneWay = true;
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::IsSolidObject | ActorState::ApplyGravitation, false);

		switch (_type) {
			default:
			case PlatformType::CarrotusFruit: async_await RequestMetadataAsync("MovingPlatform/CarrotusFruit"_s); break;
			case PlatformType::Ball: async_await RequestMetadataAsync("MovingPlatform/Ball"_s); break;
			case PlatformType::CarrotusGrass: async_await RequestMetadataAsync("MovingPlatform/CarrotusGrass"_s); break;
			case PlatformType::Lab: async_await RequestMetadataAsync("MovingPlatform/Lab"_s); break;
			case PlatformType::Sonic: async_await RequestMetadataAsync("MovingPlatform/Sonic"_s); break;
			case PlatformType::Spike: async_await RequestMetadataAsync("MovingPlatform/Spike"_s); break;
			case PlatformType::SpikeBall: async_await RequestMetadataAsync("MovingPlatform/SpikeBall"_s); break;
		}

		SetAnimation("Platform"_s);

		for (int i = 0; i < length; i++) {
			ChainPiece& piece = _pieces.emplace_back();
			piece.Command = std::make_unique<RenderCommand>();
			piece.Command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE);
			piece.Command->material().setBlendingEnabled(true);
			piece.Command->material().reserveUniformsDataMemory();
			piece.Command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

			GLUniformCache* textureUniform = piece.Command->material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
		}

		async_return true;
	}

	void MovingPlatform::OnUpdate(float timeMult)
	{
		_lastPos = _pos;
		_phase -= _speed * timeMult;
		if (_speed > 0.0f) {
			if (_phase < -fTwoPi) {
				_phase += fTwoPi;
			}
		} else if (_speed < 0.0f) {
			if (_phase > fTwoPi) {
				_phase -= fTwoPi;
			}
		}

		MoveInstantly(GetPhasePosition((int)_pieces.size()), MoveType::Absolute | MoveType::Force);

		for (int i = 0; i < _pieces.size(); i++) {
			_pieces[i].Pos = GetPhasePosition(i);
		}

		Vector2f diff = _pos - _lastPos;
		AABBf aabb = AABBInner;
		aabb.T -= 4.0f;

		if (_type != PlatformType::SpikeBall) {
			auto& players = _levelHandler->GetPlayers();
			for (auto& player : players) {
				if (player->GetCarryingObject() == this) {
					AABBf aabb3 = aabb;
					aabb3.T -= 4.0f;
					if (aabb3.Overlaps(player->AABBInner) && player->GetState(ActorState::ApplyGravitation)) {
						// Try to adjust Y, because it collides with carrying platform sometimes
						bool success = false;
						Vector2f playerPos = player->GetPos();
						Vector2f adjustedPos = Vector2f(playerPos.X + diff.X, playerPos.Y + diff.Y);
						for (int i = 0; i < 8; i++) {
							if (player->MoveInstantly(adjustedPos, MoveType::Absolute)) {
								success = true;
								break;
							}
							adjustedPos.Y -= 0.5f * timeMult;
						}

						if (!success) {
							player->MoveInstantly(Vector2f(0.0f, diff.Y), MoveType::Relative);
						}

						player->SetCarryingObject(this, true);
					} else {
						player->SetCarryingObject(nullptr);
						SetState(ActorState::IsSolidObject, false);
					}
				} else if (aabb.Overlaps(player->AABBInner) && player->GetSpeed().Y >= diff.Y * timeMult && !player->CanMoveVertically()) {
					player->SetCarryingObject(this, true);
					SetState(ActorState::IsSolidObject, true);
				}
			}

			if (_type == PlatformType::Spike) {
				AABBf aabb2 = aabb;
				aabb2.T += 40.0f;
				aabb2.B += 20.0f;

				_levelHandler->GetCollidingPlayers(aabb2, [](Actors::ActorBase* actor) {
					if (auto player = dynamic_cast<Player*>(actor)) {
						player->TakeDamage(1, 2.0f);
					}
					return true;
				});
			}
		} else {
			_levelHandler->GetCollidingPlayers(aabb, [](Actors::ActorBase* actor) {
				if (auto player = dynamic_cast<Player*>(actor)) {
					player->TakeDamage(1, 2.0f);
				}
				return true;
			});
		}
	}

	void MovingPlatform::OnUpdateHitbox()
	{
		SolidObjectBase::OnUpdateHitbox();

		switch (_type) {
			case PlatformType::CarrotusFruit:
				AABBInner.L += 10.0f;
				AABBInner.T += 2.0f;
				AABBInner.R -= 10.0f;
				AABBInner.B = AABBInner.T + 16.0f;
				break;
			case PlatformType::Lab:
				AABBInner.L += 4.0f;
				AABBInner.T -= 4.0f;
				AABBInner.R -= 4.0f;
				AABBInner.B = AABBInner.T + 16.0f;
				break;
			case PlatformType::Sonic:
				AABBInner.L += 8.0f;
				AABBInner.T -= 4.0f;
				AABBInner.R -= 8.0f;
				AABBInner.B = AABBInner.T + 16.0f;
				break;
			case PlatformType::Spike:
				AABBInner.L += 4.0f;
				AABBInner.T -= 2.0f;
				AABBInner.R -= 4.0f;
				AABBInner.B = AABBInner.T + 16.0f;
				break;
			case PlatformType::SpikeBall:
				AABBInner.L += 8.0f;
				AABBInner.T += 8.0f;
				AABBInner.R -= 8.0f;
				AABBInner.B -= 8.0f;
				break;
			default:
				AABBInner.L += 4.0f;
				AABBInner.T += 2.0f;
				AABBInner.R -= 4.0f;
				AABBInner.B = AABBInner.T + 16.0f;
				break;
		}
	}

	bool MovingPlatform::OnDraw(RenderQueue& renderQueue)
	{
		if (!_pieces.empty()) {
			auto it = _metadata->Graphics.find(String::nullTerminatedView("Chain"_s));
			if (it != _metadata->Graphics.end()) {
				auto& chainAnim = it->second;
				Vector2i texSize = chainAnim.Base->TextureDiffuse->size();

				for (int i = 0; i < _pieces.size(); i++) {
					auto command = _pieces[i].Command.get();

					int curAnimFrame = chainAnim.FrameOffset + (i % chainAnim.FrameCount);
					int col = curAnimFrame % chainAnim.Base->FrameConfiguration.X;
					int row = curAnimFrame / chainAnim.Base->FrameConfiguration.X;
					float texScaleX = (float(chainAnim.Base->FrameDimensions.X) / float(texSize.X));
					float texBiasX = (float(chainAnim.Base->FrameDimensions.X * col) / float(texSize.X));
					float texScaleY = (float(chainAnim.Base->FrameDimensions.Y) / float(texSize.Y));
					float texBiasY = (float(chainAnim.Base->FrameDimensions.Y * row) / float(texSize.Y));

					auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
					instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
					instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue((float)chainAnim.Base->FrameDimensions.X, (float)chainAnim.Base->FrameDimensions.Y);
					instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

					auto& pos = _pieces[i].Pos;
					command->setTransformation(Matrix4x4f::Translation(pos.X, pos.Y, 0.0f));
					command->setLayer(_renderer.layer() - 2);
					command->material().setTexture(*chainAnim.Base->TextureDiffuse.get());

					renderQueue.addCommand(command);
				}
			}
		}

		return SolidObjectBase::OnDraw(renderQueue);
	}

	Vector2f MovingPlatform::GetPhasePosition(int distance)
	{
		float effectivePhase = _phase;

		if (_isSwing) {
			effectivePhase = fPiOver2 + sinf(effectivePhase) * fPiOver2;
		} else if (_pieces.size() > 4 && distance > 0 && distance < _pieces.size()) {
			float shift = sinf(fPi * (float)(distance + 1) / _pieces.size()) * _speed * 4.0f;
			effectivePhase -= shift;
		}

		float multiX = cosf(effectivePhase);
		float multiY = sinf(effectivePhase);

		return Vector2f(
			std::round(_originPos.X + multiX * distance * 12.0f),
			std::round(_originPos.Y + multiY * distance * 12.0f)
		);
	}
}