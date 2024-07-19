#include "SwingingVine.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"

#include "../../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Actors::Environment
{
	SwingingVine::SwingingVine()
		:
		_angle(0.0f),
		_phase(0.0f),
		_justTurned(false)
	{
	}

	SwingingVine::~SwingingVine()
	{
		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			player->CancelCarryingObject(this);
		}
	}

	Task<bool> SwingingVine::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Object/SwingingVine"_s);

		SetAnimation(AnimState::Default);

		_renderer.AnimPaused = true;

		if (_currentAnimation != nullptr) {
			_currentAnimation->Base->TextureDiffuse->setWrap(SamplerWrapping::Repeat);
		}

		for (int i = 0; i < ChunkCount; i++) {
			_chunks[i] = std::make_unique<RenderCommand>(RenderCommand::Type::Sprite);
			_chunks[i]->material().setShaderProgramType(Material::ShaderProgramType::Sprite);
			_chunks[i]->material().setBlendingEnabled(true);
			_chunks[i]->material().reserveUniformsDataMemory();
			_chunks[i]->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			_chunks[i]->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			GLUniformCache* textureUniform = _chunks[i]->material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
		}

		async_return true;
	}

	void SwingingVine::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		for (int i = 0; i < ChunkCount; i++) {
			_angle = sinf(_phase - i * (0.64f / ChunkCount)) * 1.2f + fPiOver2;

			float distance = ChunkSize * powf(i, 0.95f);
			_chunkPos[i].X = _pos.X + cosf(_angle) * distance;
			_chunkPos[i].Y = _pos.Y + sinf(_angle) * distance;
		}

		auto& lastChunk = _chunkPos[ChunkCount - 1];
		AABBInner = AABBf(lastChunk.X - 10.0f, lastChunk.Y - 10.0f, lastChunk.X + 10.0f, lastChunk.Y + 10.0f);

		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			if (player->GetCarryingObject() == this) {
				float chunkAngle = sinf(_phase - ChunkCount * 0.08f) * 0.6f;
				Vector2f prevPos = player->GetPos();
				Vector2 newPos = lastChunk + Vector2(chunkAngle * -22.0f, 20.0f + std::abs(chunkAngle) * -10.0f);
				player->MoveInstantly(newPos, MoveType::Absolute);

				if (_justTurned) {
					// TODO: `SwingingVine::OnUpdate()` is called after `Player::OnUpdate()`, so this must be called one frame later
					player->SetFacingLeft(!player->IsFacingLeft());
					_justTurned = false;
				} else if (player->IsFacingLeft()) {
					if (newPos.X > prevPos.X) {
						player->_renderer.AnimTime = 0.0f;
						_justTurned = true;
					}
				} else {
					if (newPos.X < prevPos.X) {
						player->_renderer.AnimTime = 0.0f;
						_justTurned = true;
					}
				}

				player->_renderer.setRotation(chunkAngle);
			}
		}

		_phase += timeMult * 0.04f;

		SetState(ActorState::IsDirty, true);
	}

	void SwingingVine::OnUpdateHitbox()
	{
	}

	bool SwingingVine::OnDraw(RenderQueue& renderQueue)
	{
		if (_currentAnimation != nullptr) {
			auto& resBase = _currentAnimation->Base;
			Vector2i texSize = resBase->TextureDiffuse->size();

			for (int i = 0; i < ChunkCount; i++) {
				auto command = _chunks[i].get();

				float chunkTexSize = ChunkSize / texSize.Y;
				float chunkAngle = sinf(_phase - i * 0.08f) * 1.2f;

				auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
				instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, chunkTexSize, chunkTexSize * i);
				instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(texSize.X, ChunkSize);
				instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

				Matrix4x4f worldMatrix = Matrix4x4f::Translation(_chunkPos[i].X - texSize.X / 2, _chunkPos[i].Y - ChunkSize / 2, 0.0f);
				worldMatrix.RotateZ(chunkAngle);
				command->setTransformation(worldMatrix);
				command->setLayer(_renderer.layer());
				command->material().setTexture(*resBase->TextureDiffuse);

				renderQueue.addCommand(command);
			}
		}

		return true;
	}

	bool SwingingVine::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* player = runtime_cast<Player*>(other)) {
			if (player->_springCooldown <= 0.0f) {
				player->UpdateCarryingObject(this, SuspendType::SwingingVine);
			}
			return true;
		}

		return false;
	}
}