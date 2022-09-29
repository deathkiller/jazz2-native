#include "Bilsy.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"

#include "../../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Bosses
{
	Bilsy::Bilsy()
		:
		_state(StateWaiting),
		_stateTime(0.0f),
		_endText(0)
	{
	}

	void Bilsy::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			case 0:
			default:
				PreloadMetadataAsync("Boss/Bilsy"_s);
				break;
			case 1: // Xmas
				PreloadMetadataAsync("Boss/BilsyXmas"_s);
				break;
		}
	}

	Task<bool> Bilsy::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_theme = details.Params[0];
		_endText = details.Params[1];

		_originPos = _pos;

		SetHealthByDifficulty(120);
		_scoreValue = 3000;

		SetState(ActorState::ApplyGravitation, false);

		switch (_theme) {
			case 0:
			default:
				co_await RequestMetadataAsync("Boss/Bilsy"_s);
				break;
			case 1: // Xmas
				co_await RequestMetadataAsync("Boss/BilsyXmas"_s);
				break;
		}

		SetAnimation(AnimState::Idle);

		_renderer.setDrawEnabled(false);

		co_return true;
	}

	bool Bilsy::OnActivatedBoss()
	{
		Teleport();
		return true;
	}

	void Bilsy::OnUpdate(float timeMult)
	{
		BossBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		switch (_state) {
			case StateWaiting: {
				if (_stateTime <= 0.0f) {
					_state = StateTransition;
					SetTransition((AnimState)1073741826, false, [this]() {
						PlaySfx("ThrowFireball"_s);

						std::shared_ptr<Fireball> fireball = std::make_shared<Fireball>();
						uint8_t fireballParams[2] = { _theme, (uint8_t)(IsFacingLeft() ? 1 : 0) };
						fireball->OnActivated({
							.LevelHandler = _levelHandler,
							.Pos = Vector3i((int)_pos.X + (IsFacingLeft() ? -26 : 26), (int)_pos.Y - 20, _renderer.layer() + 2),
							.Params = fireballParams
						});
						_levelHandler->AddActor(fireball);

						SetTransition((AnimState)1073741827, false, [this]() {
							_state = StateWaiting2;
							_stateTime = 30.0f;
						});
					});
				}
				break;
			}
			case StateWaiting2: {
				if (_stateTime <= 0.0f) {
					SetState(ActorState::CanBeFrozen, false);

					PlaySfx("Disappear"_s, 0.8f);

					_state = StateTransition;
					SetTransition((AnimState)1073741825, false, [this]() {
						Teleport();
					});
				}
				break;
			}
		}

		_stateTime -= timeMult;
	}

	void Bilsy::OnUpdateHitbox()
	{
		UpdateHitbox(20, 60);
	}

	bool Bilsy::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		StringView text = _levelHandler->GetLevelText(_endText, -1, '|');
		_levelHandler->ShowLevelText(text);

		return BossBase::OnPerish(collider);
	}

	void Bilsy::Teleport()
	{
		for (int i = 0; i < 20; i++) {
			Vector2f pos = Vector2f(_originPos.X + Random().NextFloat(-320.0f, 320.0f), _originPos.Y + Random().NextFloat(-240.0f, 240.0f));
			if (MoveInstantly(pos, MoveType::Absolute)) {
				break;
			}
		}

		OnUpdateHitbox();

		int j = 60;
		while (j-- > 0 && MoveInstantly(Vector2f(0.0f, 4.0f), MoveType::Relative)) {
			// Nothing to do...
		}
		while (j-- > 0 && MoveInstantly(Vector2f(0.0f, 1.0f), MoveType::Relative)) {
			// Nothing to do...
		}

		bool found = false;
		Vector2f targetPos = Vector2f(FLT_MAX, FLT_MAX);

		auto& players = _levelHandler->GetPlayers();
		for (auto player : players) {
			Vector2f newPos = player->GetPos();
			if ((_pos - newPos).Length() < (_pos - targetPos).Length()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			SetFacingLeft(targetPos.X < _pos.X);
		}

		_renderer.setDrawEnabled(true);

		_state = StateTransition;
		SetTransition((AnimState)1073741824, false, [this]() {
			SetState(ActorState::CanBeFrozen, true);

			_state = StateWaiting;
			_stateTime = 30.0f;
		});

		PlaySfx("Appear"_s, 0.8f);
	}

	Task<bool> Bilsy::Fireball::OnActivatedAsync(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		SetFacingLeft(details.Params[1] != 0);
		_timeLeft = 90.0f;

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithAmmo = false;

		_health = INT32_MAX;
		_speed.X = (IsFacingLeft() ? -4.0f : 4.0f);
		_speed.Y = 2.0f;

		switch (theme) {
			case 0:
			default:
				co_await RequestMetadataAsync("Boss/Bilsy"_s);
				break;

			case 1: // Xmas
				co_await RequestMetadataAsync("Boss/BilsyXmas"_s);
				break;
		}

		SetAnimation((AnimState)1073741828);

		PlaySfx("FireStart"_s);

		co_return true;
	}

	void Bilsy::Fireball::OnUpdate(float timeMult)
	{
		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);

		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_timeLeft -= timeMult;
			FollowNearestPlayer();
		}

		// TODO: Spawn fire particles
	}

	void Bilsy::Fireball::OnUpdateHitbox()
	{
		UpdateHitbox(18, 18);
	}

	void Bilsy::Fireball::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.85f;
		light.Brightness = 0.4f;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 30.0f;
	}

	bool Bilsy::Fireball::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto player = dynamic_cast<Player*>(other.get())) {
			DecreaseHealth(INT32_MAX);
		}

		return ActorBase::OnHandleCollision(other);
	}

	bool Bilsy::Fireball::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::RF);

		return EnemyBase::OnPerish(collider);
	}

	void Bilsy::Fireball::FollowNearestPlayer()
	{
		bool found = false;
		Vector2f targetPos = Vector2f(FLT_MAX, FLT_MAX);

		auto& players = _levelHandler->GetPlayers();
		for (auto player : players) {
			Vector2f newPos = player->GetPos();
			if ((_pos - newPos).Length() < (_pos - targetPos).Length()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			Vector2f diff = (targetPos - _pos).Normalized();
			Vector2f speed = (Vector2f(_speed.X, _speed.Y) + diff * 0.4f).Normalized();
			_speed.X = speed.X * 4.0f;
			_speed.Y = speed.Y * 4.0f;

			_renderer.setRotation(atan2f(_speed.Y, _speed.X));
		}
	}
}