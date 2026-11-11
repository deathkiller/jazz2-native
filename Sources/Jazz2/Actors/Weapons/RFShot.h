#pragma once

#include "ShotBase.h"
#include "../Player.h"

namespace Jazz2::Actors::Weapons
{
	/**
		@brief RF (shot)
		
		Rocket-fuel (RF) missile that flies straight while trailing smoke and explodes on impact with a wall or after
		its short lifetime, pushing nearby players away with its blast. RF missiles are fired in a spread, and the
		powered-up variant produces a larger explosion.
	*/
	class RFShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		/** @brief Creates a new instance */
		RFShot();

		/** @brief Called when the shot is fired */
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);
		/** @brief Sets which of the rockets fired together this is, -1 flies up from the aim, +1 down, 0 straight on, set before @ref OnFire() */
		void SetSpread(float spread) {
			_spread = spread;
		}

		WeaponType GetWeaponType() override {
			return WeaponType::RF;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		bool OnPerish(ActorBase* collider) override;
		void OnHitWall(float timeMult) override;
		void OnRicochet() override;

	private:
		/** @brief Ticks between a rocket reaching a wall and its blast moving anyone, the blast is a tick later and what it throws moves a tick after that */
		static constexpr float BlastDelayTicks = 2.0f;
		/** @brief Ticks a rocket is hidden for after it starts */
		static constexpr float HiddenTicks = 8.0f;
		/** @brief @ref _blastDelayLeft when the fuse has not been lit, so reaching a wall twice cannot restart it */
		static constexpr float BlastDelayUnarmed = -1.0f;

		Vector2f _gunspotPos;
		float _smokeTimer;
		/** @brief Counts @ref BlastDelayTicks down once a wall has been reached; @ref BlastDelayUnarmed until then */
		float _blastDelayLeft;
		float _hiddenLeft;
		float _spread;
		std::uint8_t _fired;
	};
}