#include "Rapier.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Enemies
{
	Rapier::Rapier()
		:
		_anglePhase(0.0f),
		_attackTime(80.0f),
		_attacking(false),
		_noiseCooldown(Random().FastFloat(200.0f, 400.0f))
	{
	}

	void Rapier::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Rapier"_s);
	}

	Task<bool> Rapier::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(2);
		_scoreValue = 300;

		_originPos = _lastPos = _targetPos = _pos;

		async_await RequestMetadataAsync("Enemy/Rapier"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Rapier::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();
		HandleBlinking(timeMult);
		UpdateFrozenState(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (_currentTransition == nullptr) {
			if (_attackTime > 0.0f) {
				_attackTime -= timeMult;
			} else {
				if (_attacking) {
					SetAnimation(AnimState::Idle);
					SetTransition((AnimState)1073741826, false, [this]() {
						_targetPos = _originPos;

						_attackTime = Random().NextFloat(200.0f, 260.0f);
						_attacking = false;
					});
				} else {
					AttackNearestPlayer();
				}
			}

			if (_noiseCooldown > 0.0f) {
				_noiseCooldown -= timeMult;
			} else {
				_noiseCooldown = Random().FastFloat(300.0f, 600.0f);

				if (Random().NextFloat() < 0.5f) {
					PlaySfx("Noise"_s, 0.7f);
				}
			}
		}

		_anglePhase += timeMult * 0.02f;

		float speedX = ((_targetPos.X - _lastPos.X) * timeMult / 26.0f + _lastSpeed.X * 1.4f) / 2.4f;
		float speedY = ((_targetPos.Y - _lastPos.Y) * timeMult / 26.0f + _lastSpeed.Y * 1.4f) / 2.4f;
		_lastPos.X += speedX;
		_lastPos.Y += speedY;
		_lastSpeed = Vector2f(speedX, speedY);

		bool willFaceLeft = (_speed.X < 0.0f);
		if (IsFacingLeft() != willFaceLeft) {
			SetTransition(AnimState::TransitionTurn, false, [this, willFaceLeft]() {
				SetFacingLeft(willFaceLeft);
			});
		}

		MoveInstantly(_lastPos + Vector2f(cosf(_anglePhase) * 10.0f, sinf(_anglePhase * 2.0f) * 10.0f), MoveType::Absolute | MoveType::Force);
	}

	bool Rapier::OnPerish(ActorBase* collider)
	{
		auto tilemap = _levelHandler->TileMap();
		if (tilemap != nullptr) {
			constexpr int DebrisSize = 2;

			GraphicResource* res = (_currentTransition != nullptr ? _currentTransition : _currentAnimation);
			Vector2i texSize = res->Base->TextureDiffuse->size();

			float x = _pos.X - res->Base->Hotspot.X;
			float y = _pos.Y - res->Base->Hotspot.Y;

			for (int fy = 0; fy < res->Base->FrameDimensions.Y; fy += DebrisSize + 1) {
				for (int fx = 0; fx < res->Base->FrameDimensions.X; fx += DebrisSize + 1) {
					float currentSize = DebrisSize * Random().FastFloat(0.4f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris = { };
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - fx : fx), y + fy);
					debris.Depth = _renderer.layer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - res->Base->FrameDimensions.X / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 5.0f) / res->Base->FrameDimensions.X,
						 ((fy - res->Base->FrameDimensions.Y / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 5.0f) / res->Base->FrameDimensions.Y);
					debris.Acceleration = Vector2f::Zero;

					debris.Scale = 1.2f;
					debris.ScaleSpeed = -0.004f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.01f;

					debris.Time = 280.0f;

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = (((float)(_renderer.CurrentFrame % res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.X) + ((float)fx / float(texSize.X)));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = (((float)(_renderer.CurrentFrame / res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.Y) + ((float)fy / float(texSize.Y)));

					debris.DiffuseTexture = res->Base->TextureDiffuse.get();
					debris.Flags = Tiles::TileMap::DebrisFlags::Disappear;

					tilemap->CreateDebris(debris);
				}
			}
		}

		PlaySfx("Die"_s);
		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	void Rapier::AttackNearestPlayer()
	{
		bool found = false;
		Vector2f foundPos = Vector2f(FLT_MAX, FLT_MAX);

		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			Vector2f newPos = player->GetPos();
			if ((_lastPos - newPos).SqrLength() < (_lastPos - foundPos).SqrLength()) {
				foundPos = newPos;
				found = true;
			}
		}

		Vector2f diff = (foundPos - _lastPos);
		if (found && diff.Length() <= 200.0f) {
			SetAnimation((AnimState)1073741824);
			SetTransition((AnimState)1073741825, false, [this, foundPos]() {
				_targetPos = foundPos;

				_attackTime = 80.0f;
				_attacking = true;

				PlaySfx("Attack"_s, 0.7f);
			});
		}
	}
}