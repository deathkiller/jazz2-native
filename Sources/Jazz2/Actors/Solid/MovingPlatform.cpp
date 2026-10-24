#include "MovingPlatform.h"
#include "../../ContentResolver.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"

#include "../../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Actors::Solid
{
	MovingPlatform::MovingPlatform()
	{
	}

	MovingPlatform::~MovingPlatform()
	{
		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			player->CancelCarryingObject(this);
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
		EventParamsReader params(details);
		_type = (PlatformType)params.GetUint8(0);

		std::uint8_t length = params.GetUint8(3);
		_speed = params.GetInt8(2) * SpeedUnit;
		std::uint8_t sync = params.GetUint8(1);
		_isSwing = params.GetBool(4);
		_phase = sync * fPiOver2 - _speed * _levelHandler->GetElapsedFrames();

		_originPos = _pos;
		_lastPos = _originPos;

		IsOneWay = true;
		SetState(ActorState::CollideWithTileset | ActorState::IsSolidObject | ActorState::ApplyGravitation, false);

		switch (_type) {
			default:
			case PlatformType::CarrotusFruit: async_await RequestMetadataAsync("MovingPlatform/CarrotusFruit"_s); break;
			case PlatformType::Ball: async_await RequestMetadataAsync("MovingPlatform/Ball"_s); break;
			case PlatformType::CarrotusGrass: async_await RequestMetadataAsync("MovingPlatform/CarrotusGrass"_s); break;
			case PlatformType::Lab: async_await RequestMetadataAsync("MovingPlatform/Lab"_s); break;
			case PlatformType::Sonic: async_await RequestMetadataAsync("MovingPlatform/Sonic"_s); break;
			case PlatformType::Spike: async_await RequestMetadataAsync("MovingPlatform/Spike"_s); break;
			case PlatformType::SpikeBall:
				async_await RequestMetadataAsync("MovingPlatform/SpikeBall"_s);
				_maxHealth = _health = 8;
				break;
		}

		SetAnimation((AnimState)0);

		auto& resolver = ContentResolver::Get();
		for (std::int32_t i = 0; i < length; i++) {
			ChainPiece& piece = _pieces.emplace_back();
			if (!resolver.IsHeadless()) {
				piece.Command = std::make_unique<RenderCommand>(RenderCommand::Type::Sprite);
				piece.Command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite);
				piece.Command->GetMaterial().SetBlendingEnabled(true);
				piece.Command->GetMaterial().ReserveUniformsDataMemory();
				piece.Command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

				auto* textureUniform = piece.Command->GetMaterial().Uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->GetIntValue(0) != 0) {
					textureUniform->SetIntValue(0); // GL_TEXTURE0
				}
			}
		}

		async_return true;
	}

	void MovingPlatform::OnUpdate(float timeMult)
	{
		UpdateFrozenState(timeMult);

		_lastPos = _pos;
		if (_frozenTimeLeft <= 0.0f) {
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

			MoveInstantly(GetPhasePosition((std::int32_t)_pieces.size()), MoveType::Absolute | MoveType::Force);

			for (int i = 0; i < _pieces.size(); i++) {
				_pieces[i].Pos = GetPhasePosition(i);
			}
		}

		Vector2f diff = _pos - _lastPos;
		AABBf aabb = AABBInner;
		aabb.T -= 4.0f;

		if (_bodyHidden) {
			// Only the chain is there, nothing to stand on
			for (auto* player : _levelHandler->GetPlayers()) {
				if (player->GetCarryingObject() == this) {
					player->CancelCarryingObject();
				}
			}
			SetState(ActorState::IsSolidObject, false);
		} else if (_type != PlatformType::SpikeBall) {
			auto players = _levelHandler->GetPlayers();
			for (auto* player : players) {
				if (player->GetCarryingObject() == this) {
					AABBf aabb3 = aabb;
					aabb3.T -= 4.0f;
					if (aabb3.Overlaps(player->AABBInner) && player->GetState(ActorState::ApplyGravitation)) {
						// The feet stand right on the top, the player moves along with the platform - checked against the
						// tiles only, the platform would stop it itself (it was lifted up step by step and fell back, it
						// shook and hovered above the platform), a wall stops it going along, otherwise it falls off
						Vector2f playerPos = player->GetPos();
						float standingY = AABBInner.T - (player->AABBInner.B - playerPos.Y);
						float carriedX = playerPos.X + diff.X;
						if (player->GetSpeed().X == 0.0f) {
							// Whole pixels off the platform while standing still - both are drawn at whole pixels, at a fraction
							// apart they took turns stepping and the player wobbled on it by a pixel
							carriedX = _pos.X + std::round(carriedX - _pos.X);
						}
						if (TryCarryPlayer(player, Vector2f(carriedX, standingY)) ||
							TryCarryPlayer(player, Vector2f(playerPos.X, standingY))) {
							player->UpdateCarryingObject(this);
						} else {
							player->CancelCarryingObject();
							SetState(ActorState::IsSolidObject, false);
						}
					} else {
						player->CancelCarryingObject();
						SetState(ActorState::IsSolidObject, false);
					}
				} else if (aabb.Overlaps(player->AABBInner) && player->GetSpeed().Y >= diff.Y * timeMult && !player->CanMoveVertically() &&
					player->AABBInner.B - player->GetSpeed().Y * timeMult <= aabb.T - diff.Y + 8.0f) {
					// Caught only from above (the last condition), a player the platform swings into from a side would be caught
					// inside it, where it couldn't carry them anymore
					player->UpdateCarryingObject(this);
					SetState(ActorState::IsSolidObject, true);
				}
			}

			if (_type == PlatformType::Spike) {
				AABBf aabb2 = aabb;
				aabb2.T += 40.0f;
				aabb2.B += 20.0f;

				_levelHandler->GetCollidingPlayers(aabb2, [](Actors::ActorBase* actor) {
					if (auto* player = runtime_cast<Player>(actor)) {
						if (player->GetSpeed().Y < 0.0f) {
							player->TakeDamage(1, 2.0f);
						}
					}
					return true;
				});
			}
		} else {
			_levelHandler->GetCollidingPlayers(aabb, [](Actors::ActorBase* actor) {
				if (auto* player = runtime_cast<Player>(actor)) {
					player->TakeDamage(1, 2.0f);
				}
				return true;
			});
		}
	}

	void MovingPlatform::OnSerializeState(Stream& dest)
	{
		SolidObjectBase::OnSerializeState(dest);

		dest.WriteValueAsLE<float>(_phase);
	}

	void MovingPlatform::OnDeserializeState(Stream& src)
	{
		SolidObjectBase::OnDeserializeState(src);

		_phase = src.ReadValueAsLE<float>();

		// The platform is solid only while it carries a player, who attaches again on the next update
		SetState(ActorState::IsSolidObject, false);

		// The chain is positioned only by OnUpdate(), which skips it while the platform is frozen
		_lastPos = _pos;
		for (std::int32_t i = 0; i < (std::int32_t)_pieces.size(); i++) {
			_pieces[i].Pos = GetPhasePosition(i);
		}
	}

	bool MovingPlatform::OnHandleCollision(ActorBase* other)
	{
		if (_type == PlatformType::SpikeBall && _health > 0) {
			if (auto* shotBase = runtime_cast<Weapons::ShotBase>(other)) {
				if (shotBase->GetStrength() > 0) {
					DecreaseHealth(shotBase->GetStrength(), shotBase);
					shotBase->DecreaseHealth(INT32_MAX);
					return true;
				}
			}
		}

		return SolidObjectBase::OnHandleCollision(other);
	}

	bool MovingPlatform::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(ParticleDebrisEffect::Standard, Vector2f::Zero);

		return SolidObjectBase::OnPerish(collider);
	}

	bool MovingPlatform::TryCarryPlayer(Player* player, Vector2f pos)
	{
		if (auto* tiles = _levelHandler->TileMap()) {
			Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, false };
			if (!tiles->IsTileEmpty(player->AABBInner + (pos - player->GetPos()), params)) {
				return false;
			}
		}
		return player->MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
	}

	void MovingPlatform::OnUpdateHitbox()
	{
		SolidObjectBase::OnUpdateHitbox();

		switch (_type) {
			case PlatformType::CarrotusFruit:
				AABBInner.L -= 10.0f;
				AABBInner.R += 10.0f;
				break;
			case PlatformType::Lab:
				AABBInner.L -= 18.0f;
				AABBInner.R += 18.0f;
				break;
			case PlatformType::Sonic:
				AABBInner.L -= 14.0f;
				AABBInner.R += 14.0f;
				break;
			case PlatformType::Spike:
				AABBInner.L -= 16.0f;
				AABBInner.R += 16.0f;
				break;
			case PlatformType::SpikeBall:
				AABBInner.L -= 8.0f;
				AABBInner.T -= 8.0f;
				AABBInner.R += 8.0f;
				AABBInner.B += 8.0f;
				return;
			default:
				AABBInner.L -= 8.0f;
				AABBInner.R += 8.0f;
				break;
		}

		// As measured in JJ2+, a player stands 24 pixels above any platform whatever it looks like (fruit, grass and spike
		// alike), the feet 5 pixels above the position of it here
		AABBInner.T = _pos.Y - StandingTop;
		AABBInner.B = AABBInner.T + (_type == PlatformType::Spike ? 20.0f : 16.0f);
	}

	bool MovingPlatform::OnDraw(RenderQueue& renderQueue)
	{
		if (!_pieces.empty()) {
			auto* chainAnim = _metadata->FindAnimation((AnimState)1); // Chain
			if (chainAnim != nullptr && chainAnim->Base->TextureDiffuse != nullptr) {
				Vector2i texSize = chainAnim->Base->TextureDiffuse->GetSize();
				auto& resolver = ContentResolver::Get();
				bool chainIndexed = ((chainAnim->Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed);

				for (std::int32_t i = 0; i < (std::int32_t)_pieces.size(); i++) {
					auto* command = _pieces[i].Command.get();
					resolver.ConfigureSpriteShader(*command, chainIndexed);

					std::int32_t curAnimFrame = chainAnim->FrameOffset + (i % chainAnim->FrameCount);
					Recti frameRect = chainAnim->Base->GetFrameRect(curAnimFrame);
					float texScaleX = (float(frameRect.W) / float(texSize.X));
					float texBiasX = (float(frameRect.X) / float(texSize.X));
					float texScaleY = (float(frameRect.H) / float(texSize.Y));
					float texBiasY = (float(frameRect.Y) / float(texSize.Y));

					auto* instanceBlock = command->GetInstanceBlock();
					instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
					instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue((float)frameRect.W, (float)frameRect.H);
					instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

					auto pos = _pieces[i].Pos;
					command->SetTransformation(Matrix4x4f::Translation(pos.X - frameRect.W / 2, pos.Y - frameRect.H / 2, 0.0f));
					command->SetLayer(_renderer.layer() - 2);
					resolver.BindSpritePalette(*command, *chainAnim->Base->TextureDiffuse.get(), chainIndexed, chainAnim->PaletteOffset);

					renderQueue.AddCommand(command);
				}
			}
		}

		if (_bodyHidden) {
			return true;
		}

		return SolidObjectBase::OnDraw(renderQueue);
	}

	Vector2f MovingPlatform::GetPhasePosition(std::int32_t distance)
	{
		float effectivePhase = _phase;

		if (_isSwing) {
			effectivePhase = fPiOver2 + sinApprox(effectivePhase) * fPiOver2;
		} else if (_pieces.size() > 4 && distance > 0 && distance < _pieces.size()) {
			float shift = sinApprox(fPi * (float)(distance + 1) / _pieces.size()) * _speed * 4.0f;
			effectivePhase -= shift;
		}

		float multiX, multiY;
		sincosApprox(effectivePhase, multiY, multiX);

		float linkDistance;
		switch (_type) {
			case PlatformType::CarrotusFruit: linkDistance = 13.0f; break;
			case PlatformType::CarrotusGrass: linkDistance = 25.0f; break;
			case PlatformType::Lab: linkDistance = 12.0f; break;
			default: linkDistance = 11.0f; break;
		}

		return Vector2f(
			_originPos.X + multiX * distance * linkDistance,
			_originPos.Y + multiY * distance * linkDistance
		);
	}
}