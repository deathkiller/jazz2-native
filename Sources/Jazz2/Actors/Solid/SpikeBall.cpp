#include "SpikeBall.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Actors::Solid
{
	SpikeBall::SpikeBall()
	{
	}

	void SpikeBall::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("MovingPlatform/SpikeBall"_s);
	}

	Task<bool> SpikeBall::OnActivatedAsync(const ActorActivationDetails& details)
	{
		uint8_t length = details.Params[2];
		_speed = *(int8_t*)&details.Params[1] * 0.0072f;
		uint8_t sync = details.Params[0];
		_isSwing = details.Params[3] != 0;
		_phase = sync * fPiOver2 - _speed * _levelHandler->ElapsedFrames();
		_shade = details.Params[4] != 0;

		_originPos = _pos;
		_originLayer = _renderer.layer() - 12;

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("MovingPlatform/SpikeBall"_s);
		SetAnimation((AnimState)0);

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

	void SpikeBall::OnUpdate(float timeMult)
	{
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

		float scale = 1.0f;
		MoveInstantly(GetPhasePosition((int)_pieces.size(), &scale), MoveType::Absolute | MoveType::Force);

		for (int i = 0; i < _pieces.size(); i++) {
			_pieces[i].Pos = GetPhasePosition(i, &_pieces[i].Scale);
		}

		_canHurtPlayer = (std::abs(1.0f - scale) < 0.06f);

		_renderer.setScale(scale);
		_renderer.setLayer(_originLayer + (uint16_t)(scale * 20));
		if (_shade) {
			_renderer.setColor(scale < 1.0f ? Colorf(scale, scale, scale, 1.0f) : Colorf::White);
		}
	}

	void SpikeBall::OnUpdateHitbox()
	{
		EnemyBase::OnUpdateHitbox();

		AABBInner.L += 8.0f;
		AABBInner.T += 8.0f;
		AABBInner.R -= 8.0f;
		AABBInner.B -= 8.0f;
	}

	bool SpikeBall::OnDraw(RenderQueue& renderQueue)
	{
		if (!_pieces.empty()) {
			auto* chainAnim = _metadata->FindAnimation((AnimState)1);
			if (chainAnim != nullptr) {
				Vector2i texSize = chainAnim->Base->TextureDiffuse->size();

				for (int i = 0; i < _pieces.size(); i++) {
					auto command = _pieces[i].Command.get();
					float scale = _pieces[i].Scale;

					int curAnimFrame = chainAnim->FrameOffset + (i % chainAnim->FrameCount);
					int col = curAnimFrame % chainAnim->Base->FrameConfiguration.X;
					int row = curAnimFrame / chainAnim->Base->FrameConfiguration.X;
					float texScaleX = (float(chainAnim->Base->FrameDimensions.X) / float(texSize.X));
					float texBiasX = (float(chainAnim->Base->FrameDimensions.X * col) / float(texSize.X));
					float texScaleY = (float(chainAnim->Base->FrameDimensions.Y) / float(texSize.Y));
					float texBiasY = (float(chainAnim->Base->FrameDimensions.Y * row) / float(texSize.Y));

					auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
					instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
					instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue((float)chainAnim->Base->FrameDimensions.X, (float)chainAnim->Base->FrameDimensions.Y);
					if (_shade) {
						instanceBlock->uniform(Material::ColorUniformName)->setFloatVector((scale < 1.0f ? Colorf(scale, scale, scale, 1.0f) : Colorf::White).Data());
					} else {
						instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());
					}

					auto& pos = _pieces[i].Pos;
					command->setTransformation(Matrix4x4f::Translation(pos.X - chainAnim->Base->FrameDimensions.X / 2, pos.Y - chainAnim->Base->FrameDimensions.Y / 2, 0.0f));
					command->setLayer(_originLayer + (uint16_t)(scale * 20));
					command->material().setTexture(*chainAnim->Base->TextureDiffuse.get());

					renderQueue.addCommand(command);
				}
			}
		}

		return EnemyBase::OnDraw(renderQueue);
	}

	Vector2f SpikeBall::GetPhasePosition(int distance, float* scale)
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

		if (scale != nullptr) {
			*scale = 1.0f + multiX * 0.4f * distance / _pieces.size();
		}
		return Vector2f(
			std::round(_originPos.X),
			std::round(_originPos.Y + multiY * distance * 12.0f)
		);
	}
}