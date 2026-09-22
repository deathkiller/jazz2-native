#include "SwingingVine.h"
#include "../../ContentResolver.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"

#include "../../../nCine/Graphics/RenderQueue.h"

#include <array>

namespace Jazz2::Actors::Environment
{
	SwingingVine::SwingingVine()
		: _angle(0.0f), _phase(0.0f)
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

		_phase = _originTile.X + _originTile.Y * 0.5f;

		_renderer.AnimPaused = true;

		auto& resolver = ContentResolver::Get();
		if (!resolver.IsHeadless()) {
			for (std::int32_t i = 0; i < ChunkCount; i++) {
				_chunks[i] = std::make_unique<RenderCommand>(RenderCommand::Type::Sprite);
				_chunks[i]->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite);
				_chunks[i]->GetMaterial().SetBlendingEnabled(true);
				_chunks[i]->GetMaterial().ReserveUniformsDataMemory();
				_chunks[i]->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
				_chunks[i]->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);

				auto* textureUniform = _chunks[i]->GetMaterial().Uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->GetIntValue(0) != 0) {
					textureUniform->SetIntValue(0); // GL_TEXTURE0
				}
			}
		}

		async_return true;
	}

	void SwingingVine::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		// The distance of each chunk along the vine depends only on its index, so the `powf()` is paid once
		// per process instead of sixteen times a frame per vine - and the angles go through the cheap
		// approximations, which are exact libm calls everywhere but on the consoles, where a libm `sinf()`
		// costs thousands of cycles and this loop alone was five of them per chunk (80 per vine per frame)
		static const auto chunkDistances = [] {
			std::array<float, ChunkCount> distances;
			for (std::int32_t i = 0; i < ChunkCount; i++) {
				distances[i] = ChunkSize * powf((float)i, 0.95f);
			}
			return distances;
		}();

		float currentPhase = _phase + 0.04f * _levelHandler->GetElapsedFrames();
		for (std::int32_t i = 0; i < ChunkCount; i++) {
			_angle = sinApprox(currentPhase - i * ChunkPhaseStep) * 1.2f + fPiOver2;

			float distance = chunkDistances[i];
			float sinAngle, cosAngle;
			sincosApprox(_angle, sinAngle, cosAngle);
			_chunkPos[i].X = _pos.X + cosAngle * distance;
			_chunkPos[i].Y = _pos.Y + sinAngle * distance;
		}

		auto& lastChunk = _chunkPos[ChunkCount - 1];
		AABBInner = AABBf(lastChunk.X - 10.0f, lastChunk.Y - 10.0f, lastChunk.X + 10.0f, lastChunk.Y + 10.0f);

		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			if (player->GetCarryingObject() == this) {
				// The phase of the chunk the player is actually hanging from - `lastChunk` below is
				// `_chunkPos[ChunkCount - 1]`, so the offset has to be that index, not the count. Taking it
				// one step further along evaluated a phase the tip of the vine never has, which let the
				// rabbit lean and slide against the chunk they are holding at the ends of the swing.
				float chunkAngle = sinApprox(currentPhase - (ChunkCount - 1) * ChunkPhaseStep) * 0.6f;
				Vector2f prevPos = player->GetPos();
				Vector2 newPos = lastChunk + Vector2(chunkAngle * -22.0f, 20.0f + std::abs(chunkAngle) * -10.0f);
				player->MoveInstantly(newPos, MoveType::Absolute);

				// Turned on the tick the swing reverses, not the tick after: deferring it drew one frame
				// with the animation already restarted and the sprite still facing the old way, which is
				// the snag visible at each end of the swing. Assigned from the direction rather than
				// toggled, so two players sharing a vine cannot drift out of step. The deadzone is what
				// the apex turns on - `newPos.X` passes through zero there, and rounding noise alone
				// would flip the rabbit back and forth for the few frames either side of it.
				float deltaX = newPos.X - prevPos.X;
				if (std::abs(deltaX) > TurnDeadzone) {
					bool facingLeft = (deltaX < 0.0f);
					if (player->IsFacingLeft() != facingLeft) {
						player->SetFacingLeft(facingLeft);
						player->_renderer.AnimTime = 0.0f;
					}
				}

				player->_renderer.setRotation(chunkAngle);
			}
		}

		SetState(ActorState::IsDirty, true);
	}

	void SwingingVine::OnUpdateHitbox()
	{
	}

	bool SwingingVine::OnDraw(RenderQueue& renderQueue)
	{
		if (_currentAnimation != nullptr) {
			auto& resBase = _currentAnimation->Base;
			Vector2i texSize = resBase->TextureDiffuse->GetSize();
			float currentPhase = _phase + 0.04f * _levelHandler->GetElapsedFrames();
			auto& resolver = ContentResolver::Get();
			bool indexed = ((resBase->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed);

			// The only sprite in the game that is TILED instead of blitted - the chain is longer than the
			// artwork, so one length of vine repeats along it - which makes it the only one that cares
			// where its frame ends inside the sheet. A packed sheet is padded to power-of-two dimensions
			// (the vine's 7x66 artwork arrives in an 8x128 texture), so the old `ChunkSize / texSize.Y`
			// slicing spread the chunks across the padding and drew nothing below the halfway point.
			// `GetFrameRect()` hides the packing; the regular-grid cell it falls back to is two pixels
			// bigger on each side (`JJ2Anims::AddBorder`), which tiling must skip or every seam gets a
			// transparent band.
			Recti frameRect = resBase->GetFrameRect(_currentAnimation->FrameOffset);
			if (resBase->FrameRects.empty()) {
				frameRect.X += (std::int32_t)SpriteBorder;
				frameRect.Y += (std::int32_t)SpriteBorder;
				frameRect.W -= (std::int32_t)SpriteBorder * 2;
				frameRect.H -= (std::int32_t)SpriteBorder * 2;
			}

			// Rounding the repeat to a whole number of chunks puts the seam on a chunk boundary and keeps
			// every slice inside the frame, so nothing depends on the wrap mode - which matters because a
			// sheet is not always a power of two, and ES2-class profiles may only clamp those.
			float frameWidth = (float)std::max<std::int32_t>(1, frameRect.W);
			float frameHeight = (float)std::max<std::int32_t>(1, frameRect.H);
			std::int32_t chunksPerRepeat = std::max<std::int32_t>(1, (std::int32_t)(frameHeight / ChunkSize + 0.5f));
			float chunkTexScaleX = frameWidth / texSize.X;
			float chunkTexBiasX = frameRect.X / (float)texSize.X;
			float chunkTexSize = frameHeight / (chunksPerRepeat * (float)texSize.Y);
			float chunkTexBase = frameRect.Y / (float)texSize.Y;

			for (std::int32_t i = 0; i < ChunkCount; i++) {
				auto command = _chunks[i].get();
				resolver.ConfigureSpriteShader(*command, indexed);

				float chunkAngle = sinApprox(currentPhase - i * ChunkPhaseStep) * 1.2f;

				auto instanceBlock = command->GetInstanceBlock();
				instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(chunkTexScaleX, chunkTexBiasX, chunkTexSize, chunkTexBase + chunkTexSize * (i % chunksPerRepeat));
				instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(frameWidth, ChunkSize);
				instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

				// `RotateZ()` post-multiplies, so translating first would rotate the chunk around its top-left
				// CORNER rather than its middle - which swung each chunk up to half its own width away from
				// the position `OnUpdate()` computed for it, and drew the vine as a scattered zigzag instead
				// of a chain. The order here is the engine's own (see `SceneNode::updateWorldMatrix()`):
				// translate to the position, rotate, then step back by half the sprite to centre it.
				Matrix4x4f worldMatrix = Matrix4x4f::Translation(_chunkPos[i].X, _chunkPos[i].Y, 0.0f);
				worldMatrix.RotateZ(chunkAngle);
				worldMatrix.Translate(frameWidth * -0.5f, ChunkSize * -0.5f, 0.0f);
				command->SetTransformation(worldMatrix);
				command->SetLayer(_renderer.layer());
				resolver.BindSpritePalette(*command, *resBase->TextureDiffuse, indexed, _currentAnimation->PaletteOffset);

				renderQueue.AddCommand(command);
			}
		}

		return true;
	}

	bool SwingingVine::OnHandleCollision(ActorBase* other)
	{
		if (auto* player = runtime_cast<Player>(other)) {
			if (player->_springCooldown <= 0.0f) {
				player->UpdateCarryingObject(this, SuspendType::SwingingVine);
			}
			return true;
		}

		return false;
	}

	void SwingingVine::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/SwingingVine"_s);
	}
}