#include "SwingingVine.h"
#include "../../ContentResolver.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"

#include "../../../nCine/Graphics/RenderQueue.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Jazz2::Actors::Environment
{
	namespace
	{
		// The swing of JJ2 goes in ticks of 70 a second, the time of the game in frames of 60
		constexpr float TicksPerFrame = 70.0f / 60.0f;
	}

	SwingingVine::SwingingVine()
		: _length(DefaultLength), _position(DefaultLength * 256), _speed(0), _ticks(0), _amplitude(0.0f)
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

		// JJ2 starts a vine on the right when it's made, as measured in JJ2+ - online the clients have to agree, so all of them
		// swing from the start of the level there
		_ticks = (_levelHandler->IsLocalSession() ? std::int32_t(GetTickTime()) : 0);
		UpdateAmplitude();

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

		std::int32_t targetTicks = std::int32_t(GetTickTime());
		while (_ticks < targetTicks) {
			AdvanceSwing(_position, _speed);
			_ticks++;
		}

		UpdateChunks();
	}

	void SwingingVine::UpdateChunks()
	{
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

		float currentPhase = GetCurrentPhase();
		float lengthScale = GetLengthScale();
		for (std::int32_t i = 0; i < ChunkCount; i++) {
			float angle = sinApprox(currentPhase - i * ChunkPhaseStep) * _amplitude + fPiOver2;

			float distance = chunkDistances[i] * lengthScale;
			float sinAngle, cosAngle;
			sincosApprox(angle, sinAngle, cosAngle);
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
				float chunkAngle = sinApprox(currentPhase - (ChunkCount - 1) * ChunkPhaseStep) * _amplitude * 0.5f;
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
			float currentPhase = GetCurrentPhase();
			float chunkHeight = ChunkSize * GetLengthScale();
			auto& resolver = ContentResolver::Get();
			bool indexed = ((resBase->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed);

			// The only sprite in the game that is TILED instead of blitted - the chain is longer than the
			// artwork, so one length of vine repeats along it - which makes it the only one that cares
			// where its frame ends inside the sheet. A packed sheet is padded to power-of-two dimensions
			// (the vine's 7x66 artwork arrives in an 8x128 texture), so the old `ChunkSize / texSize.Y`
			// slicing spread the chunks across the padding and drew nothing below the halfway point.
			// `GetFrameRect()` hides the packing; the regular-grid cell it falls back to carries a
			// `GenericGraphicResource::FrameBorder` on each side, which tiling must skip or every seam gets a
			// transparent band.
			Recti frameRect = resBase->GetFrameRect(_currentAnimation->FrameOffset);
			if (resBase->FrameRects.empty()) {
				frameRect.X += GenericGraphicResource::FrameBorder;
				frameRect.Y += GenericGraphicResource::FrameBorder;
				frameRect.W -= GenericGraphicResource::FrameBorder * 2;
				frameRect.H -= GenericGraphicResource::FrameBorder * 2;
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

				float chunkAngle = sinApprox(currentPhase - i * ChunkPhaseStep) * _amplitude;

				auto instanceBlock = command->GetInstanceBlock();
				instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(chunkTexScaleX, chunkTexBiasX, chunkTexSize, chunkTexBase + chunkTexSize * (i % chunksPerRepeat));
				instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(frameWidth, chunkHeight);
				instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

				// `RotateZ()` post-multiplies, so translating first would rotate the chunk around its top-left
				// CORNER rather than its middle - which swung each chunk up to half its own width away from
				// the position `OnUpdate()` computed for it, and drew the vine as a scattered zigzag instead
				// of a chain. The order here is the engine's own (see `SceneNode::updateWorldMatrix()`):
				// translate to the position, rotate, then step back by half the sprite to centre it.
				Matrix4x4f worldMatrix = Matrix4x4f::Translation(_chunkPos[i].X, _chunkPos[i].Y, 0.0f);
				worldMatrix.RotateZ(chunkAngle);
				worldMatrix.Translate(frameWidth * -0.5f, chunkHeight * -0.5f, 0.0f);
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

	void SwingingVine::GetSwing(std::int32_t& length, std::int32_t& position, std::int32_t& speed) const
	{
		length = _length;
		position = _position;
		speed = _speed;
	}

	void SwingingVine::SetSwing(std::int32_t length, std::int32_t position, std::int32_t speed)
	{
		bool lengthChanged = (length != _length);
		_length = length;
		_position = position;
		_speed = speed;
		// It goes on from now
		_ticks = std::int32_t(GetTickTime());
		if (lengthChanged) {
			UpdateAmplitude();
		}
		UpdateChunks();
	}

	float SwingingVine::GetReleaseSpeedX() const
	{
		// JJ2 gives the player 4 px a tick the way the vine swings while hanging on it, it's kept when letting go
		return float(GetDirection()) * 4.0f * TicksPerFrame;
	}

	void SwingingVine::AdvanceSwing(std::int32_t& position, std::int32_t& speed)
	{
		// As measured in JJ2+, it speeds up by 32 a tick towards the middle
		speed += (position > 0 ? -32 : 32);
		position += speed;
	}

	Vector2f SwingingVine::GetLegacyTipOffset(std::int32_t length, std::int32_t position)
	{
		// Measured in JJ2+ on vines of 64, 128 and 192, to within 0.4 px: the rows of the vine are moved sideways by
		// `position * row^2 / 2^22`, it's 1 row shorter for every 2048 of it, the end is 20 px below the last one
		std::int32_t rows = length - (std::abs(position) >> 11);
		return Vector2f(float(position) * float(rows) * float(rows) / 4194304.0f, float(rows + 20));
	}

	float SwingingVine::GetTickTime() const
	{
		return _levelHandler->GetElapsedFrames() * TicksPerFrame;
	}

	std::int32_t SwingingVine::GetDirection() const
	{
		// At the ends of the swing either way gives the same phase
		return (_speed != 0 ? (_speed < 0 ? -1 : 1) : (_position > 0 ? -1 : 1));
	}

	float SwingingVine::GetLengthScale() const
	{
		// The end hangs as far below as in JJ2
		static const float lastChunkDistance = ChunkSize * powf(float(ChunkCount - 1), 0.95f);
		return float(std::max<std::int32_t>(_length, 1)) / lastChunkDistance;
	}

	void SwingingVine::UpdateAmplitude()
	{
		// The end reaches as far to the sides as in JJ2, as far as the swing allows
		std::int32_t length = std::max<std::int32_t>(_length, 1);
		constexpr float MaxAmplitude = 1.2f;
		constexpr float RiderLean = 22.0f * 0.5f;
		float reach = GetLegacyTipOffset(length, length * 256).X;
		float amplitude = 0.0f;
		if (reach >= float(length) * std::sin(MaxAmplitude) + RiderLean * MaxAmplitude) {
			amplitude = MaxAmplitude;
		} else if (reach > 0.0f) {
			amplitude = std::min(reach / (float(length) + RiderLean), MaxAmplitude);
			for (std::int32_t i = 0; i < 8; i++) {
				float f = float(length) * std::sin(amplitude) + RiderLean * amplitude - reach;
				float df = float(length) * std::cos(amplitude) + RiderLean;
				amplitude = std::clamp(amplitude - f / df, 0.0f, MaxAmplitude);
			}
		}
		_amplitude = amplitude;
	}

	float SwingingVine::GetCurrentPhase() const
	{
		// The end of the vine is on the side of the swing of JJ2, and the phase only grows, so where it's going decides
		// which half of the circle it's in - against the reach of this half of the swing, which the speed tells (it swings
		// out from the middle as far as it came in)
		float fraction = std::clamp(GetTickTime() - float(_ticks), 0.0f, 1.0f);
		float position = float(_position - _speed) + float(_speed) * fraction;
		float reach = float(std::abs(_position)) + float(_speed) * float(_speed) / 64.0f;
		float side = (reach > 0.0f ? std::clamp(position / reach, -1.0f, 1.0f) : 0.0f);
		float tipPhase = (GetDirection() > 0 ? fPi + std::asin(side) : -std::asin(side));
		return tipPhase + (ChunkCount - 1) * ChunkPhaseStep;
	}

	void SwingingVine::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/SwingingVine"_s);
	}
}