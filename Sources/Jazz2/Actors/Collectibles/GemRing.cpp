#include "GemRing.h"
#include "../../ContentResolver.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Actors::Collectibles
{
	GemRing::GemRing()
		: _phase(0.0f), _collected(false), _collectedPhase(0.0f)
	{
	}

	void GemRing::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Collectible/Gems"_s);
	}

	Task<bool> GemRing::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await CollectibleBase::OnActivatedAsync(details);

		std::int32_t length = (details.Params[0] > 0 ? details.Params[0] : 8);
		_speed = (details.Params[1] > 0 ? details.Params[1] : 8) * 0.00625f;
		_untouched = false;

		SetState(ActorState::SkipPerPixelCollisions, true);

		async_await RequestMetadataAsync("Collectible/Gems"_s);

		auto& resolver = ContentResolver::Get();
		if (!resolver.IsHeadless()) {
			for (std::int32_t i = 0; i < length; i++) {
				ChainPiece& piece = _pieces.emplace_back();
				piece.Scale = 0.8f;
				piece.Command = std::make_unique<RenderCommand>(RenderCommand::Type::Sprite);
				piece.Command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite);
				piece.Command->GetMaterial().SetBlendingEnabled(true);
				piece.Command->GetMaterial().ReserveUniformsDataMemory();
				piece.Command->GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

				auto* textureUniform = piece.Command->GetMaterial().Uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->GetIntValue(0) != 0) {
					textureUniform->SetIntValue(0); // GL_TEXTURE0
				}
			}
		}

		async_return true;
	}

	void GemRing::OnUpdate(float timeMult)
	{
		if (_collected) {
			if (_collectedPhase > 100.0f) {
				DecreaseHealth(INT32_MAX);
				return;
			}

			for (int i = 0; i < _pieces.size(); i++) {
				float angle = _phase * (1.0f + _collectedPhase * 0.001f) + (i * fTwoPi / _pieces.size());
				float distance = 8 * 4 + _collectedPhase * 3.6f;
				_pieces[i].Pos = Vector2f(_pos.X + cosf(angle) * distance, _pos.Y + sinf(angle) * distance);
				_pieces[i].Angle = angle + fPiOver2;
				_pieces[i].Scale += 0.02f * timeMult;
			}

			_phase += timeMult * _speed * 3.0f;
			_collectedPhase += timeMult;
		} else {
			for (int i = 0; i < _pieces.size(); i++) {
				float angle = _phase + (i * fTwoPi / _pieces.size());
				float distance = 8 * (4 + sinf(_phase * 1.1f));
				_pieces[i].Pos = Vector2f(_pos.X + cosf(angle) * distance, _pos.Y + sinf(angle) * distance);
				_pieces[i].Angle = angle + fPiOver2;
			}

			_phase += timeMult * _speed;
		}
	}

	void GemRing::OnUpdateHitbox()
	{
		AABBInner = AABBf(
			_pos.X - 20.0f,
			_pos.Y - 20.0f,
			_pos.X + 20.0f,
			_pos.Y + 20.0f
		);
	}

	bool GemRing::OnDraw(RenderQueue& renderQueue)
	{
		if (!_pieces.empty()) {
			auto* res = _metadata->FindAnimation(AnimState::Default); // GemRed
			if (res != nullptr && res->Base->TextureDiffuse != nullptr) {
				Vector2i texSize = res->Base->TextureDiffuse->GetSize();

				for (std::int32_t i = 0; i < _pieces.size(); i++) {
					auto command = _pieces[i].Command.get();

					std::int32_t curAnimFrame = res->FrameOffset + (i % res->FrameCount);
					std::int32_t col = curAnimFrame % res->Base->FrameConfiguration.X;
					std::int32_t row = curAnimFrame / res->Base->FrameConfiguration.X;
					float texScaleX = (float(res->Base->FrameDimensions.X) / float(texSize.X));
					float texBiasX = (float(res->Base->FrameDimensions.X * col) / float(texSize.X));
					float texScaleY = (float(res->Base->FrameDimensions.Y) / float(texSize.Y));
					float texBiasY = (float(res->Base->FrameDimensions.Y * row) / float(texSize.Y));

					auto instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
					instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
					instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(res->Base->FrameDimensions.X * _pieces[i].Scale, res->Base->FrameDimensions.Y * _pieces[i].Scale);
					instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf(1.0f, 1.0f, 1.0f, 0.7f).Data());

					auto& pos = _pieces[i].Pos;
					command->SetTransformation(Matrix4x4f::Translation(pos.X, pos.Y, 0.0f).RotateZ(_pieces[i].Angle));
					command->SetLayer(_renderer.layer() - 2);
					command->GetMaterial().SetTexture(*res->Base->TextureDiffuse.get());

					renderQueue.AddCommand(command);
				}
			}
		}

		return true;
	}

	void GemRing::OnCollect(Player* player)
	{
		if (!_collected) {
			_collected = true;
			SetState(ActorState::CollideWithOtherActors, false);

			player->AddGems(0, (std::int32_t)_pieces.size());
			player->AddScore(800);
		}
	}
}