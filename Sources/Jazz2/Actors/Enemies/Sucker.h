#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Sucker
		
		Small plunger-like creature that shuffles along the ground in short bursts, popping and squeaking
		as it moves and turning at walls and ledges. It harms the player only on contact and is defeated
		by a single hit; it can also spawn deflating from a destroyed @ref SuckerFloat.
	*/
	class Sucker : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Sucker();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool StopsAtStopEnemy() const override {
			return false;
		}
		float GetTurnLookAhead() const override {
			return 0.0f;
		}
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;
		void OnHitFloor(float timeMult) override;
		void OnHitWall(float timeMult) override;
		float GetGravityModifier(float baseGravity, bool isRising) const override;

	private:
		/** @brief Stage of a sucker that has just deflated */
		enum class FallStage : std::uint8_t {
			None,
			Deflating,
			Falling
		};

		/** @brief Acceleration sideways while deflating, in px per original tick squared */
		static constexpr float DeflateAcceleration = (1024.0f / 65536.0f) * TicksPerFrame * TicksPerFrame;
		/** @brief Fastest it flies sideways while deflating, in px per original tick */
		static constexpr float DeflateMaxSpeed = 6.0f * TicksPerFrame;
		/** @brief Speed it is pushed away at when it starts to deflate, and how long the deflating lasts in original ticks */
		static constexpr float DeflateStartSpeed = 3.0f * TicksPerFrame;
		static constexpr float DeflateTicks = 21.0f;
		/** @brief Speed it sinks at while deflating, in px per original tick */
		static constexpr float DeflateSinkSpeed = (2048.0f / 65536.0f) * TicksPerFrame;
		/** @brief Gravity while it falls, in px per original tick squared */
		static constexpr float FallGravity = (4096.0f / 65536.0f) * TicksPerFrame * TicksPerFrame;
		/** @brief Share of the sideways speed kept each original tick while it falls */
		static constexpr float FallSpeedKeptPerTick = 15.0f / 16.0f;

		std::int32_t _cycle;
		float _cycleTimer;
		bool _stuck;
		FallStage _fallStage;
	};
}